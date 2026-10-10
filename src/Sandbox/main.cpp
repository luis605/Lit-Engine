#include <GLFW/glfw3.h>
#include <filesystem>
#include <optional>
#include <string>
#include <cmath>
#include <cstdlib>
#include <random>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.camera;
import Engine.input;
import Engine.mesh;
import Engine.asset;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Overlay;
import Engine.TestScenes;
import Engine.Render.material;
import Sandbox.scene;

void inverseKinematics(Scene& scene, const float deltaTime) {
    SceneContext& sceneCtx = scene.getCntx();

    static glm::vec3 target = glm::vec3(1.5f, 1.5f, 0.0f);
    glm::vec3 origin(0.0f, 0.0f, 0.0f);

    constexpr float speed = 3.0f;

    if (InputManager::IsKeyHeld(GLFW_KEY_I)) target.x -= speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_K)) target.x += speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_J)) target.z -= speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_L)) target.z += speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_O)) target.y -= speed * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_P)) target.y += speed * deltaTime;

    constexpr float L1 = 2.5f;
    constexpr float L2 = 2.5f;
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
    Engine engine;
    if (!engine.open({"Lit Sandbox"})) return 1;

    const uint32_t cubeMesh = engine.loadMesh("cube");
    const uint32_t sphereMesh = engine.loadMesh("sphere");

    engine.input().bindKey("save", GLFW_KEY_F5);
    engine.input().bindKey("load", GLFW_KEY_F9);
    engine.enableFlyCamera({.speed = 2.0f, .sprintMultiplier = 2.0f});
    engine.setShaderWatch(true);
    engine.enableImGui();
    if (std::filesystem::exists("resources/data/params.json")) engine.params().loadFile("resources/data/params.json");
    if (std::filesystem::exists("resources/data/post_fx.json")) engine.loadPostFx("resources/data/post_fx.json");
    DebugOverlay overlay;
    overlay.setVisible(std::getenv("LIT_OVERLAY") != nullptr);
    if (std::getenv("LIT_GPU_PROFILE")) engine.setFullProfiling(true);

    World& world = engine.world();
    world.camera().setFarPlane(500.0f);

    Scene scene({engine, world, cubeMesh, sphereMesh});
    scene.onStart();

    if (const char* testScene = std::getenv("LIT_TESTSCENE")) {
        TestSceneKit kit;
        kit.cube = cubeMesh;
        kit.sphere = sphereMesh;
        kit.material = [&engine](const glm::vec3& color, const glm::vec3& emissive, float intensity, float roughness) {
            Material m;
            m.baseColor = color;
            m.roughness = roughness;
            m.emissive = emissive;
            m.emissiveIntensity = intensity;
            return engine.createMaterial(m);
        };
        TestSceneInfo info;
        if (buildTestScene(engine, testScene, kit, info)) applyTestSceneCamera(engine, info);
        else Lit::Log::Error("Unknown test scene {}", testScene);
    }

    if (const char* stress = std::getenv("LIT_STRESS")) {
        const size_t count = static_cast<size_t>(std::atoll(stress));
        std::mt19937 gen(1);
        std::uniform_real_distribution<float> dist(-200.0f, 200.0f);
        world.createBatch(count, [&](size_t i, EntityDesc& desc) {
            desc.name = "stress";
            desc.mesh = (i % 2 == 0) ? cubeMesh : sphereMesh;
            desc.position = glm::vec3(dist(gen), dist(gen), dist(gen));
            desc.shader = (i % 3 == 0) ? 1 : 0;
        });
        Lit::Log::Info("Spawned {} stress entities", count);
    }

    bool showDebug = true;
    EntityHandle picked;

    return engine.run([&](float deltaTime) {
        if (!engine.isMouseCaptured() && InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
            const glm::vec2 mouse = InputManager::GetMousePosition();
            if (const auto hit = engine.pick(mouse.x, mouse.y)) {
                picked = hit->entity;
                Lit::Log::Info("Picked '{}' at distance {:.2f}", world.getName(hit->entity), hit->distance);
            }
        }

        if (InputManager::IsKeyPressed(GLFW_KEY_F1)) showDebug = !showDebug;
        if (InputManager::IsKeyPressed(GLFW_KEY_F3)) overlay.toggle();
        overlay.draw(engine);
        if (showDebug) {
            engine.debugHierarchy();
            engine.debugLine(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.1f, 0.1f, 1.0f));
            engine.debugLine(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec4(0.1f, 1.0f, 0.1f, 1.0f));
            engine.debugLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(0.2f, 0.4f, 1.0f, 1.0f));
            engine.debugSphere(glm::vec3(2.0f, 1.0f, 0.0f), 0.5f, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
            if (world.isAlive(picked)) engine.debugBox(world.getWorldPosition(picked), world.getScale(picked) * 0.5f, glm::vec4(1.0f, 0.9f, 0.1f, 1.0f));
        }

        if (engine.input().pressed("save")) world.saveScene("resources/sandbox.litscene");
        if (engine.input().pressed("load") && world.loadScene("resources/sandbox.litscene")) scene.rebind();

        inverseKinematics(scene, deltaTime);
        scene.onUpdate(deltaTime, static_cast<float>(engine.time().unscaledElapsed));
    });
}
