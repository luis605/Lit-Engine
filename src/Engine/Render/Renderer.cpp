module;

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <unordered_map>
#include <cmath>
#include <string>
#include <cstring>
#include <cstddef>
#include "Engine/Log/Log.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.shader;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.shaderManager;
import Engine.Render.component;
import Engine.mesh;

namespace {
struct DrawElementsIndirectCommand {
    unsigned int count;
    unsigned int instanceCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    unsigned int baseInstance;
};

static_assert(offsetof(DrawElementsIndirectCommand, count) == 0, "Offset mismatch for count");
static_assert(offsetof(DrawElementsIndirectCommand, instanceCount) == 4, "Offset mismatch for instanceCount");
static_assert(offsetof(DrawElementsIndirectCommand, firstIndex) == 8, "Offset mismatch for firstIndex");
static_assert(offsetof(DrawElementsIndirectCommand, baseVertex) == 12, "Offset mismatch for baseVertex");
static_assert(offsetof(DrawElementsIndirectCommand, baseInstance) == 16, "Offset mismatch for baseInstance");
static_assert(sizeof(DrawElementsIndirectCommand) == 20, "Size mismatch for DrawElementsIndirectCommand");

struct MeshInfo {
    unsigned int indexCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    float boundingRadius;
    alignas(16) glm::vec4 boundingCenter;
};

static_assert(offsetof(MeshInfo, indexCount) == 0, "Offset mismatch for indexCount");
static_assert(offsetof(MeshInfo, firstIndex) == 4, "Offset mismatch for firstIndex");
static_assert(offsetof(MeshInfo, baseVertex) == 8, "Offset mismatch for baseVertex");
static_assert(offsetof(MeshInfo, boundingRadius) == 12, "Offset mismatch for boundingRadius");
static_assert(offsetof(MeshInfo, boundingCenter) == 16, "Offset mismatch for boundingCenter");
static_assert(sizeof(MeshInfo) == 32, "Size mismatch for MeshInfo");

struct SceneUniforms {
    glm::mat4 projection;
    glm::mat4 view;
    alignas(16) glm::vec3 lightPos;
    alignas(16) glm::vec3 viewPos;
    alignas(16) glm::vec3 lightColor;
    alignas(16) glm::vec4 frustumPlanes[6];
};

static_assert(offsetof(SceneUniforms, projection) == 0, "Offset mismatch for projection");
static_assert(offsetof(SceneUniforms, view) == 64, "Offset mismatch for view");
static_assert(offsetof(SceneUniforms, lightPos) == 128, "Offset mismatch for lightPos");
static_assert(offsetof(SceneUniforms, viewPos) == 144, "Offset mismatch for viewPos");
static_assert(offsetof(SceneUniforms, lightColor) == 160, "Offset mismatch for lightColor");
static_assert(offsetof(SceneUniforms, frustumPlanes) == 176, "Offset mismatch for frustumPlanes");
static_assert(sizeof(SceneUniforms) == 272, "Size mismatch for SceneUniforms");

struct VisibleTransparentObject {
    unsigned int objectId;
    float distance;
};

static_assert(offsetof(VisibleTransparentObject, objectId) == 0, "Offset mismatch for objectId");
static_assert(offsetof(VisibleTransparentObject, distance) == 4, "Offset mismatch for distance");
static_assert(sizeof(VisibleTransparentObject) == 8, "Size mismatch for VisibleTransparentObject");

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

void extractFrustumPlanes(const glm::mat4& vp, glm::vec4* planes) {

    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);

    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);

    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);

    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);

    planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);

    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    for (int i = 0; i < 6; i++) {
        planes[i] = glm::normalize(planes[i]);
    }
}

} // namespace

Renderer::Renderer()
    : m_cullingShader(nullptr), m_transformShader(nullptr), m_transparentCullShader(nullptr), m_bitonicSortShader(nullptr),
      m_transparentCommandGenShader(nullptr), m_initialized(false), m_vboSize(0), m_eboSize(0),
      m_numDrawingShaders(0) {}

void Renderer::init(const int windowWidth, const int windowHeight) {
    if (m_initialized)
        return;

    m_uiManager = new UIManager();
    m_uiManager->init(windowWidth, windowHeight);

    setupShaders();

    if (!m_cullingShader || !m_cullingShader->isInitialized()) {
        Lit::Log::Error("Renderer failed to initialize: Culling shader could not be loaded.");
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    m_maxObjects = 2000000;
    reallocateBuffers(m_maxObjects);

    glGenBuffers(1, &m_atomicCounterBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int) * m_shaderManager.getShaderCount(), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_meshInfoBuffer);

    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCullStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCullEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentSortStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentSortEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCommandGenStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCommandGenEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueDrawStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueDrawEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentDrawStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentDrawEnd);

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

    glGenBuffers(1, &m_transparentAtomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

    m_initialized = true;
}

void Renderer::reallocateBuffers(size_t numObjects) {
    m_maxObjects = numObjects;

    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT;

    auto reallocate = [&](GLuint& buffer, size_t& currentSize, void*& mappedPtr, size_t objectSize) {
        if (mappedPtr) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        }
        glDeleteBuffers(1, &buffer);
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        currentSize = m_maxObjects * objectSize * NUM_FRAMES_IN_FLIGHT;
        glBufferStorage(GL_SHADER_STORAGE_BUFFER, currentSize, nullptr, flags);
        mappedPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, currentSize, flags | GL_MAP_FLUSH_EXPLICIT_BIT);
    };

    reallocate(m_objectBuffer, m_objectBufferSize, m_objectBufferPtr, sizeof(TransformComponent));
    reallocate(m_hierarchyBuffer, m_hierarchyBufferSize, m_hierarchyBufferPtr, sizeof(HierarchyComponent));
    reallocate(m_renderableBuffer, m_renderableBufferSize, m_renderableBufferPtr, sizeof(RenderableComponent));
    reallocate(m_sortedHierarchyBuffer, m_sortedHierarchyBufferSize, m_sortedHierarchyBufferPtr, sizeof(unsigned int));
    reallocate(m_drawCommandBuffer, m_drawCommandBufferSize, m_drawCommandBufferPtr, sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders);
    reallocate(m_visibleTransparentObjectBuffer, m_visibleTransparentObjectBufferSize, m_visibleTransparentObjectBufferPtr, sizeof(VisibleTransparentObject));
    reallocate(m_transparentDrawCommandBuffer, m_transparentDrawCommandBufferSize, m_transparentDrawCommandBufferPtr, sizeof(DrawElementsIndirectCommand));

    if (m_sceneUBOPtr)
        glUnmapBuffer(GL_UNIFORM_BUFFER);
    glDeleteBuffers(1, &m_sceneUBO);
    glGenBuffers(1, &m_sceneUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_sceneUBO);
    m_sceneUBOSize = sizeof(SceneUniforms) * NUM_FRAMES_IN_FLIGHT;
    glBufferStorage(GL_UNIFORM_BUFFER, m_sceneUBOSize, nullptr, flags);
    m_sceneUBOPtr = glMapBufferRange(GL_UNIFORM_BUFFER, 0, m_sceneUBOSize, flags | GL_MAP_FLUSH_EXPLICIT_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_sceneUBO);
}

void Renderer::cleanup() {
    if (!m_initialized)
        return;

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_fences[i]) {
            glDeleteSync(m_fences[i]);
        }
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_objectBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_hierarchyBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_renderableBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_sortedHierarchyBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_drawCommandBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_visibleTransparentObjectBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_transparentDrawCommandBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

    glBindBuffer(GL_UNIFORM_BUFFER, m_sceneUBO);
    glUnmapBuffer(GL_UNIFORM_BUFFER);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_drawCommandBuffer);
    glDeleteBuffers(1, &m_objectBuffer);
    glDeleteBuffers(1, &m_hierarchyBuffer);
    glDeleteBuffers(1, &m_atomicCounterBuffer);
    glDeleteBuffers(1, &m_meshInfoBuffer);
    glDeleteBuffers(1, &m_renderableBuffer);
    glDeleteBuffers(1, &m_sortedHierarchyBuffer);
    glDeleteBuffers(1, &m_visibleTransparentObjectBuffer);
    glDeleteBuffers(1, &m_transparentAtomicCounter);
    glDeleteBuffers(1, &m_transparentDrawCommandBuffer);
    glDeleteBuffers(1, &m_sceneUBO);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCullStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCullEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentSortStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentSortEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCommandGenStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentCommandGenEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueDrawStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueDrawEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentDrawStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransparentDrawEnd);

    m_shaderManager.cleanup();
    m_cullingShader = nullptr;
    m_transformShader = nullptr;
    m_transparentCullShader = nullptr;
    m_bitonicSortShader = nullptr;
    m_transparentCommandGenShader = nullptr;

    m_shaderManager.cleanup();
    delete m_uiManager;
    m_uiManager = nullptr;

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
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBindVertexArray(0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, s_totalVertexSize, vertexDataSize, mesh.vertices.data());

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, s_totalIndexSize, indexDataSize, mesh.indices.data());

    glm::vec3 center(0.0f);
    const size_t numVertices = mesh.vertices.size() / 6;
    if (numVertices > 0) {
        for (size_t i = 0; i < mesh.vertices.size(); i += 6) {
            center.x += mesh.vertices[i];
            center.y += mesh.vertices[i + 1];
            center.z += mesh.vertices[i + 2];
        }
        center /= static_cast<float>(numVertices);
    }

    float maxRadiusSq = 0.0f;
    for (size_t i = 0; i < mesh.vertices.size(); i += 6) {
        const glm::vec3 vertex(mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]);
        const float distSq = glm::distance2(center, vertex);
        if (distSq > maxRadiusSq) {
            maxRadiusSq = distSq;
        }
    }
    const float radius = glm::sqrt(maxRadiusSq);

    s_meshInfos.push_back({.indexCount = static_cast<unsigned int>(mesh.indices.size()),
                           .firstIndex = static_cast<unsigned int>(s_totalIndexSize / sizeof(unsigned int)),
                           .baseVertex = static_cast<unsigned int>(s_totalVertexSize / (6 * sizeof(float))),
                           .boundingRadius = radius,
                           .boundingCenter = glm::vec4(center, 1.0f)});

    s_totalVertexSize += vertexDataSize;
    s_totalIndexSize += indexDataSize;
}

void Renderer::drawScene(SceneDatabase& sceneDatabase, const Camera& camera) {
    const unsigned int numObjects = sceneDatabase.renderables.size();
    if (numObjects > m_maxObjects) {
        for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
            if (m_fences[i]) {
                glClientWaitSync(m_fences[i], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
                glDeleteSync(m_fences[i]);
                m_fences[i] = nullptr;
            }
        }
        reallocateBuffers(numObjects * 1.5);
    }

    m_currentFrame = (m_currentFrame + 1) % NUM_FRAMES_IN_FLIGHT;
    const int previousFrame = (m_currentFrame + NUM_FRAMES_IN_FLIGHT - 1) % NUM_FRAMES_IN_FLIGHT;

    if (m_fences[m_currentFrame]) {
        GLenum waitResult = glClientWaitSync(m_fences[m_currentFrame], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
        if (waitResult == GL_TIMEOUT_EXPIRED) {
            Lit::Log::Warn("Timeout expired while waiting for fence.");
        } else if (waitResult == GL_WAIT_FAILED) {
            Lit::Log::Error("Failed to wait for fence.");
        }
        glDeleteSync(m_fences[m_currentFrame]);
    }

    long startTime, endTime;
    long transformStartTime, transformEndTime;
    long cullStartTime, cullEndTime;
    long opaqueDrawStartTime, opaqueDrawEndTime;
    long transparentCullStartTime, transparentCullEndTime;
    long transparentSortStartTime, transparentSortEndTime;
    long transparentCommandGenStartTime, transparentCommandGenEndTime;
    long transparentDrawStartTime, transparentDrawEndTime;

    int done = 0;
    glGetQueryObjectiv(m_queryEnd[previousFrame], GL_QUERY_RESULT_AVAILABLE, &done);
    if (done) {
        glGetQueryObjecti64v(m_queryStart[previousFrame], GL_QUERY_RESULT, &startTime);
        glGetQueryObjecti64v(m_queryEnd[previousFrame], GL_QUERY_RESULT, &endTime);
        glGetQueryObjecti64v(m_queryTransformStart[previousFrame], GL_QUERY_RESULT, &transformStartTime);
        glGetQueryObjecti64v(m_queryTransformEnd[previousFrame], GL_QUERY_RESULT, &transformEndTime);
        glGetQueryObjecti64v(m_queryCullStart[previousFrame], GL_QUERY_RESULT, &cullStartTime);
        glGetQueryObjecti64v(m_queryCullEnd[previousFrame], GL_QUERY_RESULT, &cullEndTime);
        glGetQueryObjecti64v(m_queryOpaqueDrawStart[previousFrame], GL_QUERY_RESULT, &opaqueDrawStartTime);
        glGetQueryObjecti64v(m_queryOpaqueDrawEnd[previousFrame], GL_QUERY_RESULT, &opaqueDrawEndTime);
        glGetQueryObjecti64v(m_queryTransparentCullStart[previousFrame], GL_QUERY_RESULT, &transparentCullStartTime);
        glGetQueryObjecti64v(m_queryTransparentCullEnd[previousFrame], GL_QUERY_RESULT, &transparentCullEndTime);
        glGetQueryObjecti64v(m_queryTransparentSortStart[previousFrame], GL_QUERY_RESULT, &transparentSortStartTime);
        glGetQueryObjecti64v(m_queryTransparentSortEnd[previousFrame], GL_QUERY_RESULT, &transparentSortEndTime);
        glGetQueryObjecti64v(m_queryTransparentCommandGenStart[previousFrame], GL_QUERY_RESULT, &transparentCommandGenStartTime);
        glGetQueryObjecti64v(m_queryTransparentCommandGenEnd[previousFrame], GL_QUERY_RESULT, &transparentCommandGenEndTime);
        glGetQueryObjecti64v(m_queryTransparentDrawStart[previousFrame], GL_QUERY_RESULT, &transparentDrawStartTime);
        glGetQueryObjecti64v(m_queryTransparentDrawEnd[previousFrame], GL_QUERY_RESULT, &transparentDrawEndTime);

        constexpr bool debugMode = true;
        if (debugMode) {
            Lit::Log::Debug("GPU Frame Time: {} ms", (endTime - startTime) / 1000000.0);
            Lit::Log::Debug("  Transform: {} ms", (transformEndTime - transformStartTime) / 1000000.0);
            Lit::Log::Debug("  Cull: {} ms", (cullEndTime - cullStartTime) / 1000000.0);
            Lit::Log::Debug("  Opaque Draw: {} ms", (opaqueDrawEndTime - opaqueDrawStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Cull: {} ms", (transparentCullEndTime - transparentCullStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Sort: {} ms", (transparentSortEndTime - transparentSortStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Command Gen: {} ms", (transparentCommandGenEndTime - transparentCommandGenStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Draw: {} ms", (transparentDrawEndTime - transparentDrawStartTime) / 1000000.0);
        }
    }

    glQueryCounter(m_queryStart[m_currentFrame], GL_TIMESTAMP);
    if (sceneDatabase.m_isHierarchyDirty) {
        sceneDatabase.updateHierarchy();
    }

    if (!m_initialized || s_meshInfos.empty()) {
        return;
    }

    if (numObjects == 0) {
        glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    const size_t frameOffset = m_currentFrame * m_maxObjects;
    const size_t uboFrameOffset = m_currentFrame * sizeof(SceneUniforms);

    auto updateBuffer = [&](GLuint buffer, void* ptr, size_t bufferSize, const auto& data, size_t objectSize) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        const size_t dataSize = data.size() * objectSize;
        const size_t frameOffsetBytes = m_currentFrame * (bufferSize / NUM_FRAMES_IN_FLIGHT);
        memcpy(static_cast<char*>(ptr) + frameOffsetBytes, data.data(), dataSize);
        glFlushMappedBufferRange(GL_SHADER_STORAGE_BUFFER, frameOffsetBytes, dataSize);
    };

    glQueryCounter(m_queryTransformStart[m_currentFrame], GL_TIMESTAMP);
    m_transformShader->bind();
    m_transformShader->setUniform("u_objectCount", (unsigned int)sceneDatabase.sortedHierarchyList.size());

    updateBuffer(m_objectBuffer, m_objectBufferPtr, m_objectBufferSize, sceneDatabase.transforms, sizeof(TransformComponent));
    updateBuffer(m_hierarchyBuffer, m_hierarchyBufferPtr, m_hierarchyBufferSize, sceneDatabase.hierarchies, sizeof(HierarchyComponent));
    updateBuffer(m_sortedHierarchyBuffer, m_sortedHierarchyBufferPtr, m_sortedHierarchyBufferSize, sceneDatabase.sortedHierarchyList, sizeof(unsigned int));

    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_hierarchyBuffer, frameOffset * sizeof(HierarchyComponent), m_maxObjects * sizeof(HierarchyComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_sortedHierarchyBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));

    const unsigned int transformWorkgroupSize = 64;
    const unsigned int transformNumWorkgroups = (sceneDatabase.sortedHierarchyList.size() + transformWorkgroupSize - 1) / transformWorkgroupSize;

    for (uint32_t level = 0; level <= sceneDatabase.m_maxHierarchyDepth; ++level) {
        m_transformShader->setUniform("u_currentHierarchyLevel", level);
        glDispatchCompute(transformNumWorkgroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    glQueryCounter(m_queryTransformEnd[m_currentFrame], GL_TIMESTAMP);

    updateBuffer(m_renderableBuffer, m_renderableBufferPtr, m_renderableBufferSize, sceneDatabase.renderables, sizeof(RenderableComponent));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshInfoBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, s_meshInfos.size() * sizeof(MeshInfo), s_meshInfos.data(), GL_STATIC_DRAW);

    glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();

    glQueryCounter(m_queryCullStart[m_currentFrame], GL_TIMESTAMP);

    std::vector<unsigned int> zeros(m_numDrawingShaders, 0);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int) * m_numDrawingShaders, zeros.data());

    m_cullingShader->bind();
    m_cullingShader->setUniform("u_objectCount", numObjects);
    m_cullingShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_atomicCounterBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_drawCommandBuffer, frameOffset * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders, m_maxObjects * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

    const unsigned int workgroupSize = 64;
    const unsigned int numWorkgroups = (numObjects + workgroupSize - 1) / workgroupSize;
    glDispatchCompute(numWorkgroups, 1, 1);

    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    std::vector<unsigned int> drawCounts(m_numDrawingShaders);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounterBuffer);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int) * m_numDrawingShaders, drawCounts.data());

    glQueryCounter(m_queryCullEnd[m_currentFrame], GL_TIMESTAMP);

    SceneUniforms sceneUniforms;
    sceneUniforms.projection = camera.getProjectionMatrix();
    sceneUniforms.view = camera.getViewMatrix();
    sceneUniforms.lightPos = glm::vec3(0.0f, 10.0f, 0.0f);
    sceneUniforms.viewPos = camera.getPosition();
    sceneUniforms.lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
    extractFrustumPlanes(sceneUniforms.projection * sceneUniforms.view, sceneUniforms.frustumPlanes);

    memcpy(static_cast<char*>(m_sceneUBOPtr) + uboFrameOffset, &sceneUniforms, sizeof(SceneUniforms));
    glBindBuffer(GL_UNIFORM_BUFFER, m_sceneUBO);
    glFlushMappedBufferRange(GL_UNIFORM_BUFFER, uboFrameOffset, sizeof(SceneUniforms));
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_sceneUBO, uboFrameOffset, sizeof(SceneUniforms));

    glBindVertexArray(m_vao);

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));

    glQueryCounter(m_queryOpaqueDrawStart[m_currentFrame], GL_TIMESTAMP);
    for (uint32_t shaderId = 0; shaderId < m_numDrawingShaders; ++shaderId) {
        const unsigned int drawCount = drawCounts[shaderId];
        if (drawCount > 0) {
            Shader* shader = m_shaderManager.getShader(shaderId);
            if (shader) {
                shader->bind();
                const size_t indirect_offset = (m_currentFrame * m_numDrawingShaders * m_maxObjects + shaderId * m_maxObjects) * sizeof(DrawElementsIndirectCommand);
                glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)indirect_offset, drawCount, sizeof(DrawElementsIndirectCommand));
            }
        }
    }
    glQueryCounter(m_queryOpaqueDrawEnd[m_currentFrame], GL_TIMESTAMP);

    unsigned int zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    glQueryCounter(m_queryTransparentCullStart[m_currentFrame], GL_TIMESTAMP);
    m_transparentCullShader->bind();
    m_transparentCullShader->setUniform("u_objectCount", numObjects);
    m_transparentCullShader->setUniform("u_cameraPos", camera.getPosition());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transparentAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

    glDispatchCompute(numWorkgroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
    glQueryCounter(m_queryTransparentCullEnd[m_currentFrame], GL_TIMESTAMP);

    unsigned int visibleTransparentCount = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &visibleTransparentCount);

    if (visibleTransparentCount > 1) {
        glQueryCounter(m_queryTransparentSortStart[m_currentFrame], GL_TIMESTAMP);
        m_bitonicSortShader->bind();
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));

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
        glQueryCounter(m_queryTransparentSortEnd[m_currentFrame], GL_TIMESTAMP);
    }

    glQueryCounter(m_queryTransparentCommandGenStart[m_currentFrame], GL_TIMESTAMP);
    m_transparentCommandGenShader->bind();
    m_transparentCommandGenShader->setUniform("u_visibleTransparentCount", visibleTransparentCount);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_transparentDrawCommandBuffer, frameOffset * sizeof(DrawElementsIndirectCommand), m_maxObjects * sizeof(DrawElementsIndirectCommand));

    const unsigned int transparentWorkgroups = (visibleTransparentCount + workgroupSize - 1) / workgroupSize;
    if (visibleTransparentCount > 0) {
        glDispatchCompute(transparentWorkgroups, 1, 1);
    }
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    glQueryCounter(m_queryTransparentCommandGenEnd[m_currentFrame], GL_TIMESTAMP);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glQueryCounter(m_queryTransparentDrawStart[m_currentFrame], GL_TIMESTAMP);
    Shader* transparentShader = m_shaderManager.getShader(2);
    if (transparentShader && visibleTransparentCount > 0) {
        transparentShader->bind();
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_transparentDrawCommandBuffer);
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)(frameOffset * sizeof(DrawElementsIndirectCommand)), visibleTransparentCount, sizeof(DrawElementsIndirectCommand));
    }
    glQueryCounter(m_queryTransparentDrawEnd[m_currentFrame], GL_TIMESTAMP);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(0);

    m_uiManager->render();

    glQueryCounter(m_queryEnd[m_currentFrame], GL_TIMESTAMP);

    m_fences[m_currentFrame] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}
void Renderer::setupShaders() {
    m_shaderManager.loadShader("resources/shaders/cube.vert", "resources/shaders/cube.frag");
    m_shaderManager.loadShader("resources/shaders/cube.vert", "resources/shaders/red.frag");
    m_shaderManager.loadShader("resources/shaders/cube.vert", "resources/shaders/transparent.frag");
    m_numDrawingShaders = m_shaderManager.getShaderCount();

    const auto cullingShaderId = m_shaderManager.loadComputeShader("resources/shaders/cull.comp");
    m_cullingShader = m_shaderManager.getShader(cullingShaderId);

    const auto transformShaderId = m_shaderManager.loadComputeShader("resources/shaders/transform.comp");
    m_transformShader = m_shaderManager.getShader(transformShaderId);

    const auto transparentCullId = m_shaderManager.loadComputeShader("resources/shaders/transparent_cull.comp");
    m_transparentCullShader = m_shaderManager.getShader(transparentCullId);

    const auto bitonicSortId = m_shaderManager.loadComputeShader("resources/shaders/bitonic_sort.comp");
    m_bitonicSortShader = m_shaderManager.getShader(bitonicSortId);

    const auto transparentCommandGenId = m_shaderManager.loadComputeShader("resources/shaders/transparent_command_gen.comp");
    m_transparentCommandGenShader = m_shaderManager.getShader(transparentCommandGenId);
}

void Renderer::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_uiManager->addText(text, x, y, scale, color);
}
