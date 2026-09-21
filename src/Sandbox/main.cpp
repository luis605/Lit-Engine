#include <GLFW/glfw3.h>
#include <filesystem>
#include <optional>
#include <string>
#include <iostream>
#include <cmath>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.camera;
import Engine.input;
import Engine.mesh;
import Engine.asset;
import Engine.World;
import Engine.glm;
import Sandbox.scene;

static uint32_t loadMesh(Engine& engine, const std::string& name) {
    const std::string src = "resources/models/" + name + ".obj";
    const std::string dst = "resources/assets/" + name + ".asset";
    if (!AssetManager::bake(src, dst)) { Lit::Log::Warn("Failed to bake {}", src); }
    auto mesh = AssetManager::load(dst);
    if (!mesh) {
        Lit::Log::Warn("Failed to load {}", dst);
        return 0;
    }
    return engine.uploadMesh(*mesh);
}

void inverseKinematics(Scene& scene, const float deltaTime) {
    SceneContext& sceneCtx = scene.getCntx();

    static glm::vec3 target = glm::vec3(1.5f, 1.5f, 0.0f);
    glm::vec3 origin(0.0f, 0.0f, 0.0f);

    constexpr float speed = 3.0f;

    if (InputManager::IsKeyHeld(GLFW_KEY_I)) target.x += speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_K)) target.x -= speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_J)) target.z += speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_L)) target.z -= speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_O)) target.y += speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_P)) target.y -= speed * deltaTime;

    constexpr float L1 = 1.5f;
    constexpr float L2 = 1.5f;
    constexpr float maxDistance = L1 + L2;
    constexpr float minDistance = 0.001f;

    glm::vec3 toTarget = target - origin;
    float dist = glm::length(toTarget);

    float clampedDist = glm::clamp(dist, minDistance, maxDistance - 0.0001f);
    glm::vec3 targetDir = (dist > 0.0001f) ? (toTarget / dist) : glm::vec3(0.0f, 1.0f, 0.0f);

    float cosAlpha = (L1 * L1 + clampedDist * clampedDist - L2 * L2) / (2.0f * L1 * clampedDist);
    cosAlpha = glm::clamp(cosAlpha, -1.0f, 1.0f);
    float alpha = std::acos(cosAlpha);

    glm::vec3 poleHint(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(targetDir, poleHint)) > 0.99f) {
        poleHint = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 bendNormal = glm::normalize(glm::cross(targetDir, poleHint));
    glm::vec3 bendDir    = glm::normalize(glm::cross(bendNormal, targetDir));

    glm::vec3 dir1 = targetDir * std::cos(alpha) + bendDir * std::sin(alpha);
    glm::vec3 joint = origin + dir1 * L1;

    glm::vec3 dir2 = glm::normalize(target - joint);

    const glm::vec3 localStretchAxis(0.0f, 1.0f, 0.0f);

    auto getRotation = [](glm::vec3 u, glm::vec3 v) -> glm::quat {
        float d = glm::dot(u, v);
        if (d > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (d < -0.999999f) {
            glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), u);
            if (glm::dot(axis, axis) < 0.0001f) {
                axis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), u);
            }
            return glm::angleAxis(3.14159265359f, glm::normalize(axis));
        }
        glm::vec3 axis = glm::cross(u, v);
        return glm::normalize(glm::quat(1.0f + d, axis.x, axis.y, axis.z));
    };
    
    glm::quat rot1 = getRotation(localStretchAxis, dir1);
    glm::quat rot2 = getRotation(localStretchAxis, dir2);

    const glm::vec3 size(0.5f, 1.5f, 0.5f);

    glm::vec3 pos0 = origin + dir1 * (L1 * 0.5f);
    glm::vec3 pos1 = joint  + dir2 * (L2 * 0.5f);

    glm::mat4 m0 = glm::translate(glm::mat4(1.0f), pos0) * glm::mat4_cast(rot1) * glm::scale(glm::mat4(1.0f), size);
    glm::mat4 m1 = glm::translate(glm::mat4(1.0f), pos1) * glm::mat4_cast(rot2) * glm::scale(glm::mat4(1.0f), size);

    sceneCtx.world.setLocalMatrix(scene.lower(), m0);
    sceneCtx.world.setLocalMatrix(scene.upper(), m1);
}

int main() {
    Lit::Log::Init();
    if (!glfwInit()) {
        Lit::Log::Fatal("Failed to initialize GLFW");
        return 1;
    }

    const int width = 1280;
    const int height = 720;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(width, height, "Lit Sandbox", nullptr, nullptr);
    if (!window) {
        Lit::Log::Fatal("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    InputManager::Init(window);

    Engine engine;
    engine.init(window, width, height);

    std::filesystem::create_directories("resources/assets");
    const uint32_t cubeMesh = loadMesh(engine, "cube");
    const uint32_t sphereMesh = loadMesh(engine, "sphere");

    World world;
    Camera& camera = world.camera();
    camera.setFarPlane(500.0f);

    Scene scene({engine, world, cubeMesh, sphereMesh});
    scene.onStart();

    bool mouseLocked = false;
    float lastTime = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(window)) {
        const float now = -static_cast<float>(glfwGetTime());
        const float deltaTime = now - lastTime;
        lastTime = now;

        if (InputManager::IsKeyPressed(GLFW_KEY_T)) {
            mouseLocked = !mouseLocked;
            glfwSetInputMode(window, GLFW_CURSOR, mouseLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }

        const float speed = InputManager::IsKeyHeld(GLFW_KEY_LEFT_SHIFT) ? 4.0f : 2.0f;
        if (InputManager::IsKeyHeld(GLFW_KEY_S)) camera.processKeyboard(CameraMovement::FORWARD, speed * deltaTime);
        if (InputManager::IsKeyHeld(GLFW_KEY_W)) camera.processKeyboard(CameraMovement::BACKWARD, speed * deltaTime);
        if (InputManager::IsKeyHeld(GLFW_KEY_D)) camera.processKeyboard(CameraMovement::LEFT, speed * deltaTime);
        if (InputManager::IsKeyHeld(GLFW_KEY_A)) camera.processKeyboard(CameraMovement::RIGHT, speed * deltaTime);
        if (mouseLocked) {
            const glm::vec2 delta = InputManager::GetMouseDelta();
            camera.processMouseMovement(delta.x, -delta.y);
        }

        inverseKinematics(scene, deltaTime);

        scene.onUpdate(deltaTime, now);

        engine.update(world);
        InputManager::Update();
        engine.present();
        glfwPollEvents();
    }

    engine.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
