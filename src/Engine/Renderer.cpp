module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

module renderer;

import glm;
import camera;
import shader;
import std;

Renderer::Renderer()
    : m_shader(nullptr), m_cubeVAO(0), m_cubeVBO(0), m_cubeEBO(0), m_initialized(false) {}

void Renderer::init() {
    if (m_initialized)
        return;

    setupShaders();

    if (!m_shader || !m_shader->isInitialized()) {
        std::cerr << "Renderer failed to initialize: Shaders could not be loaded." << std::endl;
        return;
    }

    setupCubeMesh();
    glEnable(GL_DEPTH_TEST);
    m_initialized = true;
}

void Renderer::cleanup() {
    if (!m_initialized)
        return;

    glDeleteVertexArrays(1, &m_cubeVAO);
    glDeleteBuffers(1, &m_cubeVBO);
    glDeleteBuffers(1, &m_cubeEBO);

    m_shader.reset();
    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

void Renderer::drawScene(const Camera& camera) {
    if (!m_initialized)
        return;

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader->bind();

    m_shader->setUniform("u_colorA", glm::vec3(0.1f, 0.0f, 0.5f));
    m_shader->setUniform("u_colorB", glm::vec3(1.0f, 0.0f, 0.2f));

    glm::mat4 projection = camera.getProjectionMatrix();
    glm::mat4 view = camera.getViewMatrix();

    glm::mat4 model = glm::mat4(1.0f);
    float angle = (float)glfwGetTime() * glm::radians(50.0f);
    model = glm::rotate(model, angle, glm::normalize(glm::vec3(1.0f, 0.5f, 0.2f)));

    m_shader->setUniform("projection", projection);
    m_shader->setUniform("view", view);
    m_shader->setUniform("model", model);

    glBindVertexArray(m_cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    m_shader->unbind();
}

void Renderer::setupShaders() {

    m_shader = std::make_unique<Shader>("shaders/cube.vert", "shaders/cube.frag");
}

void Renderer::setupCubeMesh() {
    float vertices[] = {-0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, 0.5f, 0.5f,
                        -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f, 0.5f,
                        -0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  -0.5f, 0.5f, 0.5f};

    unsigned int indices[] = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
                              4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4};

    glGenVertexArrays(1, &m_cubeVAO);
    glBindVertexArray(m_cubeVAO);

    glGenBuffers(1, &m_cubeVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &m_cubeEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}