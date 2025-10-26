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

struct Renderer::GPUMesh {
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    unsigned int indexCount = 0;

    void release() {
        if (vao != 0) {
            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &vbo);
            glDeleteBuffers(1, &ebo);
            vao = 0;
            vbo = 0;
            ebo = 0;
            indexCount = 0;
        }
    }
};

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

    for (auto& mesh : m_gpuMeshes) {
        mesh.release();
    }
    m_gpuMeshes.clear();

    m_shader.reset();
    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

void Renderer::uploadMesh(const Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    GPUMesh gpuMesh;
    gpuMesh.indexCount = mesh.indices.size();

    glGenVertexArrays(1, &gpuMesh.vao);
    glBindVertexArray(gpuMesh.vao);

    glGenBuffers(1, &gpuMesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gpuMesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(float), mesh.vertices.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &gpuMesh.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpuMesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                 mesh.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindVertexArray(0);

    m_gpuMeshes.push_back(std::move(gpuMesh));
}

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

    if (!m_gpuMeshes.empty()) {
        // This is a temporary drawing loop. It will be replaced by the GPU-driven pipeline.
        for (size_t i = 0; i < sceneDatabase.renderables.size(); ++i) {
            const auto& transform = sceneDatabase.transforms[i];
            m_shader->setUniform("model", transform.localMatrix);

            const auto& gpuMesh = m_gpuMeshes[0];
            glBindVertexArray(gpuMesh.vao);
            glDrawElements(GL_TRIANGLES, gpuMesh.indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }

    m_shader->unbind();
}

void Renderer::setupShaders() {
    m_shader =
        std::make_unique<Shader>("resources/shaders/cube.vert", "resources/shaders/cube.frag");
}