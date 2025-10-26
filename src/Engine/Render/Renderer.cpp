module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.shader;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import std;

Renderer::Renderer() : m_shader(nullptr), m_initialized(false) {}

void Renderer::init() {
    if (m_initialized)
        return;

    setupShaders();

    if (!m_shader || !m_shader->isInitialized()) {
        std::cerr << "Renderer failed to initialize: Shaders could not be loaded." << std::endl;
        return;
    }

    glEnable(GL_DEPTH_TEST);
    m_initialized = true;
}

void Renderer::cleanup() {
    if (!m_initialized)
        return;

    m_shader.reset();
    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

void Renderer::drawScene(const SceneDatabase& sceneDatabase, const Camera& camera) {
    if (!m_initialized)
        return;

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader->bind();

    m_shader->setUniform("u_colorA", glm::vec3(0.1f, 0.0f, 0.5f));
    m_shader->setUniform("u_colorB", glm::vec3(1.0f, 0.0f, 0.2f));

    glm::mat4 projection = camera.getProjectionMatrix();
    glm::mat4 view = camera.getViewMatrix();

    m_shader->setUniform("projection", projection);
    m_shader->setUniform("view", view);

    // This is a temporary drawing loop. It will be replaced by the GPU-driven pipeline.
    for (size_t i = 0; i < sceneDatabase.renderables.size(); ++i) {
        const auto& renderable = sceneDatabase.renderables[i];
        const auto& transform = sceneDatabase.transforms[i];

        // This is where the connection to the mesh is missing.
        // For now, I can't draw anything yet.
        // I will leave this empty until I have all the features.
        // The next step is to implement the asset pipeline
        // and then the opaque pass, which will fill this in.
    }

    m_shader->unbind();
}

void Renderer::setupShaders() {
    m_shader = std::make_unique<Shader>("resources/shaders/cube.vert", "resources/shaders/cube.frag");
}
