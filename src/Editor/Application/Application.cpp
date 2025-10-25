#include <glad/glad.h>
#include <GLFW/glfw3.h>

import application;
import engine;
import mesh;
import model;
import scene;
import camera;
import input;
import glm;
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

    std::vector<float> vertices = {-0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, 0.5f, 0.5f,
                                   -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f, 0.5f,
                                   -0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  -0.5f, 0.5f, 0.5f};

    std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
                                         4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4};

    std::vector<Mesh> meshes;
    meshes.emplace_back(vertices, indices);

    m_scene.addModel(Model(std::move(meshes)));

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

    m_engine.update(m_scene, camera);

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