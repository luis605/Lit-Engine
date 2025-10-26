#include <glad/glad.h>
#include <GLFW/glfw3.h>

import Editor.application;
import Engine.engine;
import Engine.mesh;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.camera;
import Engine.input;
import Engine.glm;
import std;

Application::Application() {
    if (!glfwInit()) {
        std::println("Failed to initialize GLFW");
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(1280, 720, "Lit Engine", nullptr, nullptr);
    if (!m_window) {
        std::println("Failed to create GLFW window");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::println("Failed to initialize GLAD");
        return;
    }

    InputManager::Init(m_window);
    m_engine.init();

    auto entity = m_sceneDatabase.createEntity();
    m_sceneDatabase.transforms[entity].localMatrix = glm::mat4(1.0f);
    m_sceneDatabase.renderables[entity].mesh_uuid = 0; // Placeholder
    m_sceneDatabase.renderables[entity].material_uuid = 0; // Placeholder
    m_sceneDatabase.renderables[entity].shaderId = 0; // Placeholder
    m_sceneDatabase.renderables[entity].objectId = entity;

    std::println("Application created");
}

Application::~Application() {
    m_engine.cleanup();
    glfwDestroyWindow(m_window);
    glfwTerminate();
    std::println("Application destroyed");
}

void Application::update() {
    static float lastFrame = 0.0f;
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    processInput(deltaTime);

    m_engine.update(m_sceneDatabase, camera);

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

    glm::vec2 mouseDelta = InputManager::GetMouseDelta();
    camera.processMouseMovement(mouseDelta.x, -mouseDelta.y);
}

bool Application::isRunning() const { return !glfwWindowShouldClose(m_window); }