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
      m_opaqueSortShader(nullptr), m_transparentCommandGenShader(nullptr), m_commandGenShader(nullptr), m_largeObjectCullShader(nullptr),
      m_largeObjectSortShader(nullptr), m_largeObjectCommandGenShader(nullptr), m_depthPrepassShader(nullptr),
      m_debugDepthShader(nullptr), m_hizMipmapShader(nullptr), m_initialized(false), m_vboSize(0), m_eboSize(0), m_numDrawingShaders(0) {}

void Renderer::init(const int windowWidth, const int windowHeight) {
    if (m_initialized)
        return;

    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

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

    glGenBuffers(1, &m_visibleObjectAtomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleObjectAtomicCounter);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_drawAtomicCounterBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_drawAtomicCounterBuffer);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int) * m_shaderManager.getShaderCount(), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_meshInfoBuffer);

    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueSortStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueSortEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryCommandGenStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryCommandGenEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectSortStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectSortEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectCommandGenStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectCommandGenEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryDepthPrePassStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryDepthPrePassEnd);
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
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryHizMipmapStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryHizMipmapEnd);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryUiStart);
    glGenQueries(NUM_FRAMES_IN_FLIGHT, m_queryUiEnd);

    glGenBuffers(1, &m_visibleLargeObjectAtomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleLargeObjectAtomicCounter);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

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

    m_maxMipLevel = static_cast<int>(std::floor(std::log2(std::max(windowWidth, windowHeight))));
    glGenTextures(1, &m_hizTexture);
    glBindTexture(GL_TEXTURE_2D, m_hizTexture);

    glTexStorage2D(GL_TEXTURE_2D, m_maxMipLevel, GL_R32F, windowWidth, windowHeight);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_depthFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthFbo);
    glGenRenderbuffers(1, &m_depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, windowWidth, windowHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRenderbuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hizTexture, 0);

    const GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Lit::Log::Error("Depth Pre-Pass FBO is not complete!");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenFramebuffers(1, &m_hizFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hizFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hizTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_NONE);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::string error;
        switch (status) {
        case GL_FRAMEBUFFER_UNDEFINED:
            error = "GL_FRAMEBUFFER_UNDEFINED";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            error = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            error = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            error = "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            error = "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            error = "GL_FRAMEBUFFER_UNSUPPORTED";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            error = "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
            error = "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
            break;
        default:
            error = "Unknown error";
            break;
        }
        Lit::Log::Error("Hi-Z FBO is not complete! Error: {}", error);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    float quadVertices[] = {
        -1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    glGenVertexArrays(1, &m_debugQuadVao);
    glGenBuffers(1, &m_debugQuadVbo);
    glBindVertexArray(m_debugQuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_debugQuadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glGenBuffers(1, &m_depthPrepassAtomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_depthPrepassAtomicCounter);
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
    reallocate(m_visibleObjectBuffer, m_visibleObjectBufferSize, m_visibleObjectBufferPtr, sizeof(unsigned int));
    reallocate(m_drawCommandBuffer, m_drawCommandBufferSize, m_drawCommandBufferPtr, sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders);
    reallocate(m_visibleTransparentObjectIdsBuffer, m_visibleTransparentObjectIdsBufferSize, m_visibleTransparentObjectIdsBufferPtr, sizeof(VisibleTransparentObject));
    reallocate(m_transparentDrawCommandBuffer, m_transparentDrawCommandBufferSize, m_transparentDrawCommandBufferPtr, sizeof(DrawElementsIndirectCommand));
    reallocate(m_depthPrepassDrawCommandBuffer, m_depthPrepassDrawCommandBufferSize, m_depthPrepassDrawCommandBufferPtr, sizeof(DrawElementsIndirectCommand));
    reallocate(m_visibleLargeObjectBuffer, m_visibleLargeObjectBufferSize, m_visibleLargeObjectBufferPtr, sizeof(unsigned int));

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

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_depthPrepassDrawCommandBuffer);
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_visibleLargeObjectBuffer);
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
    glDeleteBuffers(1, &m_visibleObjectAtomicCounter);
    glDeleteBuffers(1, &m_drawAtomicCounterBuffer);
    glDeleteBuffers(1, &m_meshInfoBuffer);
    glDeleteBuffers(1, &m_renderableBuffer);
    glDeleteBuffers(1, &m_sortedHierarchyBuffer);
    glDeleteBuffers(1, &m_visibleObjectBuffer);
    glDeleteBuffers(1, &m_visibleTransparentObjectIdsBuffer);
    glDeleteBuffers(1, &m_transparentAtomicCounter);
    glDeleteBuffers(1, &m_transparentDrawCommandBuffer);
    glDeleteBuffers(1, &m_depthPrepassDrawCommandBuffer);
    glDeleteBuffers(1, &m_depthPrepassAtomicCounter);
    glDeleteBuffers(1, &m_visibleLargeObjectBuffer);
    glDeleteBuffers(1, &m_visibleLargeObjectAtomicCounter);
    glDeleteBuffers(1, &m_sceneUBO);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryTransformEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryCullEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueSortStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryOpaqueSortEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryCommandGenStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryCommandGenEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectSortStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectSortEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectCommandGenStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryLargeObjectCommandGenEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryDepthPrePassStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryDepthPrePassEnd);
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
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryHizMipmapStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryHizMipmapEnd);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryUiStart);
    glDeleteQueries(NUM_FRAMES_IN_FLIGHT, m_queryUiEnd);

    glDeleteFramebuffers(1, &m_depthFbo);
    glDeleteFramebuffers(1, &m_hizFbo);
    glDeleteRenderbuffers(1, &m_depthRenderbuffer);
    glDeleteTextures(1, &m_hizTexture);
    glDeleteVertexArrays(1, &m_debugQuadVao);
    glDeleteBuffers(1, &m_debugQuadVbo);

    m_shaderManager.cleanup();
    m_cullingShader = nullptr;
    m_transformShader = nullptr;
    m_transparentCullShader = nullptr;
    m_bitonicSortShader = nullptr;
    m_transparentCommandGenShader = nullptr;
    m_largeObjectCullShader = nullptr;
    m_depthPrepassShader = nullptr;
    m_debugDepthShader = nullptr;
    m_hizMipmapShader = nullptr;

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

void Renderer::setSmallObjectThreshold(float threshold) { m_smallObjectThreshold = threshold; }
void Renderer::setLargeObjectThreshold(float threshold) { m_largeObjectThreshold = threshold; }
void Renderer::setDebugMipLevel(int mipLevel) {
    if (mipLevel >= 0 && mipLevel < m_maxMipLevel) {
        m_debugMipLevel = mipLevel;
    }
}

void Renderer::drawScene(SceneDatabase& sceneDatabase, const Camera& camera) {
    auto updateBuffer = [&](GLuint buffer, void* ptr, size_t bufferSize, const auto& data, size_t objectSize) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        const size_t dataSize = data.size() * objectSize;
        const size_t frameOffsetBytes = m_currentFrame * (bufferSize / NUM_FRAMES_IN_FLIGHT);
        memcpy(static_cast<char*>(ptr) + frameOffsetBytes, data.data(), dataSize);
        glFlushMappedBufferRange(GL_SHADER_STORAGE_BUFFER, frameOffsetBytes, dataSize);
    };

    float currentFrameTime = glfwGetTime();
    float deltaTime = currentFrameTime - m_lastFrameTime;
    m_lastFrameTime = currentFrameTime;

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

    if (m_fences[previousFrame]) {
        GLenum waitResult = glClientWaitSync(m_fences[previousFrame], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
        if (waitResult == GL_TIMEOUT_EXPIRED) {
            Lit::Log::Warn("Timeout expired while waiting for fence of previous frame.");
        } else if (waitResult == GL_WAIT_FAILED) {
            Lit::Log::Error("Failed to wait for fence of previous frame.");
        }
        glDeleteSync(m_fences[previousFrame]);
        m_fences[previousFrame] = nullptr;
    }

    long startTime = 0, endTime = 0;
    long transformStartTime = 0, transformEndTime = 0;
    long cullStartTime = 0, cullEndTime = 0;
    long opaqueSortStartTime = 0, opaqueSortEndTime = 0;
    long commandGenStartTime = 0, commandGenEndTime = 0;
    long opaqueDrawStartTime = 0, opaqueDrawEndTime = 0;
    long largeObjectSortStartTime = 0, largeObjectSortEndTime = 0;
    long largeObjectCommandGenStartTime = 0, largeObjectCommandGenEndTime = 0;
    long transparentCullStartTime = 0, transparentCullEndTime = 0;
    long transparentSortStartTime = 0, transparentSortEndTime = 0;
    long transparentCommandGenStartTime = 0, transparentCommandGenEndTime = 0;
    long transparentDrawStartTime = 0, transparentDrawEndTime = 0;
    long depthPrePassStartTime = 0, depthPrePassEndTime = 0;
    long hizMipmapStartTime = 0, hizMipmapEndTime = 0;
    long uiStartTime = 0, uiEndTime = 0;

    int done = 0;
    glGetQueryObjectiv(m_queryEnd[previousFrame], GL_QUERY_RESULT_AVAILABLE, &done);
    if (done) {
        glGetQueryObjecti64v(m_queryStart[previousFrame], GL_QUERY_RESULT, &startTime);
        glGetQueryObjecti64v(m_queryEnd[previousFrame], GL_QUERY_RESULT, &endTime);
        glGetQueryObjecti64v(m_queryTransformStart[previousFrame], GL_QUERY_RESULT, &transformStartTime);
        glGetQueryObjecti64v(m_queryTransformEnd[previousFrame], GL_QUERY_RESULT, &transformEndTime);
        glGetQueryObjecti64v(m_queryCullStart[previousFrame], GL_QUERY_RESULT, &cullStartTime);
        glGetQueryObjecti64v(m_queryCullEnd[previousFrame], GL_QUERY_RESULT, &cullEndTime);
        glGetQueryObjecti64v(m_queryOpaqueSortStart[previousFrame], GL_QUERY_RESULT, &opaqueSortStartTime);
        glGetQueryObjecti64v(m_queryOpaqueSortEnd[previousFrame], GL_QUERY_RESULT, &opaqueSortEndTime);
        glGetQueryObjecti64v(m_queryCommandGenStart[previousFrame], GL_QUERY_RESULT, &commandGenStartTime);
        glGetQueryObjecti64v(m_queryCommandGenEnd[previousFrame], GL_QUERY_RESULT, &commandGenEndTime);
        glGetQueryObjecti64v(m_queryOpaqueDrawStart[previousFrame], GL_QUERY_RESULT, &opaqueDrawStartTime);
        glGetQueryObjecti64v(m_queryOpaqueDrawEnd[previousFrame], GL_QUERY_RESULT, &opaqueDrawEndTime);
        glGetQueryObjecti64v(m_queryLargeObjectSortStart[previousFrame], GL_QUERY_RESULT, &largeObjectSortStartTime);
        glGetQueryObjecti64v(m_queryLargeObjectSortEnd[previousFrame], GL_QUERY_RESULT, &largeObjectSortEndTime);
        glGetQueryObjecti64v(m_queryLargeObjectCommandGenStart[previousFrame], GL_QUERY_RESULT, &largeObjectCommandGenStartTime);
        glGetQueryObjecti64v(m_queryLargeObjectCommandGenEnd[previousFrame], GL_QUERY_RESULT, &largeObjectCommandGenEndTime);
        glGetQueryObjecti64v(m_queryTransparentCullStart[previousFrame], GL_QUERY_RESULT, &transparentCullStartTime);
        glGetQueryObjecti64v(m_queryTransparentCullEnd[previousFrame], GL_QUERY_RESULT, &transparentCullEndTime);
        glGetQueryObjecti64v(m_queryTransparentSortStart[previousFrame], GL_QUERY_RESULT, &transparentSortStartTime);
        glGetQueryObjecti64v(m_queryTransparentSortEnd[previousFrame], GL_QUERY_RESULT, &transparentSortEndTime);
        glGetQueryObjecti64v(m_queryTransparentCommandGenStart[previousFrame], GL_QUERY_RESULT, &transparentCommandGenStartTime);
        glGetQueryObjecti64v(m_queryTransparentCommandGenEnd[previousFrame], GL_QUERY_RESULT, &transparentCommandGenEndTime);
        glGetQueryObjecti64v(m_queryTransparentDrawStart[previousFrame], GL_QUERY_RESULT, &transparentDrawStartTime);
        glGetQueryObjecti64v(m_queryTransparentDrawEnd[previousFrame], GL_QUERY_RESULT, &transparentDrawEndTime);
        glGetQueryObjecti64v(m_queryDepthPrePassStart[previousFrame], GL_QUERY_RESULT, &depthPrePassStartTime);
        glGetQueryObjecti64v(m_queryDepthPrePassEnd[previousFrame], GL_QUERY_RESULT, &depthPrePassEndTime);
        glGetQueryObjecti64v(m_queryHizMipmapStart[previousFrame], GL_QUERY_RESULT, &hizMipmapStartTime);
        glGetQueryObjecti64v(m_queryHizMipmapEnd[previousFrame], GL_QUERY_RESULT, &hizMipmapEndTime);
        glGetQueryObjecti64v(m_queryUiStart[previousFrame], GL_QUERY_RESULT, &uiStartTime);
        glGetQueryObjecti64v(m_queryUiEnd[previousFrame], GL_QUERY_RESULT, &uiEndTime);

        constexpr bool debugMode = true;
        if (debugMode) {
            Lit::Log::Debug("GPU Frame Time: {} ms", (endTime - startTime) / 1000000.0);
            Lit::Log::Debug("  Transform: {} ms", (transformEndTime - transformStartTime) / 1000000.0);
            Lit::Log::Debug("  Depth Pre-Pass: {} ms", (depthPrePassEndTime - depthPrePassStartTime) / 1000000.0);
            Lit::Log::Debug("  Hi-Z Mipmap Gen: {} ms", (hizMipmapEndTime - hizMipmapStartTime) / 1000000.0);
            Lit::Log::Debug("  Opaque Cull: {} ms", (cullEndTime - cullStartTime) / 1000000.0);
            Lit::Log::Debug("  Opaque Sort: {} ms", (opaqueSortEndTime - opaqueSortStartTime) / 1000000.0);
            Lit::Log::Debug("  Command Gen: {} ms", (commandGenEndTime - commandGenStartTime) / 1000000.0);
            Lit::Log::Debug("  Opaque Draw: {} ms", (opaqueDrawEndTime - opaqueDrawStartTime) / 1000000.0);
            Lit::Log::Debug("  Large Object Sort: {} ms", (largeObjectSortEndTime - largeObjectSortStartTime) / 1000000.0);
            Lit::Log::Debug("  Large Object Command Gen: {} ms", (largeObjectCommandGenEndTime - largeObjectCommandGenStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Cull: {} ms", (transparentCullEndTime - transparentCullStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Sort: {} ms", (transparentSortEndTime - transparentSortStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Command Gen: {} ms", (transparentCommandGenEndTime - transparentCommandGenStartTime) / 1000000.0);
            Lit::Log::Debug("  Transparent Draw: {} ms", (transparentDrawEndTime - transparentDrawStartTime) / 1000000.0);
            Lit::Log::Debug("  UI Render: {} ms", (uiEndTime - uiStartTime) / 1000000.0);
        }
    }

    glQueryCounter(m_queryStart[m_currentFrame], GL_TIMESTAMP);
    if (m_processedHierarchyVersion < sceneDatabase.m_hierarchyVersion) {
        sceneDatabase.updateHierarchy();
        m_hierarchyUpdateCounter = NUM_FRAMES_IN_FLIGHT;
        m_processedHierarchyVersion = sceneDatabase.m_hierarchyVersion;
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

    glQueryCounter(m_queryTransformStart[m_currentFrame], GL_TIMESTAMP);
    m_transformShader->bind();
    m_transformShader->setUniform("u_objectCount", (unsigned int)sceneDatabase.sortedHierarchyList.size());

    if (m_processedDataVersion < sceneDatabase.m_dataVersion) {
        m_dataUpdateCounter = NUM_FRAMES_IN_FLIGHT;
        m_processedDataVersion = sceneDatabase.m_dataVersion;
    }

    if (m_hierarchyUpdateCounter > 0) {
        updateBuffer(m_hierarchyBuffer, m_hierarchyBufferPtr, m_hierarchyBufferSize, sceneDatabase.hierarchies, sizeof(HierarchyComponent));
        updateBuffer(m_sortedHierarchyBuffer, m_sortedHierarchyBufferPtr, m_sortedHierarchyBufferSize, sceneDatabase.sortedHierarchyList, sizeof(unsigned int));
        m_hierarchyUpdateCounter--;
    }

    if (m_dataUpdateCounter > 0) {
        updateBuffer(m_objectBuffer, m_objectBufferPtr, m_objectBufferSize, sceneDatabase.transforms, sizeof(TransformComponent));
        updateBuffer(m_renderableBuffer, m_renderableBufferPtr, m_renderableBufferSize, sceneDatabase.renderables, sizeof(RenderableComponent));
        m_dataUpdateCounter--;
    }

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

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshInfoBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, s_meshInfos.size() * sizeof(MeshInfo), s_meshInfos.data(), GL_STATIC_DRAW);

    glm::mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();

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

    const unsigned int workgroupSize = 64;
    const unsigned int numWorkgroups = (numObjects + workgroupSize - 1) / workgroupSize;

    glQueryCounter(m_queryDepthPrePassStart[m_currentFrame], GL_TIMESTAMP);
    unsigned int zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleLargeObjectAtomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    m_largeObjectCullShader->bind();
    m_largeObjectCullShader->setUniform("u_objectCount", numObjects);
    m_largeObjectCullShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);
    m_largeObjectCullShader->setUniform("u_largeObjectThreshold", m_largeObjectThreshold);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_visibleLargeObjectAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

    glDispatchCompute(numWorkgroups, 1, 1);
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    unsigned int visibleLargeObjectCount = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleLargeObjectAtomicCounter);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &visibleLargeObjectCount);

    glQueryCounter(m_queryLargeObjectSortStart[m_currentFrame], GL_TIMESTAMP);
    if (visibleLargeObjectCount > 1) {
        m_largeObjectSortShader->bind();
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

        const unsigned int numElements = nextPowerOfTwo(visibleLargeObjectCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {
                m_largeObjectSortShader->setUniform("u_sort_k", k);
                m_largeObjectSortShader->setUniform("u_sort_j", j);
                const unsigned int numSortWorkgroups = (numElements + 512 - 1) / 512;
                glDispatchCompute(numSortWorkgroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }
    }
    glQueryCounter(m_queryLargeObjectSortEnd[m_currentFrame], GL_TIMESTAMP);

    glQueryCounter(m_queryLargeObjectCommandGenStart[m_currentFrame], GL_TIMESTAMP);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_depthPrepassAtomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    m_largeObjectCommandGenShader->bind();
    m_largeObjectCommandGenShader->setUniform("u_visibleLargeObjectCount", visibleLargeObjectCount);
    m_largeObjectCommandGenShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_depthPrepassAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_depthPrepassDrawCommandBuffer, frameOffset * sizeof(DrawElementsIndirectCommand), m_maxObjects * sizeof(DrawElementsIndirectCommand));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));

    if (visibleLargeObjectCount > 0) {
        glDispatchCompute(1, 1, 1);
    }
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
    glQueryCounter(m_queryLargeObjectCommandGenEnd[m_currentFrame], GL_TIMESTAMP);

    glBindFramebuffer(GL_FRAMEBUFFER, m_depthFbo);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    unsigned int largeObjectDrawCount = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_depthPrepassAtomicCounter);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &largeObjectDrawCount);

    if (largeObjectDrawCount > 0) {
        m_depthPrepassShader->bind();
        glBindVertexArray(m_vao);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_depthPrepassDrawCommandBuffer);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)(frameOffset * sizeof(DrawElementsIndirectCommand)), largeObjectDrawCount, sizeof(DrawElementsIndirectCommand));
    }

    glEnable(GL_DEPTH_TEST);

    glQueryCounter(m_queryDepthPrePassEnd[m_currentFrame], GL_TIMESTAMP);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glQueryCounter(m_queryHizMipmapStart[m_currentFrame], GL_TIMESTAMP);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    m_hizMipmapShader->bind();
    for (int i = 1; i < m_maxMipLevel; ++i) {
        glBindImageTexture(0, m_hizTexture, i - 1, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        glBindImageTexture(1, m_hizTexture, i, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

        const int currentMipWidth = std::max(1, m_windowWidth >> i);
        const int currentMipHeight = std::max(1, m_windowHeight >> i);

        const unsigned int numWorkgroupsX = static_cast<unsigned int>(std::ceil(currentMipWidth / 8.0f));
        const unsigned int numWorkgroupsY = static_cast<unsigned int>(std::ceil(currentMipHeight / 8.0f));

        glDispatchCompute(numWorkgroupsX, numWorkgroupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    glQueryCounter(m_queryHizMipmapEnd[m_currentFrame], GL_TIMESTAMP);

    glQueryCounter(m_queryCullStart[m_currentFrame], GL_TIMESTAMP);

    glQueryCounter(m_queryCullStart[m_currentFrame], GL_TIMESTAMP);

    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleObjectAtomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    m_cullingShader->bind();
    m_cullingShader->setUniform("u_objectCount", numObjects);
    m_cullingShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);
    m_cullingShader->setUniform("u_smallObjectThreshold", m_smallObjectThreshold);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hizTexture);
    m_cullingShader->setUniform("u_hizTexture", 0);
    m_cullingShader->setUniform("u_hizTextureSize", glm::vec2(m_windowWidth, m_windowHeight));
    m_cullingShader->setUniform("u_hizMaxMipLevel", static_cast<float>(m_maxMipLevel - 1));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_visibleObjectAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

    glDispatchCompute(numWorkgroups, 1, 1);

    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    unsigned int visibleObjectCount = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleObjectAtomicCounter);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &visibleObjectCount);

    glQueryCounter(m_queryCullEnd[m_currentFrame], GL_TIMESTAMP);

    glQueryCounter(m_queryOpaqueSortStart[m_currentFrame], GL_TIMESTAMP);
    if (visibleObjectCount > 1) {
        m_opaqueSortShader->bind();
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

        const unsigned int numElements = nextPowerOfTwo(visibleObjectCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {
                m_opaqueSortShader->setUniform("u_sort_k", k);
                m_opaqueSortShader->setUniform("u_sort_j", j);
                const unsigned int numSortWorkgroups = (numElements + 512 - 1) / 512;
                glDispatchCompute(numSortWorkgroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }
    }
    glQueryCounter(m_queryOpaqueSortEnd[m_currentFrame], GL_TIMESTAMP);

    glQueryCounter(m_queryCommandGenStart[m_currentFrame], GL_TIMESTAMP);
    std::vector<unsigned int> drawZeros(m_numDrawingShaders, 0);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_drawAtomicCounterBuffer);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int) * m_numDrawingShaders, drawZeros.data());

    m_commandGenShader->bind();
    m_commandGenShader->setUniform("u_visibleObjectCount", visibleObjectCount);
    m_commandGenShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_drawAtomicCounterBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_drawCommandBuffer, frameOffset * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders, m_maxObjects * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshInfoBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));

    if (visibleObjectCount > 0) {
        glDispatchCompute(1, 1, 1);
    }
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
    glQueryCounter(m_queryCommandGenEnd[m_currentFrame], GL_TIMESTAMP);

    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    std::vector<unsigned int> drawCounts(m_numDrawingShaders);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_drawAtomicCounterBuffer);
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int) * m_numDrawingShaders, drawCounts.data());

    glBindVertexArray(m_vao);

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));

    glQueryCounter(m_queryOpaqueDrawStart[m_currentFrame], GL_TIMESTAMP);
    for (uint32_t shaderId = 0; shaderId < m_numDrawingShaders; ++shaderId) {
        const unsigned int drawCount = drawCounts[shaderId];
        if (drawCount > 0) {
            Shader* shader = m_shaderManager.getShader(shaderId);
            if (shader) {
                shader->bind();
                const size_t indirect_offset = (m_currentFrame * m_numDrawingShaders * m_maxObjects + shaderId * m_maxObjects) * sizeof(DrawElementsIndirectCommand);
                glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)(indirect_offset), drawCount, sizeof(DrawElementsIndirectCommand));
            }
        }
    }
    glQueryCounter(m_queryOpaqueDrawEnd[m_currentFrame], GL_TIMESTAMP);

    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(unsigned int), &zero);

    glQueryCounter(m_queryTransparentCullStart[m_currentFrame], GL_TIMESTAMP);
    m_transparentCullShader->bind();
    m_transparentCullShader->setUniform("u_objectCount", numObjects);
    m_transparentCullShader->setUniform("u_cameraPos", camera.getPosition());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transparentAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectIdsBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));
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
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectIdsBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));

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
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectIdsBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));
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

    glQueryCounter(m_queryUiStart[m_currentFrame], GL_TIMESTAMP);
    m_uiManager->render();

    glQueryCounter(m_queryUiEnd[m_currentFrame], GL_TIMESTAMP);
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

    const auto commandGenId = m_shaderManager.loadComputeShader("resources/shaders/command_gen.comp");
    m_commandGenShader = m_shaderManager.getShader(commandGenId);

    const auto largeObjectCullId = m_shaderManager.loadComputeShader("resources/shaders/large_object_cull.comp");
    m_largeObjectCullShader = m_shaderManager.getShader(largeObjectCullId);

    const auto largeObjectSortId = m_shaderManager.loadComputeShader("resources/shaders/large_object_sort.comp");
    m_largeObjectSortShader = m_shaderManager.getShader(largeObjectSortId);

    const auto largeObjectCommandGenId = m_shaderManager.loadComputeShader("resources/shaders/large_object_command_gen.comp");
    m_largeObjectCommandGenShader = m_shaderManager.getShader(largeObjectCommandGenId);

    const auto depthPrepassId = m_shaderManager.loadShader("resources/shaders/depth_prepass.vert", "resources/shaders/depth_prepass.frag");
    m_depthPrepassShader = m_shaderManager.getShader(depthPrepassId);

    const auto debugDepthId = m_shaderManager.loadShader("resources/shaders/depth_debug.vert", "resources/shaders/depth_debug.frag");
    m_debugDepthShader = m_shaderManager.getShader(debugDepthId);

    const auto hizMipmapId = m_shaderManager.loadComputeShader("resources/shaders/hiz_mipmap.comp");
    m_hizMipmapShader = m_shaderManager.getShader(hizMipmapId);

    const auto opaqueSortId = m_shaderManager.loadComputeShader("resources/shaders/opaque_sort.comp");
    m_opaqueSortShader = m_shaderManager.getShader(opaqueSortId);
    if (hizMipmapId == static_cast<uint32_t>(-1)) {
        Lit::Log::Error("Failed to load Hi-Z Mipmap compute shader (loadComputeShader returned -1).");
    } else if (m_hizMipmapShader == nullptr) {
        Lit::Log::Error("Hi-Z Mipmap shader pointer is null after successful load ID.");
    } else if (!m_hizMipmapShader->isInitialized()) {
        Lit::Log::Error("Hi-Z Mipmap shader object is not initialized after loading.");
    }
}

void Renderer::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_uiManager->addText(text, x, y, scale, color);
}