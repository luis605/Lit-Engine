module;

#include "DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/SwapChain.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Fence.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Query.h"
#include "DiligentCore/Graphics/GraphicsEngineOpenGL/interface/EngineFactoryOpenGL.h"
#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Shader.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Sampler.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#if defined(__linux__)
#undef Bool
#undef True
#undef False
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_NSGL
#endif
#include <GLFW/glfw3native.h>

#include <vector>
#include <optional>
#include <unordered_map>
#include <cmath>
#include <string>
#include <cstring>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <chrono>
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

static std::string LoadSourceFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Lit::Log::Error("Failed to open shader file: {}", filepath);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

struct DiligentData {
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> pDevice;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> pImmediateContext;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> pSwapChain;
    Diligent::IEngineFactoryOpenGL* pFactoryGL = nullptr;

    static constexpr int NumFrames = 3;
    Diligent::RefCntAutoPtr<Diligent::IFence> pFences[NumFrames];
    Diligent::Uint64 FenceValues[NumFrames] = {0};
    Diligent::Uint64 CurrentFenceValue = 0;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pObjectBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pObjectBufferViews[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pHierarchyBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pHierarchyBufferViews[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pRenderableBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSortedHierarchyBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pSortedHierarchyBufferViews[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleObjectBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDrawCommandBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleTransparentObjectIdsBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentDrawCommandBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleLargeObjectBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDepthPrepassDrawCommandBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSceneUBO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pMeshInfoBuffer;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransformPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransformSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransformUniforms;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleObjectAtomicCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDrawAtomicCounterBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleLargeObjectAtomicCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentAtomicCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDepthPrepassAtomicCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVBO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pEBO;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pHiZTextures[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::ITexture> pDepthRenderbuffers[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::ISampler> pHiZSampler;
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransformStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransformEndQuery[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IQuery> pFrameStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pFrameEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCullStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCullEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pOpaqueSortStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pOpaqueSortEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCommandGenStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCommandGenEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectSortStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectSortEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectCommandGenStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectCommandGenEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pDepthPrePassStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pDepthPrePassEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentCullStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentCullEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentSortStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentSortEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentCommandGenStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentCommandGenEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pOpaqueDrawStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pOpaqueDrawEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentDrawStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentDrawEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pHizMipmapStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pHizMipmapEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pUiStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pUiEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectCullStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectCullEndQuery[NumFrames];

    bool QueryReady[NumFrames] = {false};
    bool OpaqueSortActive[NumFrames] = {false};
    bool LargeObjectSortActive[NumFrames] = {false};
    bool TransparentSortActive[NumFrames] = {false};
    bool TransparentDrawActive[NumFrames] = {false};
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pStagingBuffer;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pHiZMipmapPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pHiZMipmapSRB;
};

Renderer::Renderer()
    : m_cullingShader(nullptr), m_transformShader(nullptr), m_transparentCullShader(nullptr), m_bitonicSortShader(nullptr),
      m_opaqueSortShader(nullptr), m_transparentCommandGenShader(nullptr), m_commandGenShader(nullptr), m_largeObjectCullShader(nullptr),
      m_largeObjectSortShader(nullptr), m_largeObjectCommandGenShader(nullptr), m_depthPrepassShader(nullptr),
      m_hizMipmapShader(nullptr), m_initialized(false), m_vboSize(0), m_eboSize(0), m_numDrawingShaders(0),
      fullProfiling(false) {}

void Renderer::init(GLFWwindow* window, const int windowWidth, const int windowHeight) {
    if (m_initialized)
        return;

    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

    m_diligent = new DiligentData();
    m_diligent->pFactoryGL = Diligent::GetEngineFactoryOpenGL();

    Diligent::EngineGLCreateInfo EngineCI;
#if defined(_WIN32)
    EngineCI.Window.hWnd = glfwGetWin32Window(window);
#elif defined(__linux__)
    EngineCI.Window.WindowId = glfwGetX11Window(window);
    EngineCI.Window.pDisplay = glfwGetX11Display();
#elif defined(__APPLE__)
    EngineCI.Window.pNSView = glfwGetCocoaWindow(window);
#endif

#ifndef NDEBUG
    m_diligent->pFactoryGL->SetMessageCallback(nullptr);
#endif

    m_diligent->pFactoryGL->AttachToActiveGLContext(EngineCI, &m_diligent->pDevice, &m_diligent->pImmediateContext);

    m_uiManager = new UIManager();
    m_uiManager->init(windowWidth, windowHeight);

    setupShaders();
    createTransformPSO();
    createHiZPSO();

    if (!m_cullingShader || !m_cullingShader->isInitialized()) {
        Lit::Log::Error("Renderer failed to initialize: Culling shader could not be loaded.");
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    m_maxObjects = 1000000;
    reallocateBuffers(m_maxObjects);

    const unsigned int zero = 0;
    Diligent::BufferDesc AtomicCounterDesc;
    AtomicCounterDesc.Name = "Visible Object Atomic Counter";
    AtomicCounterDesc.Usage = Diligent::USAGE_DEFAULT;
    AtomicCounterDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    AtomicCounterDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    AtomicCounterDesc.ElementByteStride = sizeof(unsigned int);
    AtomicCounterDesc.Size = sizeof(unsigned int);
    Diligent::BufferData AtomicCounterData;
    AtomicCounterData.pData = &zero;
    AtomicCounterData.DataSize = sizeof(unsigned int);
    m_diligent->pVisibleObjectAtomicCounter.Release();
    m_diligent->pDevice->CreateBuffer(AtomicCounterDesc, &AtomicCounterData, &m_diligent->pVisibleObjectAtomicCounter);
    m_visibleObjectAtomicCounter = (GLuint)(size_t)m_diligent->pVisibleObjectAtomicCounter->GetNativeHandle();
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleObjectAtomicCounter);

    Diligent::BufferDesc DrawAtomicCounterDesc;
    DrawAtomicCounterDesc.Name = "Draw Atomic Counter Buffer";
    DrawAtomicCounterDesc.Usage = Diligent::USAGE_DEFAULT;
    DrawAtomicCounterDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    DrawAtomicCounterDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    DrawAtomicCounterDesc.ElementByteStride = sizeof(unsigned int);
    DrawAtomicCounterDesc.Size = sizeof(unsigned int) * m_shaderManager.getShaderCount();

    std::vector<unsigned int> drawZeros(m_shaderManager.getShaderCount(), 0);
    Diligent::BufferData DrawAtomicCounterData;
    DrawAtomicCounterData.pData = drawZeros.data();
    DrawAtomicCounterData.DataSize = drawZeros.size() * sizeof(unsigned int);
    m_diligent->pDrawAtomicCounterBuffer.Release();
    m_diligent->pDevice->CreateBuffer(DrawAtomicCounterDesc, &DrawAtomicCounterData, &m_diligent->pDrawAtomicCounterBuffer);
    m_drawAtomicCounterBuffer = (GLuint)(size_t)m_diligent->pDrawAtomicCounterBuffer->GetNativeHandle();
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_drawAtomicCounterBuffer);

    Diligent::QueryDesc queryDesc;
    queryDesc.Type = Diligent::QUERY_TYPE_TIMESTAMP;

#define CREATE_QUERY_PAIR(name, startPtr, endPtr)                              \
    for (int i = 0; i < DiligentData::NumFrames; ++i) {                        \
        queryDesc.Name = name " Start";                                        \
        m_diligent->pDevice->CreateQuery(queryDesc, &m_diligent->startPtr[i]); \
        queryDesc.Name = name " End";                                          \
        m_diligent->pDevice->CreateQuery(queryDesc, &m_diligent->endPtr[i]);   \
    }

    CREATE_QUERY_PAIR("Frame", pFrameStartQuery, pFrameEndQuery);
    CREATE_QUERY_PAIR("Transform", pTransformStartQuery, pTransformEndQuery);
    CREATE_QUERY_PAIR("Cull", pCullStartQuery, pCullEndQuery);
    CREATE_QUERY_PAIR("Opaque Sort", pOpaqueSortStartQuery, pOpaqueSortEndQuery);
    CREATE_QUERY_PAIR("Command Gen", pCommandGenStartQuery, pCommandGenEndQuery);
    CREATE_QUERY_PAIR("Large Object Sort", pLargeObjectSortStartQuery, pLargeObjectSortEndQuery);
    CREATE_QUERY_PAIR("Large Object Command Gen", pLargeObjectCommandGenStartQuery, pLargeObjectCommandGenEndQuery);
    CREATE_QUERY_PAIR("Depth Pre-Pass", pDepthPrePassStartQuery, pDepthPrePassEndQuery);
    CREATE_QUERY_PAIR("Transparent Cull", pTransparentCullStartQuery, pTransparentCullEndQuery);
    CREATE_QUERY_PAIR("Transparent Sort", pTransparentSortStartQuery, pTransparentSortEndQuery);
    CREATE_QUERY_PAIR("Transparent Command Gen", pTransparentCommandGenStartQuery, pTransparentCommandGenEndQuery);
    CREATE_QUERY_PAIR("Opaque Draw", pOpaqueDrawStartQuery, pOpaqueDrawEndQuery);
    CREATE_QUERY_PAIR("Transparent Draw", pTransparentDrawStartQuery, pTransparentDrawEndQuery);
    CREATE_QUERY_PAIR("Hi-Z Mipmap", pHizMipmapStartQuery, pHizMipmapEndQuery);
    CREATE_QUERY_PAIR("UI", pUiStartQuery, pUiEndQuery);
    CREATE_QUERY_PAIR("Large Object Cull", pLargeObjectCullStartQuery, pLargeObjectCullEndQuery);

#undef CREATE_QUERY_PAIR

    Diligent::BufferDesc LargeObjectAtomicCounterDesc;
    LargeObjectAtomicCounterDesc.Name = "Visible Large Object Atomic Counter";
    LargeObjectAtomicCounterDesc.Usage = Diligent::USAGE_DEFAULT;
    LargeObjectAtomicCounterDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    LargeObjectAtomicCounterDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    LargeObjectAtomicCounterDesc.ElementByteStride = sizeof(unsigned int);
    LargeObjectAtomicCounterDesc.Size = sizeof(unsigned int);
    m_diligent->pVisibleLargeObjectAtomicCounter.Release();
    m_diligent->pDevice->CreateBuffer(LargeObjectAtomicCounterDesc, &AtomicCounterData, &m_diligent->pVisibleLargeObjectAtomicCounter);
    m_visibleLargeObjectAtomicCounter = (GLuint)(size_t)m_diligent->pVisibleLargeObjectAtomicCounter->GetNativeHandle();
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_visibleLargeObjectAtomicCounter);

    m_vboSize = 1024 * 1024 * 10;
    m_eboSize = 1024 * 1024 * 4;

    m_vboSize = 1024 * 1024 * 10;
    m_eboSize = 1024 * 1024 * 4;

    Diligent::BufferDesc VBODesc;
    VBODesc.Name = "VBO";
    VBODesc.Usage = Diligent::USAGE_DEFAULT;
    VBODesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    VBODesc.Size = m_vboSize;
    m_diligent->pVBO.Release();
    m_diligent->pDevice->CreateBuffer(VBODesc, nullptr, &m_diligent->pVBO);
    m_vbo = (GLuint)(size_t)m_diligent->pVBO->GetNativeHandle();

    Diligent::BufferDesc EBODesc;
    EBODesc.Name = "EBO";
    EBODesc.Usage = Diligent::USAGE_DEFAULT;
    EBODesc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    EBODesc.Size = m_eboSize;
    m_diligent->pEBO.Release();
    m_diligent->pDevice->CreateBuffer(EBODesc, nullptr, &m_diligent->pEBO);
    m_ebo = (GLuint)(size_t)m_diligent->pEBO->GetNativeHandle();

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    Diligent::BufferDesc TransparentAtomicCounterDesc;
    TransparentAtomicCounterDesc.Name = "Transparent Atomic Counter";
    TransparentAtomicCounterDesc.Usage = Diligent::USAGE_DEFAULT;
    TransparentAtomicCounterDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    TransparentAtomicCounterDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    TransparentAtomicCounterDesc.ElementByteStride = sizeof(unsigned int);
    TransparentAtomicCounterDesc.Size = sizeof(unsigned int);
    m_diligent->pTransparentAtomicCounter.Release();
    m_diligent->pDevice->CreateBuffer(TransparentAtomicCounterDesc, &AtomicCounterData, &m_diligent->pTransparentAtomicCounter);
    m_transparentAtomicCounter = (GLuint)(size_t)m_diligent->pTransparentAtomicCounter->GetNativeHandle();
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_transparentAtomicCounter);

    m_maxMipLevel = static_cast<int>(std::floor(std::log2(std::max(windowWidth, windowHeight))));

    glGenFramebuffers(NUM_FRAMES_IN_FLIGHT, m_depthFbo);

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        Diligent::TextureDesc HiZDesc;
        HiZDesc.Name = "Hi-Z Texture";
        HiZDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        HiZDesc.Width = windowWidth;
        HiZDesc.Height = windowHeight;
        HiZDesc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
        HiZDesc.Usage = Diligent::USAGE_DEFAULT;
        HiZDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_RENDER_TARGET;
        HiZDesc.MipLevels = m_maxMipLevel;

        m_diligent->pHiZTextures[i].Release();
        m_diligent->pDevice->CreateTexture(HiZDesc, nullptr, &m_diligent->pHiZTextures[i]);
        m_hizTexture[i] = (GLuint)(size_t)m_diligent->pHiZTextures[i]->GetNativeHandle();
        Lit::Log::Info("Hi-Z Texture {}: native handle {}", i, m_hizTexture[i]);

        Diligent::TextureDesc DepthDesc;
        DepthDesc.Name = "Depth Pre-pass Renderbuffer";
        DepthDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        DepthDesc.Width = windowWidth;
        DepthDesc.Height = windowHeight;
        DepthDesc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
        DepthDesc.Usage = Diligent::USAGE_DEFAULT;
        DepthDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL;

        m_diligent->pDepthRenderbuffers[i].Release();
        m_diligent->pDevice->CreateTexture(DepthDesc, nullptr, &m_diligent->pDepthRenderbuffers[i]);
        m_depthRenderbuffer[i] = (GLuint)(size_t)m_diligent->pDepthRenderbuffers[i]->GetNativeHandle();
        Lit::Log::Info("Depth Renderbuffer {}: native handle {}", i, m_depthRenderbuffer[i]);

        glBindFramebuffer(GL_FRAMEBUFFER, m_depthFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthRenderbuffer[i], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hizTexture[i], 0);

        const GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, drawBuffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Lit::Log::Error("Depth Pre-Pass FBO {} is not complete!", i);
        }
    }

    Diligent::SamplerDesc SamplerCI;
    SamplerCI.Name = "Hi-Z Sampler";
    SamplerCI.MinFilter = Diligent::FILTER_TYPE_POINT;
    SamplerCI.MagFilter = Diligent::FILTER_TYPE_POINT;
    SamplerCI.MipFilter = Diligent::FILTER_TYPE_POINT;
    SamplerCI.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    SamplerCI.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    m_diligent->pDevice->CreateSampler(SamplerCI, &m_diligent->pHiZSampler);

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_hizTexture[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    Diligent::BufferDesc DepthPrepassAtomicCounterDesc;
    DepthPrepassAtomicCounterDesc.Name = "Depth Prepass Atomic Counter";
    DepthPrepassAtomicCounterDesc.Usage = Diligent::USAGE_DEFAULT;
    DepthPrepassAtomicCounterDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    DepthPrepassAtomicCounterDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    DepthPrepassAtomicCounterDesc.ElementByteStride = sizeof(unsigned int);
    DepthPrepassAtomicCounterDesc.Size = sizeof(unsigned int);
    m_diligent->pDepthPrepassAtomicCounter.Release();
    m_diligent->pDevice->CreateBuffer(DepthPrepassAtomicCounterDesc, &AtomicCounterData, &m_diligent->pDepthPrepassAtomicCounter);
    m_depthPrepassAtomicCounter = (GLuint)(size_t)m_diligent->pDepthPrepassAtomicCounter->GetNativeHandle();

    Diligent::BufferDesc StagingDesc;
    StagingDesc.Name = "Staging Buffer";
    StagingDesc.Usage = Diligent::USAGE_STAGING;
    StagingDesc.BindFlags = Diligent::BIND_NONE;
    StagingDesc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    StagingDesc.Size = sizeof(unsigned int);
    m_diligent->pStagingBuffer.Release();
    m_diligent->pDevice->CreateBuffer(StagingDesc, nullptr, &m_diligent->pStagingBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_depthPrepassAtomicCounter);

    Diligent::FenceDesc FenceCI;
    FenceCI.Type = Diligent::FENCE_TYPE_CPU_WAIT_ONLY;
    for (int i = 0; i < DiligentData::NumFrames; ++i) {
        m_diligent->pDevice->CreateFence(FenceCI, &m_diligent->pFences[i]);
        m_diligent->FenceValues[i] = 0;
    }
    m_diligent->CurrentFenceValue = 0;

    m_initialized = true;
}

void Renderer::reallocateBuffers(size_t numObjects) {
    m_maxObjects = numObjects;
    Lit::Log::Info("Reallocating renderer buffers for {} objects.", m_maxObjects);

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

    m_objectBufferSize = m_maxObjects * sizeof(TransformComponent) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc ObjBuffDesc;
    ObjBuffDesc.Name = "Object Buffer";
    ObjBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    ObjBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    ObjBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    ObjBuffDesc.ElementByteStride = sizeof(TransformComponent);
    ObjBuffDesc.Size = m_objectBufferSize;
    m_diligent->pObjectBuffer.Release();
    m_diligent->pDevice->CreateBuffer(ObjBuffDesc, nullptr, &m_diligent->pObjectBuffer);
    m_objectBuffer = (GLuint)(size_t)m_diligent->pObjectBuffer->GetNativeHandle();

    m_hierarchyBufferSize = m_maxObjects * sizeof(HierarchyComponent) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc HierarchyBuffDesc;
    HierarchyBuffDesc.Name = "Hierarchy Buffer";
    HierarchyBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    HierarchyBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    HierarchyBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    HierarchyBuffDesc.ElementByteStride = sizeof(HierarchyComponent);
    HierarchyBuffDesc.Size = m_hierarchyBufferSize;
    m_diligent->pHierarchyBuffer.Release();
    m_diligent->pDevice->CreateBuffer(HierarchyBuffDesc, nullptr, &m_diligent->pHierarchyBuffer);
    m_hierarchyBuffer = (GLuint)(size_t)m_diligent->pHierarchyBuffer->GetNativeHandle();

    m_renderableBufferSize = m_maxObjects * sizeof(RenderableComponent) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc RenderableBuffDesc;
    RenderableBuffDesc.Name = "Renderable Buffer";
    RenderableBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    RenderableBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    RenderableBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    RenderableBuffDesc.ElementByteStride = sizeof(RenderableComponent);
    RenderableBuffDesc.Size = m_renderableBufferSize;
    m_diligent->pRenderableBuffer.Release();
    m_diligent->pDevice->CreateBuffer(RenderableBuffDesc, nullptr, &m_diligent->pRenderableBuffer);
    m_renderableBuffer = (GLuint)(size_t)m_diligent->pRenderableBuffer->GetNativeHandle();

    m_sortedHierarchyBufferSize = m_maxObjects * sizeof(unsigned int) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc SortedHierarchyBuffDesc;
    SortedHierarchyBuffDesc.Name = "Sorted Hierarchy Buffer";
    SortedHierarchyBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    SortedHierarchyBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    SortedHierarchyBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    SortedHierarchyBuffDesc.ElementByteStride = sizeof(unsigned int);
    SortedHierarchyBuffDesc.Size = m_sortedHierarchyBufferSize;
    m_diligent->pSortedHierarchyBuffer.Release();
    m_diligent->pDevice->CreateBuffer(SortedHierarchyBuffDesc, nullptr, &m_diligent->pSortedHierarchyBuffer);
    m_sortedHierarchyBuffer = (GLuint)(size_t)m_diligent->pSortedHierarchyBuffer->GetNativeHandle();

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        Diligent::BufferViewDesc ViewDesc;
        ViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
        ViewDesc.ByteOffset = i * m_maxObjects * sizeof(TransformComponent);
        ViewDesc.ByteWidth = m_maxObjects * sizeof(TransformComponent);
        m_diligent->pObjectBuffer->CreateView(ViewDesc, &m_diligent->pObjectBufferViews[i]);

        ViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        ViewDesc.ByteOffset = i * m_maxObjects * sizeof(HierarchyComponent);
        ViewDesc.ByteWidth = m_maxObjects * sizeof(HierarchyComponent);
        m_diligent->pHierarchyBuffer->CreateView(ViewDesc, &m_diligent->pHierarchyBufferViews[i]);

        ViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        ViewDesc.ByteOffset = i * m_maxObjects * sizeof(unsigned int);
        ViewDesc.ByteWidth = m_maxObjects * sizeof(unsigned int);
        m_diligent->pSortedHierarchyBuffer->CreateView(ViewDesc, &m_diligent->pSortedHierarchyBufferViews[i]);
    }

    m_visibleObjectBufferSize = m_maxObjects * sizeof(unsigned int) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc VisibleObjectsBuffDesc;
    VisibleObjectsBuffDesc.Name = "Visible Objects Buffer";
    VisibleObjectsBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    VisibleObjectsBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    VisibleObjectsBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    VisibleObjectsBuffDesc.ElementByteStride = sizeof(unsigned int);
    VisibleObjectsBuffDesc.Size = m_visibleObjectBufferSize;
    m_diligent->pVisibleObjectBuffer.Release();
    m_diligent->pDevice->CreateBuffer(VisibleObjectsBuffDesc, nullptr, &m_diligent->pVisibleObjectBuffer);
    m_visibleObjectBuffer = (GLuint)(size_t)m_diligent->pVisibleObjectBuffer->GetNativeHandle();

    m_drawCommandBufferSize = m_maxObjects * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc DrawCommandsBuffDesc;
    DrawCommandsBuffDesc.Name = "Draw Command Buffer";
    DrawCommandsBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    DrawCommandsBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    DrawCommandsBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    DrawCommandsBuffDesc.ElementByteStride = sizeof(DrawElementsIndirectCommand);
    DrawCommandsBuffDesc.Size = m_drawCommandBufferSize;
    m_diligent->pDrawCommandBuffer.Release();
    m_diligent->pDevice->CreateBuffer(DrawCommandsBuffDesc, nullptr, &m_diligent->pDrawCommandBuffer);
    m_drawCommandBuffer = (GLuint)(size_t)m_diligent->pDrawCommandBuffer->GetNativeHandle();

    m_visibleTransparentObjectIdsBufferSize = m_maxObjects * sizeof(VisibleTransparentObject) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc TransparentIdsBuffDesc;
    TransparentIdsBuffDesc.Name = "Visible Transparent Object IDs Buffer";
    TransparentIdsBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    TransparentIdsBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    TransparentIdsBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    TransparentIdsBuffDesc.ElementByteStride = sizeof(VisibleTransparentObject);
    TransparentIdsBuffDesc.Size = m_visibleTransparentObjectIdsBufferSize;
    m_diligent->pVisibleTransparentObjectIdsBuffer.Release();
    m_diligent->pDevice->CreateBuffer(TransparentIdsBuffDesc, nullptr, &m_diligent->pVisibleTransparentObjectIdsBuffer);
    m_visibleTransparentObjectIdsBuffer = (GLuint)(size_t)m_diligent->pVisibleTransparentObjectIdsBuffer->GetNativeHandle();

    m_transparentDrawCommandBufferSize = m_maxObjects * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc TransparentDrawBuffDesc;
    TransparentDrawBuffDesc.Name = "Transparent Draw Command Buffer";
    TransparentDrawBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    TransparentDrawBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    TransparentDrawBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    TransparentDrawBuffDesc.ElementByteStride = sizeof(DrawElementsIndirectCommand);
    TransparentDrawBuffDesc.Size = m_transparentDrawCommandBufferSize;
    m_diligent->pTransparentDrawCommandBuffer.Release();
    m_diligent->pDevice->CreateBuffer(TransparentDrawBuffDesc, nullptr, &m_diligent->pTransparentDrawCommandBuffer);
    m_transparentDrawCommandBuffer = (GLuint)(size_t)m_diligent->pTransparentDrawCommandBuffer->GetNativeHandle();

    m_depthPrepassDrawCommandBufferSize = m_maxObjects * sizeof(DrawElementsIndirectCommand) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc DepthPrepassDrawBuffDesc;
    DepthPrepassDrawBuffDesc.Name = "Depth Pre-pass Draw Command Buffer";
    DepthPrepassDrawBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    DepthPrepassDrawBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    DepthPrepassDrawBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    DepthPrepassDrawBuffDesc.ElementByteStride = sizeof(DrawElementsIndirectCommand);
    DepthPrepassDrawBuffDesc.Size = m_depthPrepassDrawCommandBufferSize;
    m_diligent->pDepthPrepassDrawCommandBuffer.Release();
    m_diligent->pDevice->CreateBuffer(DepthPrepassDrawBuffDesc, nullptr, &m_diligent->pDepthPrepassDrawCommandBuffer);
    m_depthPrepassDrawCommandBuffer = (GLuint)(size_t)m_diligent->pDepthPrepassDrawCommandBuffer->GetNativeHandle();

    m_visibleLargeObjectBufferSize = m_maxObjects * sizeof(unsigned int) * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc VisibleLargeObjectsBuffDesc;
    VisibleLargeObjectsBuffDesc.Name = "Visible Large Objects Buffer";
    VisibleLargeObjectsBuffDesc.Usage = Diligent::USAGE_DEFAULT;
    VisibleLargeObjectsBuffDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    VisibleLargeObjectsBuffDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    VisibleLargeObjectsBuffDesc.ElementByteStride = sizeof(unsigned int);
    VisibleLargeObjectsBuffDesc.Size = m_visibleLargeObjectBufferSize;
    m_diligent->pVisibleLargeObjectBuffer.Release();
    m_diligent->pDevice->CreateBuffer(VisibleLargeObjectsBuffDesc, nullptr, &m_diligent->pVisibleLargeObjectBuffer);
    m_visibleLargeObjectBuffer = (GLuint)(size_t)m_diligent->pVisibleLargeObjectBuffer->GetNativeHandle();

    const size_t alignedSceneUniformsSize = (sizeof(SceneUniforms) + 255) & ~255;
    m_sceneUBOSize = alignedSceneUniformsSize * NUM_FRAMES_IN_FLIGHT;
    Diligent::BufferDesc SceneUBODesc;
    SceneUBODesc.Name = "Scene UBO";
    SceneUBODesc.Usage = Diligent::USAGE_DEFAULT;
    SceneUBODesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    SceneUBODesc.Size = m_sceneUBOSize;
    m_diligent->pSceneUBO.Release();
    m_diligent->pDevice->CreateBuffer(SceneUBODesc, nullptr, &m_diligent->pSceneUBO);

    Diligent::BufferDesc MeshInfoDesc;
    MeshInfoDesc.Name = "Mesh Info Buffer";
    MeshInfoDesc.Usage = Diligent::USAGE_DEFAULT;
    MeshInfoDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    MeshInfoDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    MeshInfoDesc.ElementByteStride = sizeof(MeshInfo);
    MeshInfoDesc.Size = m_maxObjects * sizeof(MeshInfo);
    m_diligent->pMeshInfoBuffer.Release();
    m_diligent->pDevice->CreateBuffer(MeshInfoDesc, nullptr, &m_diligent->pMeshInfoBuffer);

    if (m_diligent->pTransformPSO) {
        m_diligent->pTransformSRB.Release();
        m_diligent->pTransformPSO->CreateShaderResourceBinding(&m_diligent->pTransformSRB, true);
        if (!m_diligent->pTransformSRB) {
            Lit::Log::Error("Failed to create Transform SRB");
            return;
        }
        auto* transformBuf = m_diligent->pTransformSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransformBuffer");
        auto* hierarchyBuf = m_diligent->pTransformSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "HierarchyBuffer");
        auto* sortedBuf = m_diligent->pTransformSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortedHierarchyBuffer");
        auto* uniformsVar = m_diligent->pTransformSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransformUniforms");

        if (!transformBuf || !hierarchyBuf || !sortedBuf || !uniformsVar) {
            Lit::Log::Error("Failed to get transform shader variables");
            return;
        }

        transformBuf->Set(m_diligent->pObjectBuffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
        hierarchyBuf->Set(m_diligent->pHierarchyBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
        sortedBuf->Set(m_diligent->pSortedHierarchyBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
        uniformsVar->Set(m_diligent->pTransformUniforms);
    }

    m_dataUpdateCounter = NUM_FRAMES_IN_FLIGHT;
    m_hierarchyUpdateCounter = NUM_FRAMES_IN_FLIGHT;
}

void Renderer::cleanup() {
    if (!m_initialized)
        return;

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_diligent->pFences[i].Release();
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glDeleteVertexArrays(1, &m_vao);
    glDeleteVertexArrays(1, &m_vao);

    glDeleteFramebuffers(NUM_FRAMES_IN_FLIGHT, m_depthFbo);

    m_shaderManager.cleanup();
    m_cullingShader = nullptr;
    m_transformShader = nullptr;
    m_transparentCullShader = nullptr;
    m_bitonicSortShader = nullptr;
    m_transparentCommandGenShader = nullptr;
    m_largeObjectCullShader = nullptr;
    m_depthPrepassShader = nullptr;

    m_hizMipmapShader = nullptr;

    m_shaderManager.cleanup();

    delete m_uiManager;
    m_uiManager = nullptr;

    if (m_diligent) {
        delete m_diligent;
        m_diligent = nullptr;
    }

    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

void Renderer::uploadMesh(const Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    const size_t vertexDataSize = mesh.vertices.size() * sizeof(float);
    const size_t indexDataSize = mesh.indices.size() * sizeof(unsigned int);

    Lit::Log::Info("Uploading mesh: {} vertices ({} bytes), {} indices ({} bytes)", mesh.vertices.size() / 6,
                   vertexDataSize, mesh.indices.size(), indexDataSize);

    if (s_totalVertexSize + vertexDataSize > m_vboSize || s_totalIndexSize + indexDataSize > m_eboSize) {
        m_vboSize = std::max(m_vboSize * 2, s_totalVertexSize + vertexDataSize);
        m_eboSize = std::max(m_eboSize * 2, s_totalIndexSize + indexDataSize);

        Diligent::RefCntAutoPtr<Diligent::IBuffer> pNewVBO;
        Diligent::BufferDesc VBODesc;
        VBODesc.Name = "VBO";
        VBODesc.Usage = Diligent::USAGE_DEFAULT;
        VBODesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
        VBODesc.Size = m_vboSize;
        m_diligent->pDevice->CreateBuffer(VBODesc, nullptr, &pNewVBO);
        GLuint new_vbo = (GLuint)(size_t)pNewVBO->GetNativeHandle();

        Diligent::RefCntAutoPtr<Diligent::IBuffer> pNewEBO;
        Diligent::BufferDesc EBODesc;
        EBODesc.Name = "EBO";
        EBODesc.Usage = Diligent::USAGE_DEFAULT;
        EBODesc.BindFlags = Diligent::BIND_INDEX_BUFFER;
        EBODesc.Size = m_eboSize;
        m_diligent->pDevice->CreateBuffer(EBODesc, nullptr, &pNewEBO);
        GLuint new_ebo = (GLuint)(size_t)pNewEBO->GetNativeHandle();

        if (s_totalVertexSize > 0)
            m_diligent->pImmediateContext->CopyBuffer(m_diligent->pVBO, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, pNewVBO, 0, s_totalVertexSize, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (s_totalIndexSize > 0)
            m_diligent->pImmediateContext->CopyBuffer(m_diligent->pEBO, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, pNewEBO, 0, s_totalIndexSize, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_diligent->pVBO = pNewVBO;
        m_diligent->pEBO = pNewEBO;
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

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pVBO, s_totalVertexSize, vertexDataSize, mesh.vertices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pEBO, s_totalIndexSize, indexDataSize, mesh.indices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

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

    m_meshInfoDirty = true;
}

void Renderer::setSmallObjectThreshold(float threshold) { m_smallObjectThreshold = threshold; }
void Renderer::setLargeObjectThreshold(float threshold) { m_largeObjectThreshold = threshold; }

void Renderer::drawScene(SceneDatabase& sceneDatabase, const Camera& camera) {
    double transformTime = 0, opaqueCullTime = 0, opaqueSortTime = 0, opaqueCommandGenTime = 0;
    double largeObjectCullTime = 0, largeObjectCommandGenTime = 0, depthPrePassTime = 0;
    double opaqueDrawTime = 0, transparentCullTime = 0, transparentSortTime = 0;
    double transparentCommandGenTime = 0, transparentDrawTime = 0, hizMipmapTime = 0, uiTime = 0;
    double largeObjectSortTime = 0;
    std::chrono::high_resolution_clock::time_point start, end;

    auto updateBuffer = [&](GLuint buffer, void* ptr, size_t bufferSize, const auto& data, size_t objectSize) {
        Lit::Log::Info("Updating buffer with {} objects of size {} bytes each.", data.size(), objectSize);
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
        m_diligent->pImmediateContext->WaitForIdle();

        reallocateBuffers(numObjects * 1.5);
    }

    m_currentFrame = (m_currentFrame + 1) % NUM_FRAMES_IN_FLIGHT;
    const int previousFrame = (m_currentFrame + NUM_FRAMES_IN_FLIGHT - 1) % NUM_FRAMES_IN_FLIGHT;

    {
        Diligent::Uint64 FenceValue = m_diligent->FenceValues[m_currentFrame];
        m_diligent->pFences[m_currentFrame]->Wait(FenceValue);
    }

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
    const size_t alignedSceneUniformsSize = (sizeof(SceneUniforms) + 255) & ~255;
    const size_t uboFrameOffset = m_currentFrame * alignedSceneUniformsSize;

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransformStartQuery[m_currentFrame]);

    auto ReadAtomicCounter = [&](Diligent::IBuffer* pAtomicBuffer) -> unsigned int {
        m_diligent->pImmediateContext->CopyBuffer(pAtomicBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, m_diligent->pStagingBuffer, 0, sizeof(unsigned int), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pImmediateContext->WaitForIdle();
        void* pData = nullptr;
        m_diligent->pImmediateContext->MapBuffer(m_diligent->pStagingBuffer, Diligent::MAP_READ, Diligent::MAP_FLAG_NONE, pData);
        unsigned int count = 0;
        if (pData) {
            count = *reinterpret_cast<unsigned int*>(pData);
        }
        m_diligent->pImmediateContext->UnmapBuffer(m_diligent->pStagingBuffer, Diligent::MAP_READ);
        return count;
    };

    if (m_processedDataVersion < sceneDatabase.m_dataVersion) {
        m_dataUpdateCounter = NUM_FRAMES_IN_FLIGHT;
        m_processedDataVersion = sceneDatabase.m_dataVersion;
    }

    glBindVertexArray(0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    while (glGetError() != GL_NO_ERROR)
        ;

    if (m_hierarchyUpdateCounter > 0) {
        const size_t dataSize = sceneDatabase.hierarchies.size() * sizeof(HierarchyComponent);
        const size_t frameOffsetBytes = m_currentFrame * m_maxObjects * sizeof(HierarchyComponent);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pHierarchyBuffer, frameOffsetBytes, dataSize, sceneDatabase.hierarchies.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const size_t sortedHierarchyDataSize = sceneDatabase.sortedHierarchyList.size() * sizeof(unsigned int);
        const size_t sortedHierarchyFrameOffsetBytes = m_currentFrame * m_maxObjects * sizeof(unsigned int);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pSortedHierarchyBuffer, sortedHierarchyFrameOffsetBytes, sortedHierarchyDataSize, sceneDatabase.sortedHierarchyList.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_hierarchyUpdateCounter--;
    }

    if (m_dataUpdateCounter > 0) {
        const size_t dataSize = sceneDatabase.transforms.size() * sizeof(TransformComponent);
        const size_t frameOffsetBytes = m_currentFrame * m_maxObjects * sizeof(TransformComponent);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pObjectBuffer, frameOffsetBytes, dataSize, sceneDatabase.transforms.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const size_t renderableDataSize = sceneDatabase.renderables.size() * sizeof(RenderableComponent);
        const size_t renderablesFrameOffsetBytes = m_currentFrame * m_maxObjects * sizeof(RenderableComponent);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pRenderableBuffer, renderablesFrameOffsetBytes, renderableDataSize, sceneDatabase.renderables.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_dataUpdateCounter--;
    }

    const unsigned int transformWorkgroupSize = 256;
    const unsigned int transformNumWorkgroups = (sceneDatabase.sortedHierarchyList.size() + transformWorkgroupSize - 1) / transformWorkgroupSize;

    struct TransformUniforms {
        unsigned int objectCount;
        unsigned int currentHierarchyLevel;
        unsigned int baseIndex;
        unsigned int padding;
    } transformUniforms;
    transformUniforms.objectCount = (unsigned int)sceneDatabase.sortedHierarchyList.size();

    m_diligent->pImmediateContext->InvalidateState();

    {
        auto* pTransformVar = m_diligent->pTransformSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransformBuffer");
        if (pTransformVar)
            pTransformVar->Set(m_diligent->pObjectBuffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        auto* pHierarchyVar = m_diligent->pTransformSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "HierarchyBuffer");
        if (pHierarchyVar)
            pHierarchyVar->Set(m_diligent->pHierarchyBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        auto* pSortedVar = m_diligent->pTransformSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortedHierarchyBuffer");
        if (pSortedVar)
            pSortedVar->Set(m_diligent->pSortedHierarchyBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        for (uint32_t level = 0; level <= sceneDatabase.m_maxHierarchyDepth; ++level) {
            transformUniforms.currentHierarchyLevel = level;
            transformUniforms.baseIndex = m_currentFrame * m_maxObjects;
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransformUniforms, 0, sizeof(transformUniforms), &transformUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransformPSO);
            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransformSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            Diligent::DispatchComputeAttribs DispatchAttrs;
            DispatchAttrs.ThreadGroupCountX = transformNumWorkgroups;
            DispatchAttrs.ThreadGroupCountY = 1;
            DispatchAttrs.ThreadGroupCountZ = 1;
            m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);
        }

        m_diligent->pImmediateContext->WaitForIdle();

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransformEndQuery[m_currentFrame]);

    if (m_meshInfoDirty) {
        const size_t dataSize = s_meshInfos.size() * sizeof(MeshInfo);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pMeshInfoBuffer, 0, dataSize, s_meshInfos.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_meshInfoDirty = false;
    }

    SceneUniforms sceneUniforms;
    sceneUniforms.projection = camera.getProjectionMatrix();
    sceneUniforms.view = camera.getViewMatrix();
    sceneUniforms.lightPos = glm::vec3(0.0f, 10.0f, 0.0f);
    sceneUniforms.viewPos = camera.getPosition();
    sceneUniforms.lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
    extractFrustumPlanes(sceneUniforms.projection * sceneUniforms.view, sceneUniforms.frustumPlanes);

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pSceneUBO, uboFrameOffset, sizeof(SceneUniforms), &sceneUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pSceneUBO, uboFrameOffset, sizeof(SceneUniforms), &sceneUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const unsigned int workgroupSize = 256;
    const unsigned int numWorkgroups = (numObjects + workgroupSize - 1) / workgroupSize;
    unsigned int zero = 0;

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pCullStartQuery[m_currentFrame]);

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pVisibleObjectAtomicCounter, 0, sizeof(unsigned int), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_diligent->pImmediateContext->WaitForIdle();

    m_objectBuffer = (unsigned int)(size_t)m_diligent->pObjectBuffer->GetNativeHandle();
    m_renderableBuffer = (unsigned int)(size_t)m_diligent->pRenderableBuffer->GetNativeHandle();
    m_visibleObjectBuffer = (unsigned int)(size_t)m_diligent->pVisibleObjectBuffer->GetNativeHandle();
    m_visibleObjectAtomicCounter = (unsigned int)(size_t)m_diligent->pVisibleObjectAtomicCounter->GetNativeHandle();
    m_hierarchyBuffer = (unsigned int)(size_t)m_diligent->pHierarchyBuffer->GetNativeHandle();
    m_sortedHierarchyBuffer = (unsigned int)(size_t)m_diligent->pSortedHierarchyBuffer->GetNativeHandle();
    m_visibleTransparentObjectIdsBuffer = (unsigned int)(size_t)m_diligent->pVisibleTransparentObjectIdsBuffer->GetNativeHandle();
    m_transparentAtomicCounter = (unsigned int)(size_t)m_diligent->pTransparentAtomicCounter->GetNativeHandle();
    m_drawAtomicCounterBuffer = (unsigned int)(size_t)m_diligent->pDrawAtomicCounterBuffer->GetNativeHandle();
    m_drawCommandBuffer = (unsigned int)(size_t)m_diligent->pDrawCommandBuffer->GetNativeHandle();
    m_transparentDrawCommandBuffer = (unsigned int)(size_t)m_diligent->pTransparentDrawCommandBuffer->GetNativeHandle();
    m_depthPrepassDrawCommandBuffer = (unsigned int)(size_t)m_diligent->pDepthPrepassDrawCommandBuffer->GetNativeHandle();
    m_visibleLargeObjectBuffer = (unsigned int)(size_t)m_diligent->pVisibleLargeObjectBuffer->GetNativeHandle();
    m_visibleLargeObjectAtomicCounter = (unsigned int)(size_t)m_diligent->pVisibleLargeObjectAtomicCounter->GetNativeHandle();
    m_depthPrepassAtomicCounter = (unsigned int)(size_t)m_diligent->pDepthPrepassAtomicCounter->GetNativeHandle();

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_hizTexture[i] = (unsigned int)(size_t)m_diligent->pHiZTextures[i]->GetNativeHandle();
    }

    m_cullingShader->bind();
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, (GLuint)(size_t)m_diligent->pSceneUBO->GetNativeHandle(), uboFrameOffset, sizeof(SceneUniforms));
    m_cullingShader->setUniform("u_objectCount", numObjects);
    m_cullingShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);
    m_cullingShader->setUniform("u_smallObjectThreshold", m_smallObjectThreshold);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hizTexture[previousFrame]);
    m_cullingShader->setUniform("u_hizTexture", 0);
    m_cullingShader->setUniform("u_hizTextureSize", glm::vec2(m_windowWidth, m_windowHeight));
    m_cullingShader->setUniform("u_hizMaxMipLevel", static_cast<float>(m_maxMipLevel - 1));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_visibleObjectAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, (GLuint)(size_t)m_diligent->pMeshInfoBuffer->GetNativeHandle());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

    glDispatchCompute(numWorkgroups, 1, 1);

    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    unsigned int visibleObjectCount = ReadAtomicCounter(m_diligent->pVisibleObjectAtomicCounter);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pCullEndQuery[m_currentFrame]);

    if (visibleObjectCount > 1) {
        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pOpaqueSortStartQuery[m_currentFrame]);
        m_opaqueSortShader->bind();
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

        const unsigned int numElements = nextPowerOfTwo(visibleObjectCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {
                m_opaqueSortShader->setUniform("u_sort_k", k);
                m_opaqueSortShader->setUniform("u_sort_j", j);
                const unsigned int numSortWorkgroups = (numElements + 1023) / 1024;
                glDispatchCompute(numSortWorkgroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }

        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pOpaqueSortEndQuery[m_currentFrame]);
        m_diligent->OpaqueSortActive[m_currentFrame] = true;
    } else {
        m_diligent->OpaqueSortActive[m_currentFrame] = false;
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pCommandGenStartQuery[m_currentFrame]);
    std::vector<unsigned int> drawZeros(m_numDrawingShaders, 0);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pDrawAtomicCounterBuffer, 0, sizeof(unsigned int) * m_numDrawingShaders, drawZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_commandGenShader->bind();
    m_commandGenShader->setUniform("u_visibleObjectCount", visibleObjectCount);
    m_commandGenShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_drawAtomicCounterBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_drawCommandBuffer, frameOffset * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders, m_maxObjects * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, (GLuint)(size_t)m_diligent->pMeshInfoBuffer->GetNativeHandle());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));

    if (visibleObjectCount > 0) {
        glDispatchCompute(1, 1, 1);
    }
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pCommandGenEndQuery[m_currentFrame]);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectCullStartQuery[m_currentFrame]);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pVisibleLargeObjectAtomicCounter, 0, sizeof(unsigned int), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_largeObjectCullShader->bind();
    m_largeObjectCullShader->setUniform("u_objectCount", numObjects);
    m_largeObjectCullShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);
    m_largeObjectCullShader->setUniform("u_largeObjectThreshold", m_largeObjectThreshold);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_visibleLargeObjectAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, (GLuint)(size_t)m_diligent->pMeshInfoBuffer->GetNativeHandle());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

    glDispatchCompute(numWorkgroups, 1, 1);
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    unsigned int visibleLargeObjectCount = ReadAtomicCounter(m_diligent->pVisibleLargeObjectAtomicCounter);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectCullEndQuery[m_currentFrame]);

    if (visibleLargeObjectCount > 1) {
        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectSortStartQuery[m_currentFrame]);
        m_largeObjectSortShader->bind();
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

        const unsigned int numElements = nextPowerOfTwo(visibleLargeObjectCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {
                m_largeObjectSortShader->setUniform("u_sort_k", k);
                m_largeObjectSortShader->setUniform("u_sort_j", j);
                const unsigned int numSortWorkgroups = (numElements + 1023) / 1024;
                glDispatchCompute(numSortWorkgroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }

        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectSortEndQuery[m_currentFrame]);
        m_diligent->LargeObjectSortActive[m_currentFrame] = true;
    } else {
        m_diligent->LargeObjectSortActive[m_currentFrame] = false;
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectCommandGenStartQuery[m_currentFrame]);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pDepthPrepassAtomicCounter, 0, sizeof(unsigned int), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_largeObjectCommandGenShader->bind();
    m_largeObjectCommandGenShader->setUniform("u_visibleLargeObjectCount", visibleLargeObjectCount);
    m_largeObjectCommandGenShader->setUniform("u_maxDraws", (unsigned int)m_maxObjects);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_depthPrepassAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_depthPrepassDrawCommandBuffer, frameOffset * sizeof(DrawElementsIndirectCommand), m_maxObjects * sizeof(DrawElementsIndirectCommand));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, (GLuint)(size_t)m_diligent->pMeshInfoBuffer->GetNativeHandle());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));

    if (visibleLargeObjectCount > 0) {
        glDispatchCompute(1, 1, 1);
    }
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectCommandGenEndQuery[m_currentFrame]);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pDepthPrePassStartQuery[m_currentFrame]);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthFbo[m_currentFrame]);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glBindBuffer(GL_PARAMETER_BUFFER, m_depthPrepassAtomicCounter);

    m_depthPrepassShader->bind();
    glBindVertexArray(m_vao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_depthPrepassDrawCommandBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleLargeObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));
    glMultiDrawElementsIndirectCount(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)(frameOffset * sizeof(DrawElementsIndirectCommand)), 0, m_maxObjects, sizeof(DrawElementsIndirectCommand));

    glEnable(GL_DEPTH_TEST);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pDepthPrePassEndQuery[m_currentFrame]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawCommandBuffer);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_visibleObjectBuffer, frameOffset * sizeof(unsigned int), m_maxObjects * sizeof(unsigned int));

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pOpaqueDrawStartQuery[m_currentFrame]);
    for (uint32_t shaderId = 0; shaderId < m_numDrawingShaders; ++shaderId) {
        Shader* shader = m_shaderManager.getShader(shaderId);
        if (shader) {
            shader->bind();
            const size_t indirect_offset = (m_currentFrame * m_numDrawingShaders * m_maxObjects + shaderId * m_maxObjects) * sizeof(DrawElementsIndirectCommand);
            glBindBuffer(GL_PARAMETER_BUFFER, m_drawAtomicCounterBuffer);
            glMultiDrawElementsIndirectCount(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)(indirect_offset), shaderId * sizeof(unsigned int), m_maxObjects, sizeof(DrawElementsIndirectCommand));
        }
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pOpaqueDrawEndQuery[m_currentFrame]);

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentAtomicCounter, 0, sizeof(unsigned int), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentCullStartQuery[m_currentFrame]);
    m_transparentCullShader->bind();
    m_transparentCullShader->setUniform("u_objectCount", numObjects);
    m_transparentCullShader->setUniform("u_cameraPos", camera.getPosition());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transparentAtomicCounter);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectIdsBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, m_objectBuffer, frameOffset * sizeof(TransformComponent), m_maxObjects * sizeof(TransformComponent));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, (GLuint)(size_t)m_diligent->pMeshInfoBuffer->GetNativeHandle());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));

    glDispatchCompute(numWorkgroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentCullEndQuery[m_currentFrame]);

    unsigned int visibleTransparentCount = ReadAtomicCounter(m_diligent->pTransparentAtomicCounter);

    if (visibleTransparentCount > 1) {
        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentSortStartQuery[m_currentFrame]);
        m_bitonicSortShader->bind();
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectIdsBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));

        const unsigned int numElements = nextPowerOfTwo(visibleTransparentCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {
                m_bitonicSortShader->setUniform("u_sort_k", k);
                m_bitonicSortShader->setUniform("u_sort_j", j);
                const unsigned int numWorkgroups = (numElements + 1023) / 1024;
                glDispatchCompute(numWorkgroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }

        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentSortEndQuery[m_currentFrame]);
        m_diligent->TransparentSortActive[m_currentFrame] = true;
    } else {
        m_diligent->TransparentSortActive[m_currentFrame] = false;
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentCommandGenStartQuery[m_currentFrame]);
    m_transparentCommandGenShader->bind();
    m_transparentCommandGenShader->setUniform("u_visibleTransparentCount", visibleTransparentCount);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, m_visibleTransparentObjectIdsBuffer, frameOffset * sizeof(VisibleTransparentObject), m_maxObjects * sizeof(VisibleTransparentObject));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, (GLuint)(size_t)m_diligent->pMeshInfoBuffer->GetNativeHandle());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 4, m_renderableBuffer, frameOffset * sizeof(RenderableComponent), m_maxObjects * sizeof(RenderableComponent));
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 5, m_transparentDrawCommandBuffer, frameOffset * sizeof(DrawElementsIndirectCommand), m_maxObjects * sizeof(DrawElementsIndirectCommand));

    const unsigned int transparentWorkgroups = (visibleTransparentCount + workgroupSize - 1) / workgroupSize;
    if (visibleTransparentCount > 0) {
        glDispatchCompute(transparentWorkgroups, 1, 1);
    }
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentCommandGenEndQuery[m_currentFrame]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentDrawStartQuery[m_currentFrame]);
    Shader* transparentShader = m_shaderManager.getShader(2);
    if (transparentShader && visibleTransparentCount > 0) {
        transparentShader->bind();
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_transparentDrawCommandBuffer);
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)(frameOffset * sizeof(DrawElementsIndirectCommand)), visibleTransparentCount, sizeof(DrawElementsIndirectCommand));

        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentDrawEndQuery[m_currentFrame]);
        m_diligent->TransparentDrawActive[m_currentFrame] = true;
    } else {
        m_diligent->TransparentDrawActive[m_currentFrame] = false;
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(0);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pHizMipmapStartQuery[m_currentFrame]);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    if (m_diligent->pHiZMipmapPSO) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pHiZMipmapPSO);
        for (int i = 1; i < m_maxMipLevel; ++i) {
            Diligent::TextureViewDesc SourceViewDesc;
            SourceViewDesc.ViewType = Diligent::TEXTURE_VIEW_UNORDERED_ACCESS;
            SourceViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_READ;
            SourceViewDesc.MostDetailedMip = i - 1;
            SourceViewDesc.NumMipLevels = 1;
            Diligent::RefCntAutoPtr<Diligent::ITextureView> pSourceView;
            m_diligent->pHiZTextures[m_currentFrame]->CreateView(SourceViewDesc, &pSourceView);

            Diligent::TextureViewDesc DestViewDesc;
            DestViewDesc.ViewType = Diligent::TEXTURE_VIEW_UNORDERED_ACCESS;
            DestViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_WRITE;
            DestViewDesc.MostDetailedMip = i;
            DestViewDesc.NumMipLevels = 1;
            Diligent::RefCntAutoPtr<Diligent::ITextureView> pDestView;
            m_diligent->pHiZTextures[m_currentFrame]->CreateView(DestViewDesc, &pDestView);

            m_diligent->pHiZMipmapSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "u_sourceMip")->Set(pSourceView);
            m_diligent->pHiZMipmapSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "u_destMip")->Set(pDestView);

            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pHiZMipmapSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            const int currentMipWidth = std::max(1, m_windowWidth >> i);
            const int currentMipHeight = std::max(1, m_windowHeight >> i);

            Diligent::DispatchComputeAttribs DispatchAttrs;
            DispatchAttrs.ThreadGroupCountX = (currentMipWidth + 7) / 8;
            DispatchAttrs.ThreadGroupCountY = (currentMipHeight + 7) / 8;
            DispatchAttrs.ThreadGroupCountZ = 1;

            m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);

            Diligent::StateTransitionDesc Barrier;
            Barrier.pResource = m_diligent->pHiZTextures[m_currentFrame];
            Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            Barrier.NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
            Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
            m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);
        }
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pHizMipmapEndQuery[m_currentFrame]);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pUiStartQuery[m_currentFrame]);

    m_uiManager->render();

    if (fullProfiling) {
        glFinish();
        end = std::chrono::high_resolution_clock::now();
        uiTime = std::chrono::duration<double, std::milli>(end - start).count();
    }
    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pUiEndQuery[m_currentFrame]);
    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pFrameEndQuery[m_currentFrame]);
    m_diligent->QueryReady[m_currentFrame] = true;

    while (glGetError() != GL_NO_ERROR) {
    }

    m_diligent->CurrentFenceValue++;
    m_diligent->pImmediateContext->EnqueueSignal(m_diligent->pFences[m_currentFrame], m_diligent->CurrentFenceValue);
    m_diligent->FenceValues[m_currentFrame] = m_diligent->CurrentFenceValue;

    if (fullProfiling) {
        glFinish();

        auto GetQueryData = [&](Diligent::IQuery* pStartQuery, Diligent::IQuery* pEndQuery) -> double {
            Diligent::QueryDataTimestamp StartData = {};
            Diligent::QueryDataTimestamp EndData = {};

            pStartQuery->GetData(&StartData, sizeof(StartData));
            pEndQuery->GetData(&EndData, sizeof(EndData));
            if (EndData.Counter > StartData.Counter) {
                return (double)(EndData.Counter - StartData.Counter) / 1000000.0;
            }

            Diligent::Uint64 Frequency = 1000000000;

            return (double)(EndData.Counter - StartData.Counter) / 1000000.0;
        };

        auto GetQueryDataRef = [&](Diligent::IQuery* pStartQuery, Diligent::IQuery* pEndQuery) -> double {
            Diligent::QueryDataTimestamp StartData = {};
            Diligent::QueryDataTimestamp EndData = {};

            while (!pStartQuery->GetData(&StartData, sizeof(StartData))) {
            }
            while (!pEndQuery->GetData(&EndData, sizeof(EndData))) {
            }

            if (EndData.Counter > StartData.Counter) {
                return (double)(EndData.Counter - StartData.Counter) / (double)EndData.Frequency * 1000.0;
            }
            return 0.0;
        };

        transformTime = GetQueryDataRef(m_diligent->pTransformStartQuery[m_currentFrame], m_diligent->pTransformEndQuery[m_currentFrame]);
        opaqueCullTime = GetQueryDataRef(m_diligent->pCullStartQuery[m_currentFrame], m_diligent->pCullEndQuery[m_currentFrame]);
        if (m_diligent->OpaqueSortActive[m_currentFrame])
            opaqueSortTime = GetQueryDataRef(m_diligent->pOpaqueSortStartQuery[m_currentFrame], m_diligent->pOpaqueSortEndQuery[m_currentFrame]);
        else
            opaqueSortTime = 0.0;

        opaqueCommandGenTime = GetQueryDataRef(m_diligent->pCommandGenStartQuery[m_currentFrame], m_diligent->pCommandGenEndQuery[m_currentFrame]);
        largeObjectCullTime = GetQueryDataRef(m_diligent->pLargeObjectCullStartQuery[m_currentFrame], m_diligent->pLargeObjectCullEndQuery[m_currentFrame]);

        if (m_diligent->LargeObjectSortActive[m_currentFrame])
            largeObjectSortTime = GetQueryDataRef(m_diligent->pLargeObjectSortStartQuery[m_currentFrame], m_diligent->pLargeObjectSortEndQuery[m_currentFrame]);
        else
            largeObjectSortTime = 0.0;

        largeObjectCommandGenTime = GetQueryDataRef(m_diligent->pLargeObjectCommandGenStartQuery[m_currentFrame], m_diligent->pLargeObjectCommandGenEndQuery[m_currentFrame]);
        depthPrePassTime = GetQueryDataRef(m_diligent->pDepthPrePassStartQuery[m_currentFrame], m_diligent->pDepthPrePassEndQuery[m_currentFrame]);
        opaqueDrawTime = GetQueryDataRef(m_diligent->pOpaqueDrawStartQuery[m_currentFrame], m_diligent->pOpaqueDrawEndQuery[m_currentFrame]);

        transparentCullTime = GetQueryDataRef(m_diligent->pTransparentCullStartQuery[m_currentFrame], m_diligent->pTransparentCullEndQuery[m_currentFrame]);
        if (m_diligent->TransparentSortActive[m_currentFrame])
            transparentSortTime = GetQueryDataRef(m_diligent->pTransparentSortStartQuery[m_currentFrame], m_diligent->pTransparentSortEndQuery[m_currentFrame]);
        else
            transparentSortTime = 0.0;

        transparentCommandGenTime = GetQueryDataRef(m_diligent->pTransparentCommandGenStartQuery[m_currentFrame], m_diligent->pTransparentCommandGenEndQuery[m_currentFrame]);

        if (m_diligent->TransparentDrawActive[m_currentFrame])
            transparentDrawTime = GetQueryDataRef(m_diligent->pTransparentDrawStartQuery[m_currentFrame], m_diligent->pTransparentDrawEndQuery[m_currentFrame]);
        else
            transparentDrawTime = 0.0;

        hizMipmapTime = GetQueryDataRef(m_diligent->pHizMipmapStartQuery[m_currentFrame], m_diligent->pHizMipmapEndQuery[m_currentFrame]);
        uiTime = GetQueryDataRef(m_diligent->pUiStartQuery[m_currentFrame], m_diligent->pUiEndQuery[m_currentFrame]);

        Lit::Log::Debug("--- Full Profiling (GPU Queries) ---");
        Lit::Log::Debug("Sum: {} ms", transformTime + opaqueCullTime + opaqueSortTime + opaqueCommandGenTime + largeObjectCullTime + largeObjectSortTime + largeObjectCommandGenTime + depthPrePassTime + opaqueDrawTime + transparentCullTime + transparentSortTime + transparentCommandGenTime + transparentDrawTime + hizMipmapTime + uiTime);
        Lit::Log::Debug("Transform: {} ms", transformTime);
        Lit::Log::Debug("Opaque Cull: {} ms", opaqueCullTime);
        Lit::Log::Debug("Opaque Sort: {} ms", opaqueSortTime);
        Lit::Log::Debug("Opaque Command Generation: {} ms", opaqueCommandGenTime);
        Lit::Log::Debug("Large Object Culling: {} ms", largeObjectCullTime);
        Lit::Log::Debug("Large Object Sort: {} ms", largeObjectSortTime);
        Lit::Log::Debug("Large Object Command Generation: {} ms", largeObjectCommandGenTime);
        Lit::Log::Debug("Depth Pre-Pass: {} ms", depthPrePassTime);
        Lit::Log::Debug("Opaque Draw: {} ms", opaqueDrawTime);
        Lit::Log::Debug("Transparent Cull: {} ms", transparentCullTime);
        Lit::Log::Debug("Transparent Sort: {} ms", transparentSortTime);
        Lit::Log::Debug("Transparent Command Generation: {} ms", transparentCommandGenTime);
        Lit::Log::Debug("Transparent Draw: {} ms", transparentDrawTime);
        Lit::Log::Debug("Hi-Z Mipmap Generation: {} ms", hizMipmapTime);
        Lit::Log::Debug("UI Rendering: {} ms", uiTime);
    }
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

void Renderer::createTransformPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/transform.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load transform compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) {
            source = source.substr(nextLine + 1);
        }
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Transform compute shader";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create transform compute shader.");
        return;
    }

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Transform compute PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;

    PSOCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pTransformPSO);
    if (!m_diligent->pTransformPSO) {
        Lit::Log::Error("Failed to create transform compute PSO.");
        return;
    }

    Diligent::BufferDesc BuffDesc;
    BuffDesc.Name = "Transform parameters UBO";
    BuffDesc.Usage = Diligent::USAGE_DEFAULT;
    BuffDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    BuffDesc.Size = 256;
    m_diligent->pDevice->CreateBuffer(BuffDesc, nullptr, &m_diligent->pTransformUniforms);
}

void Renderer::createHiZPSO() {
    Diligent::ComputePipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Hi-Z Mipmap PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "u_sourceMip", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {Diligent::SHADER_TYPE_COMPUTE, "u_destMip", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};

    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    std::string source = LoadSourceFromFile("resources/shaders/hiz_mipmap.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load Hi-Z Mipmap compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) {
            source = source.substr(nextLine + 1);
        }
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Hi-Z Mipmap CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create Hi-Z Mipmap shader.");
        return;
    }
    PSOCreateInfo.pCS = pCS;

    m_diligent->pDevice->CreateComputePipelineState(PSOCreateInfo, &m_diligent->pHiZMipmapPSO);
    if (!m_diligent->pHiZMipmapPSO) {
        Lit::Log::Error("Failed to create Hi-Z Mipmap PSO.");
        return;
    }
    m_diligent->pHiZMipmapPSO->CreateShaderResourceBinding(&m_diligent->pHiZMipmapSRB, true);
}