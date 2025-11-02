module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <unordered_map>
#include <cmath>

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.shader;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.shaderManager;
import Engine.Render.component;
import Engine.mesh;
import Engine.Log;

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

unsigned int nextPowerOfTwo(unsigned int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

} // namespace

Renderer::Renderer()
    : m_cullingShader(nullptr), m_transparentCullShader(nullptr), m_bitonicSortShader(nullptr),
      m_transparentCommandGenShader(nullptr), m_initialized(false), m_vboSize(0), m_eboSize(0) {}

void Renderer::init() {
    if (m_initialized)
        return;

    setupShaders();

    if (!m_cullingShader || !m_cullingShader->isInitialized()) {
        Lit::Log::Error("Renderer failed to initialize: Culling shader could not be loaded.");
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glGenBuffers(1, &m_atomicCounterBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int) * m_shaderManager.getShaderCount(), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_drawCommandBuffer);
    glGenBuffers(1, &m_objectBuffer);
    glGenBuffers(1, &m_meshInfoBuffer);
    glGenBuffers(1, &m_renderableBuffer);

    m_vboSize = 1024 * 1024 * 10;
    m_eboSize = 1024 * 1024 * 4;

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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    glGenBuffers(1, &m_visibleTransparentObjectBuffer);
    glGenBuffers(1, &m_transparentAtomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &m_transparentDrawCommandBuffer);

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

    glDeleteBuffers(1, &m_visibleTransparentObjectBuffer);
    glDeleteBuffers(1, &m_transparentAtomicCounter);
    glDeleteBuffers(1, &m_transparentDrawCommandBuffer);

    m_shaderManager.cleanup();
    m_cullingShader = nullptr;
    m_transparentCullShader = nullptr;
    m_bitonicSortShader = nullptr;
    m_transparentCommandGenShader = nullptr;
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

    const unsigned int numObjects = sceneDatabase.renderables.size();
    if (numObjects == 0) {
        glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    std::vector<unsigned int> zeros(m_numDrawingShaders, 0);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int) * m_numDrawingShaders, zeros.data());

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
    glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawElementsIndirectCommand) * numObjects * m_numDrawingShaders, nullptr, GL_DYNAMIC_DRAW);

    glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();

    m_cullingShader->bind();
    m_cullingShader->setUniform("u_objectCount", numObjects);
    m_cullingShader->setUniform("u_maxDraws", numObjects);
    m_cullingShader->setUniform("u_viewProjection", viewProjection);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_atomicCounterBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_drawCommandBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer);

    const unsigned int workgroupSize = 64;
    const unsigned int numWorkgroups = (numObjects + workgroupSize - 1) / workgroupSize;
    glDispatchCompute(numWorkgroups, 1, 1);

    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    std::vector<unsigned int> drawCounts(m_numDrawingShaders);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int) * m_numDrawingShaders, drawCounts.data());

    glm::mat4 projection = camera.getProjectionMatrix();
    glm::mat4 view = camera.getViewMatrix();

    glBindVertexArray(m_vao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer);

    for (uint32_t shaderId = 0; shaderId < m_numDrawingShaders; ++shaderId) {
        const unsigned int drawCount = drawCounts[shaderId];
        if (drawCount > 0) {
            Shader* shader = m_shaderManager.getShader(shaderId);
            if (shader) {
                shader->bind();
                shader->setUniform("projection", projection);
                shader->setUniform("view", view);
                shader->setUniform("lightPos", glm::vec3(0.0f, 10.0f, 0.0f));
                shader->setUniform("viewPos", camera.getPosition());
                shader->setUniform("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));

                const void* indirect_offset = (const void*)(uintptr_t)(shaderId * numObjects * sizeof(DrawElementsIndirectCommand));
                glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, indirect_offset, drawCount, sizeof(DrawElementsIndirectCommand));
            }
        }
    }

    unsigned int zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_visibleTransparentObjectBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(unsigned int) * 2 * numObjects, nullptr, GL_DYNAMIC_DRAW);

    m_transparentCullShader->bind();
    m_transparentCullShader->setUniform("u_objectCount", numObjects);
    m_transparentCullShader->setUniform("u_viewProjection", viewProjection);
    m_transparentCullShader->setUniform("u_cameraPos", camera.getPosition());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transparentAtomicCounter);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer);

    glDispatchCompute(numWorkgroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    unsigned int visibleTransparentCount = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &visibleTransparentCount);

    if (visibleTransparentCount > 1) {
        m_bitonicSortShader->bind();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectBuffer);

        const unsigned int numElements = nextPowerOfTwo(visibleTransparentCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {
                m_bitonicSortShader->setUniform("u_sort_k", k);
                m_bitonicSortShader->setUniform("u_sort_j", j);
                const unsigned int numWorkgroups = (numElements + 511) / 512;
                glDispatchCompute(numWorkgroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }
    }

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_transparentDrawCommandBuffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawElementsIndirectCommand) * visibleTransparentCount, nullptr, GL_DYNAMIC_DRAW);

    m_transparentCommandGenShader->bind();
    m_transparentCommandGenShader->setUniform("u_visibleTransparentCount", visibleTransparentCount);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_transparentDrawCommandBuffer);

    const unsigned int transparentWorkgroups = (visibleTransparentCount + workgroupSize - 1) / workgroupSize;
    if (visibleTransparentCount > 0) {
        glDispatchCompute(transparentWorkgroups, 1, 1);
    }
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    Shader* transparentShader = m_shaderManager.getShader(2);
    if (transparentShader && visibleTransparentCount > 0) {
        transparentShader->bind();
        transparentShader->setUniform("projection", projection);
        transparentShader->setUniform("view", view);
        transparentShader->setUniform("lightPos", glm::vec3(0.0f, 10.0f, 0.0f));
        transparentShader->setUniform("viewPos", camera.getPosition());
        transparentShader->setUniform("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_transparentDrawCommandBuffer);
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, 0, visibleTransparentCount, sizeof(DrawElementsIndirectCommand));
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
}

void Renderer::setupShaders() {
    m_shaderManager.loadShader("resources/shaders/cube.vert", "resources/shaders/cube.frag");
    m_shaderManager.loadShader("resources/shaders/cube.vert", "resources/shaders/red.frag");
    m_shaderManager.loadShader("resources/shaders/cube.vert", "resources/shaders/transparent.frag");
    m_numDrawingShaders = m_shaderManager.getShaderCount();

    const auto cullingShaderId = m_shaderManager.loadComputeShader("resources/shaders/cull.comp");
    m_cullingShader = m_shaderManager.getShader(cullingShaderId);

    const auto transparentCullId = m_shaderManager.loadComputeShader("resources/shaders/transparent_cull.comp");
    m_transparentCullShader = m_shaderManager.getShader(transparentCullId);

    const auto bitonicSortId = m_shaderManager.loadComputeShader("resources/shaders/bitonic_sort.comp");
    m_bitonicSortShader = m_shaderManager.getShader(bitonicSortId);

    const auto transparentCommandGenId = m_shaderManager.loadComputeShader("resources/shaders/transparent_command_gen.comp");
    m_transparentCommandGenShader = m_shaderManager.getShader(transparentCommandGenId);
}
