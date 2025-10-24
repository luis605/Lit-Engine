#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <print>

import application;
import engine;

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

    std::println("Application created");
}

Application::~Application() {
    m_engine.cleanup();
    glfwDestroyWindow(m_window);
    glfwTerminate();
    std::println("Application destroyed");
}

void Application::update() {
    m_engine.update(camera);

    glfwSwapBuffers(m_window);
    glfwPollEvents();
}

bool Application::isRunning() const { return !glfwWindowShouldClose(m_window); }