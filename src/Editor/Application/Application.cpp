module;

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <thread>
#include "Engine/Log/Log.hpp"

module Editor.application;

import Engine.engine;
import Engine.mesh;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.camera;
import Engine.input;
import Engine.glm;
import Engine.asset;

Application::Application() {
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
    std::vector<uint32_t> sphereLODs;
    if (sphereMesh) {
        sphereLODs = m_engine.uploadMeshWithLODs(*sphereMesh, {0.35f, 0.10f, 0.03f});
    } else {
        sphereLODs.push_back(0);
    }

    const int numObjects = 2500000;
    Lit::Log::Info("Creating {} random objects...", numObjects);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distribPosHeight(-900.0f, 900.0f);
    std::uniform_real_distribution<float> distribPosSides(-150.0f, 150.0f);
    std::uniform_int_distribution<unsigned int> distribType(0, 1);

    m_parentEntity = m_sceneDatabase.createEntity();
    m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::mat4(1.0f);
    m_sceneDatabase.renderables[m_parentEntity].mesh_uuid = cubeMeshUuid;
    m_sceneDatabase.renderables[m_parentEntity].material_uuid = 0;
    m_sceneDatabase.renderables[m_parentEntity].shaderId = 0;

    m_movingObjectCount = static_cast<uint32_t>(std::min(numObjects, 1500000));
    m_basePositions.resize(m_movingObjectCount);

    for (int i = 0; i < numObjects; ++i) {
        auto entity = m_sceneDatabase.createEntity();

        glm::vec3 position(distribPosSides(gen), distribPosHeight(gen), distribPosSides(gen));
        glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
        m_sceneDatabase.transforms[entity].localMatrix = model;

        if (static_cast<uint32_t>(i) < m_movingObjectCount) { m_basePositions[i] = position; }

        uint32_t assignedMesh = (distribType(gen) == 1) ? sphereLODs[0] : cubeMeshUuid;

        m_sceneDatabase.renderables[entity].mesh_uuid = assignedMesh;
        m_sceneDatabase.renderables[entity].material_uuid = 0;

        const bool isTransparent = (i % 50 == 0);
        m_sceneDatabase.renderables[entity].shaderId = isTransparent ? 2 : (i % 2 == 0 ? 1 : 0);
        m_sceneDatabase.renderables[entity].alpha = isTransparent ? 0.6f : 1.0f;

        if (i < numObjects / 2) {
            m_sceneDatabase.hierarchies[entity].parent = m_parentEntity;
        }
    }

    m_engine.uploadBasePositions(m_basePositions);

    m_smallObjectThreshold = 0.0009f;
    m_largeObjectThreshold = 0.0055f;

    m_sceneDatabase.markHierarchyDirty();
    m_sceneDatabase.markDataDirty();

    camera.setFarPlane(2000.0f);
    camera.setPos(glm::vec3(0.0f, 1000.0f, 0.0f));
    m_engine.setFullProfiling(true);

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

   m_engine.setAnimation(currentFrame, m_movingObjectCount, 1);

   m_engine.setSmallObjectThreshold(m_smallObjectThreshold);
   m_engine.setLargeObjectThreshold(m_largeObjectThreshold);

   m_engine.update(m_sceneDatabase, camera);

   m_textUpdateTimer += deltaTime;
   if (m_textUpdateTimer >= 0.5f) {
       m_frameTimeText = "Frame time: " + std::to_string(deltaTime * 1000.0f) + " ms (" + std::to_string(1.0f / deltaTime) + " FPS)";
       m_smallObjectThresholdText = "smallObjectThreshold: " + std::to_string(m_smallObjectThreshold);
       m_largeObjectThresholdText = "largeObjectThreshold: " + std::to_string(m_largeObjectThreshold);
       Lit::Log::Info("{}", m_frameTimeText);

       m_textUpdateTimer = 0.0f;
   }
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

    float moveSpeed = 1.0f;
    if (InputManager::IsKeyHeld(GLFW_KEY_LEFT_SHIFT)) moveSpeed = 100.0f;
    if (InputManager::IsKeyHeld(GLFW_KEY_W)) { camera.processKeyboard(CameraMovement::FORWARD, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_Q)) { camera.processKeyboard(CameraMovement::FORWARD, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_S)) { camera.processKeyboard(CameraMovement::BACKWARD, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_A)) { camera.processKeyboard(CameraMovement::LEFT, moveSpeed * deltaTime); }
    if (InputManager::IsKeyHeld(GLFW_KEY_D)) { camera.processKeyboard(CameraMovement::RIGHT, moveSpeed * deltaTime); }

    bool dataChanged = false;
    if (InputManager::IsKeyHeld(GLFW_KEY_J)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(-10.0f * deltaTime, 0.0f, 0.0f));
        dataChanged = true;
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_L)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(10.0f * deltaTime, 0.0f, 0.0f));
        dataChanged = true;
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_I)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, 0.0f, -10.0f * deltaTime));
        dataChanged = true;
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_K)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, 0.0f, 10.0f * deltaTime));
        dataChanged = true;
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_U)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, 10.0f * deltaTime, 0.0f));
        dataChanged = true;
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_O)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, -10.0f * deltaTime, 0.0f));
        dataChanged = true;
    }

    if (dataChanged) { m_sceneDatabase.markTransformsDirty(m_movingObjectCount + 1); }

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

    if (InputManager::IsKeyPressed(GLFW_KEY_F1)) {
        m_engine.setDebugDepthMode(!m_engine.isDebugDepthMode());
        Lit::Log::Info("Debug Depth Mode: {}", m_engine.isDebugDepthMode() ? "ON" : "OFF");
    }

    glm::vec2 mouseDelta = InputManager::GetMouseDelta();
    camera.processMouseMovement(mouseDelta.x, -mouseDelta.y);
}

bool Application::isRunning() const { return !glfwWindowShouldClose(m_window); }