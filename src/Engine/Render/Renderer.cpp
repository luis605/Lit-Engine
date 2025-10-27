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
    float boundingRadius;

    glm::vec4 boundingCenter;
};
std::vector<MeshInfo> s_meshInfos;
size_t s_totalVertexCount = 0;
size_t s_totalIndexCount = 0;

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

    glGenBuffers(1, &m_atomicCounterBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_drawCommandBuffer);
    glGenBuffers(1, &m_objectBuffer);
    glGenBuffers(1, &m_meshInfoBuffer);
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
    glDeleteBuffers(1, &m_meshInfoBuffer);

    m_shader.reset();
    m_cullingShader.reset();
    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

void Renderer::uploadMesh(const Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    glm::vec3 center(0.0f);
    const size_t numVertices = mesh.vertices.size() / 3;
    if (numVertices > 0) {
        for (size_t i = 0; i < mesh.vertices.size(); i += 3) {
            center.x += mesh.vertices[i];
            center.y += mesh.vertices[i + 1];
            center.z += mesh.vertices[i + 2];
        }
        center /= static_cast<float>(numVertices);
    }

    float maxRadiusSq = 0.0f;
    for (size_t i = 0; i < mesh.vertices.size(); i += 3) {
        const glm::vec3 vertex(mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]);
        const float distSq = glm::distance(center, vertex);
        if (distSq > maxRadiusSq) {
            maxRadiusSq = distSq;
        }
    }
    const float radius = glm::sqrt(maxRadiusSq);

    s_meshInfos.push_back({.indexCount = static_cast<unsigned int>(mesh.indices.size()),
                           .firstIndex = static_cast<unsigned int>(s_totalIndexCount),
                           .baseVertex = static_cast<unsigned int>(s_totalVertexCount) / 3,
                           .boundingRadius = radius,
                           .boundingCenter = glm::vec4(center, 1.0f)});

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
    modelMatrices.reserve(sceneDatabase.transforms.size());
    for (const auto& transform : sceneDatabase.transforms) {
        modelMatrices.push_back(transform.localMatrix);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_objectBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, modelMatrices.size() * sizeof(glm::mat4),
                 modelMatrices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshInfoBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, s_meshInfos.size() * sizeof(MeshInfo),
                 s_meshInfos.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                 sizeof(DrawElementsIndirectCommand) * sceneDatabase.renderables.size(), nullptr,
                 GL_DYNAMIC_DRAW);

    glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();

    m_cullingShader->bind();
    m_cullingShader->setUniform("u_objectCount",
                                static_cast<unsigned int>(sceneDatabase.renderables.size()));
    m_cullingShader->setUniform("u_viewProjection", viewProjection);

    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, m_atomicCounterBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_drawCommandBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);

    const unsigned int numObjects = sceneDatabase.renderables.size();
    const unsigned int workgroupSize = 64;
    const unsigned int numWorkgroups = (numObjects + workgroupSize - 1) / workgroupSize;
    glDispatchCompute(numWorkgroups, 1, 1);

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

    glBindBuffer(GL_PARAMETER_BUFFER, m_atomicCounterBuffer);
    glMultiDrawElementsIndirectCount(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, 0,
                                     sceneDatabase.renderables.size(), 0);
    glBindVertexArray(0);
}

void Renderer::setupShaders() {
    m_shader =
        std::make_unique<Shader>("resources/shaders/cube.vert", "resources/shaders/cube.frag");
    m_cullingShader = std::make_unique<Shader>("resources/shaders/cull.comp");
}