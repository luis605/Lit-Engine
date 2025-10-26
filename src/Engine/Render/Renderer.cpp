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

namespace {
struct DrawElementsIndirectCommand {
    unsigned int count;
    unsigned int instanceCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    unsigned int baseInstance;
};

struct MeshInfo {
    unsigned int indexCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
};
std::vector<MeshInfo> s_meshInfos;
size_t s_totalVertexCount = 0;
size_t s_totalIndexCount = 0;

const int MAX_DRAW_COMMANDS = 1024;
} // namespace

Renderer::Renderer() : m_shader(nullptr), m_cullingShader(nullptr), m_initialized(false) {}

void Renderer::init() {
    if (m_initialized)
        return;

    setupShaders();

    if (!m_shader || !m_shader->isInitialized() || !m_cullingShader ||
        !m_cullingShader->isInitialized()) {
        std::cerr << "Renderer failed to initialize: Shaders could not be loaded." << std::endl;
        return;
    }

    glEnable(GL_DEPTH_TEST);

    glGenBuffers(1, &m_drawCommandBuffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawElementsIndirectCommand) * MAX_DRAW_COMMANDS,
                 nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_objectBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_objectBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(glm::mat4) * MAX_DRAW_COMMANDS, nullptr,
                 GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_atomicCounterBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    m_initialized = true;
}

void Renderer::cleanup() {
    if (!m_initialized)
        return;

    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_drawCommandBuffer);
    glDeleteBuffers(1, &m_objectBuffer);
    glDeleteBuffers(1, &m_atomicCounterBuffer);

    m_shader.reset();
    m_cullingShader.reset();
    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

void Renderer::uploadMesh(const Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    s_meshInfos.push_back({.indexCount = static_cast<unsigned int>(mesh.indices.size()),
                           .firstIndex = static_cast<unsigned int>(s_totalIndexCount),
                           .baseVertex = static_cast<unsigned int>(s_totalVertexCount) / 3});

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (s_totalVertexCount + mesh.vertices.size()) * sizeof(float),
                 nullptr, GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, s_totalVertexCount * sizeof(float),
                    mesh.vertices.size() * sizeof(float), mesh.vertices.data());

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (s_totalIndexCount + mesh.indices.size()) * sizeof(unsigned int), nullptr,
                 GL_STATIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, s_totalIndexCount * sizeof(unsigned int),
                    mesh.indices.size() * sizeof(unsigned int), mesh.indices.data());

    s_totalVertexCount += mesh.vertices.size();
    s_totalIndexCount += mesh.indices.size();
}

void Renderer::drawScene(const SceneDatabase& sceneDatabase, const Camera& camera) {
    if (!m_initialized || s_meshInfos.empty())
        return;

    unsigned int zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    std::vector<glm::mat4> modelMatrices;
    for (const auto& transform : sceneDatabase.transforms) {
        modelMatrices.push_back(transform.localMatrix);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_objectBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, modelMatrices.size() * sizeof(glm::mat4),
                    modelMatrices.data());

    m_cullingShader->bind();

    m_cullingShader->setUniform("u_indexCount", s_meshInfos[0].indexCount);
    m_cullingShader->setUniform("u_baseVertex", s_meshInfos[0].baseVertex);
    m_cullingShader->setUniform("u_firstIndex", s_meshInfos[0].firstIndex);

    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, m_atomicCounterBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_drawCommandBuffer);

    glDispatchCompute(1, 1, 1);

    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader->bind();
    glm::mat4 projection = camera.getProjectionMatrix();
    glm::mat4 view = camera.getViewMatrix();
    m_shader->setUniform("projection", projection);
    m_shader->setUniform("view", view);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, 1, 0);
    glBindVertexArray(0);
}

void Renderer::setupShaders() {
    m_shader =
        std::make_unique<Shader>("resources/shaders/cube.vert", "resources/shaders/cube.frag");
    m_cullingShader = std::make_unique<Shader>("resources/shaders/cull.comp");
}