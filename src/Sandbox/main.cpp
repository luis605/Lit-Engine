#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
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
import Engine.GalleryScene;
import Engine.IkTestScene;
import Engine.Render.material;
import Sandbox.scene;

namespace {

constexpr float TARGET_SPEED = 3.0f;
constexpr float UPPER_ARM_LENGTH = 2.5f;
constexpr float LOWER_ARM_LENGTH = 2.5f;
constexpr float MAX_REACH = UPPER_ARM_LENGTH + LOWER_ARM_LENGTH;
constexpr float MIN_REACH = 0.001f;
constexpr float REACH_MARGIN = 0.0001f;
constexpr float DIRECTION_EPSILON = 0.0001f;
constexpr float PARALLEL_DOT_LIMIT = 0.99f;
constexpr float OPPOSITE_DOT_LIMIT = -0.999999f;
constexpr float ALIGNED_DOT_LIMIT = 0.999999f;
constexpr float PI = 3.14159265359f;
constexpr float STRESS_EXTENT = 200.0f;
constexpr float FAR_PLANE = 500.0f;

glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const float alignment = glm::dot(from, to);
    if (alignment > ALIGNED_DOT_LIMIT) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (alignment < OPPOSITE_DOT_LIMIT) {
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);
        if (glm::dot(axis, axis) < DIRECTION_EPSILON) axis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), from);
        return glm::angleAxis(PI, glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + alignment, axis.x, axis.y, axis.z));
}

glm::mat4 limbTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& size) {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), size);
}

void moveIkTarget(glm::vec3& target, float deltaTime) {
    if (InputManager::IsKeyHeld(GLFW_KEY_I)) target.x -= TARGET_SPEED * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_K)) target.x += TARGET_SPEED * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_J)) target.z -= TARGET_SPEED * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_L)) target.z += TARGET_SPEED * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_O)) target.y -= TARGET_SPEED * deltaTime;
    if (InputManager::IsKeyHeld(GLFW_KEY_P)) target.y += TARGET_SPEED * deltaTime;
}

void inverseKinematics(Scene& scene, float deltaTime) {
    World& world = scene.getCntx().world;
    if (!world.isAlive(scene.lower()) || !world.isAlive(scene.upper())) return;

    static glm::vec3 target = glm::vec3(1.5f, 1.5f, 0.0f);
    const glm::vec3 origin(0.0f);
    moveIkTarget(target, deltaTime);

    const glm::vec3 toTarget = target - origin;
    const float distance = glm::length(toTarget);
    const float clampedDistance = glm::clamp(distance, MIN_REACH, MAX_REACH - REACH_MARGIN);
    const glm::vec3 targetDirection = distance > DIRECTION_EPSILON ? toTarget / distance : glm::vec3(0.0f, 1.0f, 0.0f);

    const float cosShoulder = (UPPER_ARM_LENGTH * UPPER_ARM_LENGTH + clampedDistance * clampedDistance - LOWER_ARM_LENGTH * LOWER_ARM_LENGTH) / (2.0f * UPPER_ARM_LENGTH * clampedDistance);
    const float shoulderAngle = std::acos(glm::clamp(cosShoulder, -1.0f, 1.0f));

    glm::vec3 poleHint(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(targetDirection, poleHint)) > PARALLEL_DOT_LIMIT) poleHint = glm::vec3(0.0f, 0.0f, 1.0f);

    const glm::vec3 bendNormal = glm::normalize(glm::cross(targetDirection, poleHint));
    const glm::vec3 bendDirection = glm::normalize(glm::cross(bendNormal, targetDirection));
    const glm::vec3 upperDirection = targetDirection * std::cos(shoulderAngle) + bendDirection * std::sin(shoulderAngle);
    const glm::vec3 joint = origin + upperDirection * UPPER_ARM_LENGTH;

    const glm::vec3 toTargetFromJoint = target - joint;
    const glm::vec3 lowerDirection = glm::dot(toTargetFromJoint, toTargetFromJoint) > DIRECTION_EPSILON * DIRECTION_EPSILON ? glm::normalize(toTargetFromJoint) : upperDirection;

    const glm::vec3 stretchAxis(0.0f, 1.0f, 0.0f);
    const glm::vec3 limbSize(0.5f, 1.5f, 0.5f);
    const glm::vec3 upperCenter = origin + upperDirection * (UPPER_ARM_LENGTH * 0.5f);
    const glm::vec3 lowerCenter = joint + lowerDirection * (LOWER_ARM_LENGTH * 0.5f);

    world.setLocalMatrix(scene.lower(), limbTransform(upperCenter, rotationBetween(stretchAxis, upperDirection), limbSize));
    world.setLocalMatrix(scene.upper(), limbTransform(lowerCenter, rotationBetween(stretchAxis, lowerDirection), limbSize));
}

void configureEngine(Engine& engine, bool playable) {
    engine.input().bindKey("save", GLFW_KEY_F5);
    engine.input().bindKey("load", GLFW_KEY_F9);
    if (!playable) engine.enableFlyCamera({.speed = 2.0f, .sprintMultiplier = 2.0f});
    engine.setShaderWatch(true);
    engine.enableImGui();
    if (std::filesystem::exists("resources/data/params.json")) engine.params().loadFile("resources/data/params.json");
    if (std::filesystem::exists("resources/data/post_fx.json")) engine.loadPostFx("resources/data/post_fx.json");
    if (std::getenv("LIT_GPU_PROFILE")) engine.setFullProfiling(true);
}

void spawnTestScene(Engine& engine, const char* name, uint32_t cubeMesh, uint32_t sphereMesh) {
    TestSceneKit kit;
    kit.cube = cubeMesh;
    kit.sphere = sphereMesh;
    kit.material = [&engine](const glm::vec3& color, const glm::vec3& emissive, float intensity, float roughness) {
        Material material;
        material.baseColor = color;
        material.roughness = roughness;
        material.emissive = emissive;
        material.emissiveIntensity = intensity;
        return engine.createMaterial(material);
    };
    TestSceneInfo info;
    if (buildTestScene(engine, name, kit, info)) {
        applyTestSceneCamera(engine, info);
    } else {
        Lit::Log::Error("Unknown test scene {}", name);
    }
}

void startGallery(Engine& engine, uint32_t cubeMesh, uint32_t sphereMesh) {
    GalleryKit kit;
    kit.cube = cubeMesh;
    kit.sphere = sphereMesh;
    kit.colliders = true;
    kit.material = [&engine](const Material& material) { return engine.createMaterial(material); };
    buildGalleryScene(engine, kit);
    enableGalleryPlay(engine, kit);
}

void startIkTest(Engine& engine, uint32_t cubeMesh, uint32_t sphereMesh) {
    IkTestKit kit;
    kit.cube = cubeMesh;
    kit.sphere = sphereMesh;
    kit.debugDraw = true;
    kit.material = [&engine](const Material& material) { return engine.createMaterial(material); };
    buildIkTestScene(engine, kit);
    enableIkTestPlay(engine, kit);
}

void spawnStress(World& world, const char* countText, uint32_t cubeMesh, uint32_t sphereMesh) {
    const long long requested = std::atoll(countText);
    if (requested <= 0) return;
    const size_t count = static_cast<size_t>(requested);
    std::mt19937 generator(1);
    std::uniform_real_distribution<float> coordinate(-STRESS_EXTENT, STRESS_EXTENT);
    world.createBatch(count, [&](size_t index, EntityDesc& desc) {
        desc.name = "stress";
        desc.mesh = (index % 2 == 0) ? cubeMesh : sphereMesh;
        desc.position = glm::vec3(coordinate(generator), coordinate(generator), coordinate(generator));
        desc.shader = (index % 3 == 0) ? 1 : 0;
    });
    Lit::Log::Info("Spawned {} stress entities", count);
}

void pickUnderMouse(Engine& engine, World& world, EntityHandle& picked) {
    if (engine.isMouseCaptured() || !InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) return;
    const glm::vec2 mouse = InputManager::GetMousePosition();
    const auto hit = engine.pick(mouse.x, mouse.y);
    if (!hit) return;
    picked = hit->entity;
    Lit::Log::Info("Picked '{}' at distance {:.2f}", world.getName(hit->entity), hit->distance);
}

void drawDebugGizmos(Engine& engine, World& world, EntityHandle picked) {
    engine.debugHierarchy();
    engine.debugLine(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.1f, 0.1f, 1.0f));
    engine.debugLine(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec4(0.1f, 1.0f, 0.1f, 1.0f));
    engine.debugLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(0.2f, 0.4f, 1.0f, 1.0f));
    engine.debugSphere(glm::vec3(2.0f, 1.0f, 0.0f), 0.5f, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    if (world.isAlive(picked)) engine.debugBox(world.getWorldPosition(picked), world.getScale(picked) * 0.5f, glm::vec4(1.0f, 0.9f, 0.1f, 1.0f));
}

}

int main(int argumentCount, char** arguments) {
    const char* requestedScene = argumentCount > 1 ? arguments[1] : std::getenv("LIT_TESTSCENE");
    const bool gallery = requestedScene && std::string(requestedScene) == "gallery";
    const bool ikTest = requestedScene && std::string(requestedScene) == "ik";
    const bool playable = gallery || ikTest;
    Engine engine;
    if (!engine.open({"Lit Sandbox"})) return 1;

    const uint32_t cubeMesh = engine.loadMesh("cube");
    const uint32_t sphereMesh = engine.loadMesh("sphere");
    configureEngine(engine, playable);
    DebugOverlay overlay;
    overlay.setVisible(std::getenv("LIT_OVERLAY") != nullptr);

    World& world = engine.world();
    world.camera().setFarPlane(FAR_PLANE);

    Scene scene({engine, world, cubeMesh, sphereMesh});
    if (gallery) startGallery(engine, cubeMesh, sphereMesh);
    else if (ikTest) startIkTest(engine, cubeMesh, sphereMesh);
    else scene.onStart();

    if (requestedScene && !playable) spawnTestScene(engine, requestedScene, cubeMesh, sphereMesh);
    if (const char* stress = std::getenv("LIT_STRESS")) spawnStress(world, stress, cubeMesh, sphereMesh);

    bool showDebug = true;
    EntityHandle picked;

    return engine.run([&](float deltaTime) {
        pickUnderMouse(engine, world, picked);
        if (InputManager::IsKeyPressed(GLFW_KEY_F1)) showDebug = !showDebug;
        if (InputManager::IsKeyPressed(GLFW_KEY_F3)) overlay.toggle();
        overlay.draw(engine);
        if (showDebug && !playable) drawDebugGizmos(engine, world, picked);

        if (engine.input().pressed("save")) world.saveScene("resources/sandbox.litscene");
        if (engine.input().pressed("load") && world.loadScene("resources/sandbox.litscene")) scene.rebind();

        if (playable) return;
        inverseKinematics(scene, deltaTime);
        scene.onUpdate(deltaTime, static_cast<float>(engine.time().unscaledElapsed));
    });
}
