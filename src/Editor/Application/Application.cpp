module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <random>

module Editor.application;

import Engine.engine;
import Engine.mesh;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.camera;
import Engine.input;
import Engine.glm;
import Engine.asset;
import Engine.Log;

Application::Application() {
    Lit::Log::Init();
    if (!glfwInit()) {
        Lit::Log::Fatal("Failed to initialize GLFW");
        return;
    }

    const int windowWidth = 1280;
    const int windowHeight = 720;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(windowWidth, windowHeight, "Lit Engine", nullptr, nullptr);
    if (!m_window) {
        Lit::Log::Fatal("Failed to create GLFW window");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        Lit::Log::Fatal("Failed to initialize GLAD");
        return;
    }

    InputManager::Init(m_window);
    m_engine.init(windowWidth, windowHeight);

    std::filesystem::create_directories("resources/models");
    std::filesystem::create_directories("resources/assets");

    Lit::Log::Info("Attempting to bake cube.obj. Please ensure 'resources/models/cube.obj' exists.");
    if (!AssetManager::bake("resources/models/cube.obj", "resources/assets/cube.asset")) {
        Lit::Log::Warn("Failed to bake asset. The application might not render anything.");
    }
    m_mesh = AssetManager::load("resources/assets/cube.asset");
    if (!m_mesh) {
        Lit::Log::Warn("Failed to load asset. The application might not render anything.");
    } else {
        m_engine.uploadMesh(*m_mesh);
    }

    if (!AssetManager::bake("resources/models/sphere.obj", "resources/assets/sphere.asset")) {
        Lit::Log::Warn("Failed to bake sphere asset.");
    }
    auto sphereMesh = AssetManager::load("resources/assets/sphere.asset");
    if (sphereMesh) {
        m_engine.uploadMesh(*sphereMesh);
    }

    const int numObjects = 50000;
    Lit::Log::Info("Creating {} random objects...", numObjects);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distribPos(-50.0f, 50.0f);
    std::uniform_int_distribution<unsigned int> distribMesh(0, 1);

    for (int i = 0; i < numObjects; ++i) {
        auto entity = m_sceneDatabase.createEntity();

        glm::vec3 position(distribPos(gen), distribPos(gen), distribPos(gen));
        glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
        m_sceneDatabase.transforms[entity].localMatrix = model;

        m_sceneDatabase.renderables[entity].mesh_uuid = distribMesh(gen);
        m_sceneDatabase.renderables[entity].material_uuid = 0;

        if (i % 2 == 0) {
            m_sceneDatabase.renderables[entity].shaderId = 2;
            m_sceneDatabase.renderables[entity].alpha = 0.5f;
        } else {
            m_sceneDatabase.renderables[entity].shaderId = i % 4 == 0 ? 1 : 0;
            m_sceneDatabase.renderables[entity].alpha = 1.0f;
        }
    }

    m_parentEntity = m_sceneDatabase.createEntity();
    m_sceneDatabase.transforms[m_parentEntity].localMatrix = glm::mat4(1.0f);
    m_sceneDatabase.renderables[m_parentEntity].mesh_uuid = 0;
    m_sceneDatabase.renderables[m_parentEntity].material_uuid = 0;
    m_sceneDatabase.renderables[m_parentEntity].shaderId = 0;

    for (int i = 0; i < numObjects / 2; ++i) {
        m_sceneDatabase.hierarchies[i].parent = m_parentEntity;
    }

    Lit::Log::Info("Application created");
}

Application::~Application() {
    m_engine.cleanup();
    glfwDestroyWindow(m_window);
    glfwTerminate();
    Lit::Log::Info("Application destroyed");
}

void Application::update() {
    static float lastFrame = 0.0f;
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    processInput(deltaTime);

    m_engine.update(m_sceneDatabase, camera);

    std::string frameTimeText = "Frame time: " + std::to_string(deltaTime * 1000.0f) + " ms (" + std::to_string(1.0f / deltaTime) + " FPS)";
    m_engine.AddText(frameTimeText, 10.0f, 690.0f, 0.5f, glm::vec3(1.0f, 1.0f, 1.0f));

    InputManager::Update();
    glfwSwapBuffers(m_window);
    glfwPollEvents();
}

void Application::processInput(float deltaTime) {
    if (InputManager::IsKeyHeld(GLFW_KEY_W)) {
        camera.processKeyboard(CameraMovement::FORWARD, deltaTime);
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_S)) {
        camera.processKeyboard(CameraMovement::BACKWARD, deltaTime);
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_A)) {
        camera.processKeyboard(CameraMovement::LEFT, deltaTime);
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_D)) {
        camera.processKeyboard(CameraMovement::RIGHT, deltaTime);
    }

    if (InputManager::IsKeyHeld(GLFW_KEY_J)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix =
            glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(-10.0f * deltaTime, 0.0f, 0.0f));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_L)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix =
            glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(10.0f * deltaTime, 0.0f, 0.0f));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_I)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix =
            glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, 0.0f, -10.0f * deltaTime));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_K)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix =
            glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, 0.0f, 10.0f * deltaTime));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_U)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix =
            glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, 10.0f * deltaTime, 0.0f));
    }
    if (InputManager::IsKeyHeld(GLFW_KEY_O)) {
        m_sceneDatabase.transforms[m_parentEntity].localMatrix =
            glm::translate(m_sceneDatabase.transforms[m_parentEntity].localMatrix, glm::vec3(0.0f, -10.0f * deltaTime, 0.0f));
    }

    glm::vec2 mouseDelta = InputManager::GetMouseDelta();
    camera.processMouseMovement(mouseDelta.x, -mouseDelta.y);
}

bool Application::isRunning() const { return !glfwWindowShouldClose(m_window); }