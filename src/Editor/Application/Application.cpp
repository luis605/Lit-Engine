#include <glad/glad.h>
#include <GLFW/glfw3.h>

import application;
#include <print>
#include <cmath>

Application::Application() {
    if (!glfwInit()) {
        std::println("Failed to initialize GLFW");
        return;
    }

    m_window = glfwCreateWindow(1280, 720, "Lit Engine", nullptr, nullptr);
    if (!m_window) {
        std::println("Failed to create GLFW window");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);
    if (!gladLoadGL()) {
        std::println("Failed to initialize OpenGL");
        return;
    }
    std::println("Application created");
}

Application::~Application() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
    std::println("Application destroyed");
}

void Application::update() {
    double time = glfwGetTime();
    float r = (sin(time) + 1.0f) / 2.0f;
    float g = (cos(time) + 1.0f) / 2.0f;
    float b = 0.5f;

    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glfwSwapBuffers(m_window);
    glfwPollEvents();
}

bool Application::isRunning() const {
    return !glfwWindowShouldClose(m_window);
}
