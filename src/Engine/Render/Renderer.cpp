module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.shader;
import Engine.Render.entity;
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
size_t s_totalVertexSize = 0;
size_t s_totalIndexSize = 0;

} // namespace

Renderer::Renderer() : m_shader(nullptr), m_cullingShader(nullptr), m_initialized(false), m_vboSize(0), m_eboSize(0) {}

void Renderer::init() {
    if (m_initialized)
        return;

    setupShaders();

    if (!m_shader || !m_shader->isInitialized() || !m_cullingShader || !m_cullingShader->isInitialized()) {
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
    glGenBuffers(1, &m_renderableBuffer);

    // Pre-allocate buffers
    m_vboSize = 1024 * 1024 * 10; // 10 MB
    m_eboSize = 1024 * 1024 * 4;  // 4 MB

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_vboSize, nullptr, GL_STATIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_eboSize, nullptr, GL_STATIC_DRAW);

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
    glDeleteBuffers(1, &m_renderableBuffer);

    m_shader.reset();
    m_cullingShader.reset();
    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

void Renderer::uploadMesh(const Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    const size_t vertexDataSize = mesh.vertices.size() * sizeof(float);
    const size_t indexDataSize = mesh.indices.size() * sizeof(unsigned int);

    if (s_totalVertexSize + vertexDataSize > m_vboSize || s_totalIndexSize + indexDataSize > m_eboSize) {
        // Resize buffers
        m_vboSize = std::max(m_vboSize * 2, s_totalVertexSize + vertexDataSize);
        m_eboSize = std::max(m_eboSize * 2, s_totalIndexSize + indexDataSize);

        GLuint new_vbo, new_ebo;
        glGenBuffers(1, &new_vbo);
        glGenBuffers(1, &new_ebo);

        glBindBuffer(GL_ARRAY_BUFFER, new_vbo);
        glBufferData(GL_ARRAY_BUFFER, m_vboSize, nullptr, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, new_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_eboSize, nullptr, GL_STATIC_DRAW);

        glBindBuffer(GL_COPY_READ_BUFFER, m_vbo);
        glBindBuffer(GL_COPY_WRITE_BUFFER, new_vbo);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, s_totalVertexSize);

        glBindBuffer(GL_COPY_READ_BUFFER, m_ebo);
        glBindBuffer(GL_COPY_WRITE_BUFFER, new_ebo);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, s_totalIndexSize);

        glDeleteBuffers(1, &m_vbo);
        glDeleteBuffers(1, &m_ebo);

        m_vbo = new_vbo;
        m_ebo = new_ebo;

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, s_totalVertexSize, vertexDataSize, mesh.vertices.data());

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, s_totalIndexSize, indexDataSize, mesh.indices.data());

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
                           .firstIndex = static_cast<unsigned int>(s_totalIndexSize / sizeof(unsigned int)),
                           .baseVertex = static_cast<unsigned int>(s_totalVertexSize / (3 * sizeof(float))),
                           .boundingRadius = radius,
                           .boundingCenter = glm::vec4(center, 1.0f)});

    s_totalVertexSize += vertexDataSize;
    s_totalIndexSize += indexDataSize;
}

void Renderer::drawScene(SceneDatabase& sceneDatabase, const Camera& camera) {
    if (!m_initialized || s_meshInfos.empty()) {
        return;
    }

    unsigned int zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    std::vector<glm::mat4> modelMatrices;
    modelMatrices.resize(sceneDatabase.transforms.size());

    for (const auto& entity : sceneDatabase.sortedHierarchyList) {
        auto& transform = sceneDatabase.transforms[entity];
        const auto& hierarchy = sceneDatabase.hierarchies[entity];

        if (hierarchy.parent != INVALID_ENTITY) {
            transform.worldMatrix = sceneDatabase.transforms[hierarchy.parent].worldMatrix * transform.localMatrix;
        } else {
            transform.worldMatrix = transform.localMatrix;
        }
        modelMatrices[entity] = transform.worldMatrix;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_objectBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, modelMatrices.size() * sizeof(glm::mat4), modelMatrices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_renderableBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sceneDatabase.renderables.size() * sizeof(RenderableComponent), sceneDatabase.renderables.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshInfoBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, s_meshInfos.size() * sizeof(MeshInfo), s_meshInfos.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawElementsIndirectCommand) * sceneDatabase.renderables.size(), nullptr, GL_DYNAMIC_DRAW);

    glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();

    m_cullingShader->bind();
    m_cullingShader->setUniform("u_objectCount", static_cast<unsigned int>(sceneDatabase.renderables.size()));
    m_cullingShader->setUniform("u_viewProjection", viewProjection);

    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, m_atomicCounterBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_drawCommandBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer);

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
    glMultiDrawElementsIndirectCount(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)0, 0, sceneDatabase.renderables.size(), 0);
    glBindVertexArray(0);
}

void Renderer::setupShaders() {
    m_shader = std::make_unique<Shader>("resources/shaders/cube.vert", "resources/shaders/cube.frag");
    m_cullingShader = std::make_unique<Shader>("resources/shaders/cull.comp");
}