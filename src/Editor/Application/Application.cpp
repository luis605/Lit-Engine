#include <glad/glad.h>
#include <GLFW/glfw3.h>

import application;
import engine;
import mesh;
import model;
import scene;
import camera;
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
    m_engine.update(m_scene, camera);
    glfwSwapBuffers(m_window);
    glfwPollEvents();
}

bool Application::isRunning() const { return !glfwWindowShouldClose(m_window); }