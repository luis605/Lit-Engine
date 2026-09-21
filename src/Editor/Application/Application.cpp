module;

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <thread>
#include "Engine/Log/Log.hpp"

module Editor.application;

import Engine.engine;
import Engine.mesh;
import Engine.World;
import Engine.Render.component;
import Engine.camera;
import Engine.input;
import Engine.glm;
import Engine.asset;

Application::Application() : m_world(m_engine.world()) {
    Lit::Log::Init();
    if (!glfwInit()) {
        Lit::Log::Fatal("Failed to initialize GLFW");
        return;
    }

    const int windowWidth = 1280;
    const int windowHeight = 720;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(windowWidth, windowHeight, "Lit Engine", nullptr, nullptr);
    if (!m_window) {
        Lit::Log::Fatal("Failed to create GLFW window");
        glfwTerminate();
        return;
    }

    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    InputManager::Init(m_window);
    m_engine.init(m_window, windowWidth, windowHeight);

    std::filesystem::create_directories("resources/models");
    std::filesystem::create_directories("resources/assets");

    Lit::Log::Info("Attempting to bake cube.obj. Please ensure 'resources/models/cube.obj' exists.");
    if (!AssetManager::bake("resources/models/cube.obj", "resources/assets/cube.asset")) { Lit::Log::Warn("Failed to bake asset. The application might not render anything."); }
    m_mesh = AssetManager::load("resources/assets/cube.asset");
    uint32_t cubeMeshUuid = 0;
    if (!m_mesh) {
        Lit::Log::Warn("Failed to load asset. The application might not render anything.");
    } else {
        cubeMeshUuid = m_engine.uploadMesh(*m_mesh);
    }

    if (!AssetManager::bake("resources/models/sphere.obj", "resources/assets/sphere.asset")) { Lit::Log::Warn("Failed to bake sphere asset."); }
    auto sphereMesh = AssetManager::load("resources/assets/sphere.asset");
    const uint32_t sphereMeshUuid = sphereMesh ? m_engine.uploadMesh(*sphereMesh) : 0;

    const int numObjects = 1000000;
    Lit::Log::Info("Creating {} random objects...", numObjects);

    std::random_device rd;
    std::mt19937 gen(std::getenv("LIT_SEED") ? static_cast<unsigned>(std::atoi(std::getenv("LIT_SEED"))) : rd());
    std::uniform_real_distribution<float> distribPosHeight(-150.0f, 150.0f);
    std::uniform_real_distribution<float> distribPosSideX(-150.0f, 100.0f);
    std::uniform_real_distribution<float> distribPosSideZ(-100.0f, 150.0f);
    std::uniform_int_distribution<unsigned int> distribType(0, 1);

    const auto createStart = std::chrono::steady_clock::now();
    m_world.reserve(numObjects + 1);

    EntityDesc parentDesc;
    parentDesc.name = "parent";
    parentDesc.mesh = cubeMeshUuid;
    m_parentEntity = m_world.create(parentDesc);

    m_movingObjectCount = static_cast<uint32_t>(std::min(numObjects, 150000));
    m_basePositions.resize(m_movingObjectCount);

    m_world.createBatch(numObjects, [&](size_t i, EntityDesc& desc) {
        desc.position = glm::vec3(distribPosSideX(gen), distribPosHeight(gen), distribPosSideZ(gen));
        if (i < m_movingObjectCount) { m_basePositions[i] = desc.position; }

        const bool isTransparent = (i % 50 == 0);
        desc.mesh = (distribType(gen) == 1) ? sphereMeshUuid : cubeMeshUuid;
        desc.shader = isTransparent ? 2 : (i % 2 == 0 ? 1 : 0);
        desc.alpha = isTransparent ? 0.6f : 1.0f;
        if (i < static_cast<size_t>(numObjects) / 2) { desc.parent = m_parentEntity; }
    });

    Lit::Log::Info("Created {} objects in {:.1f} ms", numObjects, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - createStart).count());

    m_engine.uploadBasePositions(m_basePositions);

    m_smallObjectThreshold = 0.0f;
    m_largeObjectThreshold = 0.0055f;

    m_world.camera().setFarPlane(2000.0f);
    m_world.camera().setPos(glm::vec3(0.0f, 1000.0f, 0.0f));
    m_engine.setFullProfiling(true);

    // benchmark overrides: LIT_CAM_POS=x,y,z  LIT_CAM_PITCH  LIT_CAM_YAW  LIT_FORCE_LOD  LIT_SEED
    if (const char* camPos = std::getenv("LIT_CAM_POS")) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (std::sscanf(camPos, "%f,%f,%f", &x, &y, &z) == 3) { m_world.camera().setPos(glm::vec3(x, y, z)); }
    }
    if (const char* camPitch = std::getenv("LIT_CAM_PITCH")) {
        const char* camYaw = std::getenv("LIT_CAM_YAW");
        m_world.camera().setOrientation(camYaw ? static_cast<float>(std::atof(camYaw)) : -90.0f, static_cast<float>(std::atof(camPitch)));
    }
    if (const char* forceLod = std::getenv("LIT_FORCE_LOD")) { m_engine.setForcedLod(std::atoi(forceLod)); }
    if (const char* lodBias = std::getenv("LIT_LOD_BIAS")) { m_lodBias = static_cast<float>(std::atof(lodBias)); }
    m_engine.setLodBias(m_lodBias);

    Lit::Log::Info("Application created");
}

Application::~Application() {
    m_engine.cleanup();
    glfwDestroyWindow(m_window);
    glfwTerminate();
    Lit::Log::Info("Application destroyed");
}

void  Application::update() {
   static float lastFrame = 0.0f;
   float currentFrame = glfwGetTime();
   float deltaTime = currentFrame - lastFrame;
   lastFrame = currentFrame;

   processInput(deltaTime);
   m_engine.tick(deltaTime);

   m_engine.setAnimation(currentFrame, m_movingObjectCount, 1);

   m_engine.setSmallObjectThreshold(m_smallObjectThreshold);
   m_engine.setLargeObjectThreshold(m_largeObjectThreshold);

   m_engine.update();

   m_textUpdateTimer += deltaTime;
   if (m_textUpdateTimer >= 0.5f) {
       m_frameTimeText = "Frame time: " + std::to_string(deltaTime * 1000.0f) + " ms (" + std::to_string(1.0f / deltaTime) + " FPS)";
       m_smallObjectThresholdText = "smallObjectThreshold: " + std::to_string(m_smallObjectThreshold);
       m_largeObjectThresholdText = "largeObjectThreshold: " + std::to_string(m_largeObjectThreshold);
       Lit::Log::Info("{}", m_frameTimeText);

       m_textUpdateTimer = 0.0f;
   }
   const glm::vec3 cameraPosition = m_world.camera().getPosition();
   m_cameraText = "camera: " + std::to_string(static_cast<int>(cameraPosition.x)) + ", " + std::to_string(static_cast<int>(cameraPosition.y)) + ", " + std::to_string(static_cast<int>(cameraPosition.z)) + "  pitch " + std::to_string(static_cast<int>(m_world.camera().getPitch())) + "  yaw " + std::to_string(static_cast<int>(m_world.camera().getYaw()));
   m_engine.AddText(m_cameraText, 10.0f, 90.0f, 0.5f, glm::vec3(1.0f, 1.0f, 1.0f));
   m_engine.AddText(m_frameTimeText, 10.0f, 30.0f, 0.5f, glm::vec3(1.0f, 1.0f, 1.0f));
   m_engine.AddText(m_smallObjectThresholdText, 10.0f, 50.0f, 0.5f, glm::vec3(1.0f, 1.0f, 1.0f));
   m_engine.AddText(m_largeObjectThresholdText, 10.0f, 70.0f, 0.5f, glm::vec3(1.0f, 1.0f, 1.0f));

   InputManager::Update();
   m_engine.present();
   glfwPollEvents();
}

void Application::processInput(float deltaTime) {
    static bool mouseLocked = false;
    if (InputManager::IsKeyPressed(GLFW_KEY_T)) {
        mouseLocked = !mouseLocked;
        if (mouseLocked) {
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    float moveSpeed = 4.0f;
    if (InputManager::IsKeyHeld(GLFW_KEY_LEFT_SHIFT)) moveSpeed = 100.0f;
    if (InputManager::IsKeyHeld(GLFW_KEY_W)) { m_world.camera().processKeyboard(CameraMovement::FORWARD, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_Q)) { m_world.camera().processKeyboard(CameraMovement::FORWARD, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_S)) { m_world.camera().processKeyboard(CameraMovement::BACKWARD, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_A)) { m_world.camera().processKeyboard(CameraMovement::LEFT, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_D)) { m_world.camera().processKeyboard(CameraMovement::RIGHT, moveSpeed * deltaTime); }

    if (InputManager::IsKeyHeld(GLFW_KEY_J)) {
        m_world.translate(m_parentEntity, glm::vec3(-10.0f * deltaTime, 0.0f, 0.0f));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_L)) {
        m_world.translate(m_parentEntity, glm::vec3(10.0f * deltaTime, 0.0f, 0.0f));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_I)) {
        m_world.translate(m_parentEntity, glm::vec3(0.0f, 0.0f, -10.0f * deltaTime));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_K)) {
        m_world.translate(m_parentEntity, glm::vec3(0.0f, 0.0f, 10.0f * deltaTime));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_U)) {
        m_world.translate(m_parentEntity, glm::vec3(0.0f, 10.0f * deltaTime, 0.0f));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_O)) {
        m_world.translate(m_parentEntity, glm::vec3(0.0f, -10.0f * deltaTime, 0.0f));
    }


    // 5 / 6 step the forced LOD: automatic <-> 0 ... 6
    {
        const int forcedLod = m_engine.getForcedLod();
        int newLod = forcedLod;
        if (InputManager::IsKeyPressed(GLFW_KEY_5)) newLod = std::max(forcedLod - 1, -1);
        if (InputManager::IsKeyPressed(GLFW_KEY_6)) newLod = std::min(forcedLod + 1, static_cast<int>(kLodLevelCount) - 1);
        if (newLod != forcedLod) {
            m_engine.setForcedLod(newLod);
            if (newLod < 0) {
                Lit::Log::Info("LOD selection: automatic");
            } else {
                Lit::Log::Info("LOD selection: forced LOD {}", newLod);
            }
        }
    }

    if (InputManager::IsKeyPressed(GLFW_KEY_1)) {
        m_smallObjectThreshold -= 0.0002f;
        if (m_smallObjectThreshold < 0.0f) { m_smallObjectThreshold = 0.0f; }
        Lit::Log::Info("smallObjectThreshold: {:.5f}", m_smallObjectThreshold);
    }
    if (InputManager::IsKeyPressed(GLFW_KEY_2)) {
        m_smallObjectThreshold += 0.0002f;
        Lit::Log::Info("smallObjectThreshold: {:.5f}", m_smallObjectThreshold);
    }

    if (InputManager::IsKeyPressed(GLFW_KEY_3)) {
        m_largeObjectThreshold -= 0.001f;
        if (m_largeObjectThreshold < 0.0f) { m_largeObjectThreshold = 0.0f; }
        Lit::Log::Info("largeObjectThreshold: {:.5f}", m_largeObjectThreshold);
    }
    if (InputManager::IsKeyPressed(GLFW_KEY_4)) {
        m_largeObjectThreshold += 0.001f;
        Lit::Log::Info("largeObjectThreshold: {:.5f}", m_largeObjectThreshold);
    }

    if (InputManager::IsKeyPressed(GLFW_KEY_7) || InputManager::IsKeyPressed(GLFW_KEY_8)) {
        m_lodBias *= InputManager::IsKeyPressed(GLFW_KEY_8) ? 1.25f : 0.8f;
        m_engine.setLodBias(m_lodBias);
        Lit::Log::Info("lodBias: {:.3f}", m_lodBias);
    }

    if (InputManager::IsKeyPressed(GLFW_KEY_F1)) {
        m_engine.setDebugDepthMode(!m_engine.isDebugDepthMode());
        Lit::Log::Info("Debug Depth Mode: {}", m_engine.isDebugDepthMode() ? "ON" : "OFF");
    }

    if (InputManager::IsKeyPressed(GLFW_KEY_F2)) {
        const glm::vec3 pos = m_world.camera().getPosition();
        Lit::Log::Info("Camera position: LIT_CAM_POS={:.1f},{:.1f},{:.1f}", pos.x, pos.y, pos.z);
    }

    if (InputManager::IsKeyPressed(GLFW_KEY_F2)) {
        const glm::vec3 pos = m_world.camera().getPosition();
        Lit::Log::Info("Camera position: LIT_CAM_POS={:.1f},{:.1f},{:.1f}", pos.x, pos.y, pos.z);
    }

    glm::vec2 mouseDelta = InputManager::GetMouseDelta();
    m_world.camera().processMouseMovement(mouseDelta.x, -mouseDelta.y);
}

bool Application::isRunning() const { return !glfwWindowShouldClose(m_window); }
