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
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

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

struct MeshInfo {
    unsigned int indexCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    float boundingRadius;
    alignas(16) glm::vec4 boundingCenter;
};

struct SceneUniforms {
    glm::mat4 projection;
    glm::mat4 view;
    alignas(16) glm::vec3 lightPos;
    alignas(16) glm::vec3 viewPos;
    alignas(16) glm::vec3 lightColor;
    alignas(16) glm::vec4 frustumPlanes[6];
};

struct VisibleTransparentObject {
    unsigned int objectId;
    float distance;
};

struct CullingUniforms {
    uint32_t objectCount;
    uint32_t maxDraws;
    uint32_t baseIndex;
    float smallObjectThreshold;
    float hizMaxMipLevel;
    float hizTextureSizeX;
    float hizTextureSizeY;
    uint32_t padding;
};

struct CommandGenUniforms {
    uint32_t visibleObjectCount;
    uint32_t maxDraws;
    uint32_t padding0;
    uint32_t padding1;
};

struct SortConstants {
    uint32_t k;
    uint32_t j;
    uint32_t padding0;
    uint32_t padding1;
};

struct LargeObjectCullUniforms {
    uint32_t objectCount;
    uint32_t maxDraws;
    float largeObjectThreshold;
    uint32_t padding;
};

struct TransparentCommandGenUniforms {
    unsigned int visibleTransparentCount;
    unsigned int padding0;
    unsigned int padding1;
    unsigned int padding2;
};

struct TransparentCullUniforms {
    uint32_t objectCount;
    float padding0;
    float padding1;
    float padding2;
    alignas(16) glm::vec3 cameraPos;
    float padding3;
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

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateStructuredBuffer(Diligent::IRenderDevice* pDevice, const char* name, Diligent::Uint32 elementSize, Diligent::Uint32 elementCount, void* pInitData = nullptr) {
    Diligent::BufferDesc Desc;
    Desc.Name = name;
    Desc.Usage = Diligent::USAGE_DEFAULT;
    Desc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    Desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = elementSize;
    Desc.Size = elementSize * elementCount;

    Diligent::BufferData InitData;
    if (pInitData) {
        InitData.pData = pInitData;
        InitData.DataSize = Desc.Size;
    }

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    pDevice->CreateBuffer(Desc, pInitData ? &InitData : nullptr, &pBuffer);
    return pBuffer;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateIndirectBuffer(Diligent::IRenderDevice* pDevice, const char* name, size_t size) {
    Diligent::BufferDesc Desc;
    Desc.Name = name;
    Desc.Usage = Diligent::USAGE_DEFAULT;
    Desc.BindFlags = Diligent::BIND_INDIRECT_DRAW_ARGS | Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    Desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = 20;
    Desc.Size = size;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    pDevice->CreateBuffer(Desc, nullptr, &pBuffer);
    return pBuffer;
}

} // namespace

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateVertexBuffer(Diligent::IRenderDevice* pDevice, size_t size) {
    Diligent::BufferDesc Desc;
    Desc.Name = "VBO";
    Desc.Usage = Diligent::USAGE_DEFAULT;
    Desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    Desc.Size = size;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    pDevice->CreateBuffer(Desc, nullptr, &pBuffer);
    return pBuffer;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateIndexBuffer(Diligent::IRenderDevice* pDevice, size_t size) {
    Diligent::BufferDesc Desc;
    Desc.Name = "EBO";
    Desc.Usage = Diligent::USAGE_DEFAULT;
    Desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    Desc.Size = size;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    pDevice->CreateBuffer(Desc, nullptr, &pBuffer);
    return pBuffer;
}

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

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pHiZMipmapSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pCullingSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pOpaqueSortSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pCommandGenSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargeObjectCullSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargeObjectSortSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentCullSRB;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pHiZMipmapPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCullingPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pOpaqueSortPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCommandGenPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pLargeObjectCullPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pLargeObjectSortPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentCullPSO;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pStagingBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pOpaqueSortConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCullingUniforms;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCommandGenConstants;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentSortSRB;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentSortPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentSortConstants;

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentCommandGenSRB;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentCommandGenPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCommandGenUniforms;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeObjectCullConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeObjectSortConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCullUniforms;
};

Renderer::Renderer()
    : m_cullingShader(nullptr), m_transparentCullShader(nullptr), m_bitonicSortShader(nullptr),
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
    createCullingPSO();
    createOpaqueSortPSO();
    createCommandGenPSO();
    createLargeObjectCullPSO();
    createLargeObjectSortPSO();
    createTransparentCullPSO();

    if (m_diligent->pTransparentCullUniforms == nullptr) {
        Diligent::BufferDesc CBDesc;
        CBDesc.Name = "Transparent Cull Uniforms";
        CBDesc.Usage = Diligent::USAGE_DEFAULT;
        CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        CBDesc.Size = sizeof(TransparentCullUniforms);
        m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pTransparentCullUniforms);
    }

    createTransparentSortPSO();
    if (m_diligent->pTransparentSortConstants == nullptr) {
        Diligent::BufferDesc CBDesc;
        CBDesc.Name = "Transparent Sort Constants";
        CBDesc.Usage = Diligent::USAGE_DEFAULT;
        CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        CBDesc.Size = sizeof(SortConstants);
        m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pTransparentSortConstants);
    }

    createTransparentCommandGenPSO();
    if (m_diligent->pTransparentCommandGenUniforms == nullptr) {
        Diligent::BufferDesc CBDesc;
        CBDesc.Name = "Transparent Command Gen Uniforms";
        CBDesc.Usage = Diligent::USAGE_DEFAULT;
        CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        CBDesc.Size = sizeof(TransparentCommandGenUniforms);
        m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pTransparentCommandGenUniforms);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    const unsigned int zero = 0;
    m_diligent->pVisibleObjectAtomicCounter = CreateStructuredBuffer(m_diligent->pDevice, "Visible Object Atomic Counter", sizeof(unsigned int), 1, (void*)&zero);
    m_visibleObjectAtomicCounter = (GLuint)(size_t)m_diligent->pVisibleObjectAtomicCounter->GetNativeHandle();

    std::vector<unsigned int> drawZeros(m_shaderManager.getShaderCount(), 0);
    m_diligent->pDrawAtomicCounterBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Draw Atomic Counter Buffer", sizeof(unsigned int), m_shaderManager.getShaderCount(), drawZeros.data());
    m_drawAtomicCounterBuffer = (GLuint)(size_t)m_diligent->pDrawAtomicCounterBuffer->GetNativeHandle();

    m_diligent->pVisibleLargeObjectAtomicCounter = CreateStructuredBuffer(m_diligent->pDevice, "Visible Large Object Atomic Counter", sizeof(unsigned int), 1, (void*)&zero);
    m_visibleLargeObjectAtomicCounter = (GLuint)(size_t)m_diligent->pVisibleLargeObjectAtomicCounter->GetNativeHandle();

    m_maxObjects = 1000000;
    reallocateBuffers(m_maxObjects);

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

    m_diligent->pTransparentAtomicCounter = CreateStructuredBuffer(m_diligent->pDevice, "Transparent Atomic Counter", sizeof(unsigned int), 1, (void*)&zero);
    m_transparentAtomicCounter = (GLuint)(size_t)m_diligent->pTransparentAtomicCounter->GetNativeHandle();

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

    m_diligent->pDepthPrepassAtomicCounter = CreateStructuredBuffer(m_diligent->pDevice, "Depth Prepass Atomic Counter", sizeof(unsigned int), 1, (void*)&zero);
    m_depthPrepassAtomicCounter = (GLuint)(size_t)m_diligent->pDepthPrepassAtomicCounter->GetNativeHandle();

    Diligent::BufferDesc StagingDesc;
    StagingDesc.Name = "Staging Buffer";
    StagingDesc.Usage = Diligent::USAGE_STAGING;
    StagingDesc.BindFlags = Diligent::BIND_NONE;
    StagingDesc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    StagingDesc.Size = sizeof(unsigned int);
    m_diligent->pStagingBuffer.Release();
    m_diligent->pDevice->CreateBuffer(StagingDesc, nullptr, &m_diligent->pStagingBuffer);

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
    m_diligent->pObjectBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Object Buffer", sizeof(TransformComponent), m_maxObjects * NUM_FRAMES_IN_FLIGHT);
    m_objectBuffer = (GLuint)(size_t)m_diligent->pObjectBuffer->GetNativeHandle();

    m_hierarchyBufferSize = m_maxObjects * sizeof(HierarchyComponent) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pHierarchyBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Hierarchy Buffer", sizeof(HierarchyComponent), m_maxObjects * NUM_FRAMES_IN_FLIGHT);
    m_hierarchyBuffer = (GLuint)(size_t)m_diligent->pHierarchyBuffer->GetNativeHandle();

    m_renderableBufferSize = m_maxObjects * sizeof(RenderableComponent) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pRenderableBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Renderable Buffer", sizeof(RenderableComponent), m_maxObjects * NUM_FRAMES_IN_FLIGHT);
    m_renderableBuffer = (GLuint)(size_t)m_diligent->pRenderableBuffer->GetNativeHandle();

    m_sortedHierarchyBufferSize = m_maxObjects * sizeof(unsigned int) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pSortedHierarchyBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Sorted Hierarchy Buffer", sizeof(unsigned int), m_maxObjects * NUM_FRAMES_IN_FLIGHT);
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
    m_diligent->pVisibleObjectBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Visible Objects Buffer", sizeof(unsigned int), m_maxObjects * NUM_FRAMES_IN_FLIGHT);
    m_visibleObjectBuffer = (GLuint)(size_t)m_diligent->pVisibleObjectBuffer->GetNativeHandle();

    m_drawCommandBufferSize = m_maxObjects * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pDrawCommandBuffer = CreateIndirectBuffer(m_diligent->pDevice, "Draw Command Buffer", m_drawCommandBufferSize);
    m_drawCommandBuffer = (GLuint)(size_t)m_diligent->pDrawCommandBuffer->GetNativeHandle();

    m_visibleTransparentObjectIdsBufferSize = m_maxObjects * sizeof(VisibleTransparentObject) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pVisibleTransparentObjectIdsBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Visible Transparent Object IDs Buffer", sizeof(VisibleTransparentObject), m_maxObjects * NUM_FRAMES_IN_FLIGHT);
    m_visibleTransparentObjectIdsBuffer = (GLuint)(size_t)m_diligent->pVisibleTransparentObjectIdsBuffer->GetNativeHandle();

    m_transparentDrawCommandBufferSize = m_maxObjects * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pTransparentDrawCommandBuffer = CreateIndirectBuffer(m_diligent->pDevice, "Transparent Draw Command Buffer", m_transparentDrawCommandBufferSize);
    m_transparentDrawCommandBuffer = (GLuint)(size_t)m_diligent->pTransparentDrawCommandBuffer->GetNativeHandle();

    m_depthPrepassDrawCommandBufferSize = m_maxObjects * sizeof(DrawElementsIndirectCommand) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pDepthPrepassDrawCommandBuffer = CreateIndirectBuffer(m_diligent->pDevice, "Depth Pre-pass Draw Command Buffer", m_depthPrepassDrawCommandBufferSize);
    m_depthPrepassDrawCommandBuffer = (GLuint)(size_t)m_diligent->pDepthPrepassDrawCommandBuffer->GetNativeHandle();

    m_visibleLargeObjectBufferSize = m_maxObjects * sizeof(unsigned int) * NUM_FRAMES_IN_FLIGHT;
    m_diligent->pVisibleLargeObjectBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Visible Large Objects Buffer", sizeof(unsigned int), m_maxObjects * NUM_FRAMES_IN_FLIGHT);
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

    m_diligent->pMeshInfoBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Mesh Info Buffer", sizeof(MeshInfo), m_maxObjects);

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

    if (m_diligent->pCullingPSO) {
        m_diligent->pCullingSRB.Release();
        m_diligent->pCullingPSO->CreateShaderResourceBinding(&m_diligent->pCullingSRB, true);
        if (!m_diligent->pCullingSRB) {
            Lit::Log::Error("Failed to create Culling SRB");
            return;
        }

        auto* sceneDataVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SceneData");
        auto* cullingUniformsVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CullingUniforms");
        auto* atomicCounterVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer");
        auto* visibleObjectVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer");
        auto* objectVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "ObjectBuffer");
        auto* meshInfoVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer");
        auto* renderableVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer");

        if (sceneDataVar && m_diligent->pSceneUBO)
            sceneDataVar->Set(m_diligent->pSceneUBO);
        if (cullingUniformsVar && m_diligent->pCullingUniforms)
            cullingUniformsVar->Set(m_diligent->pCullingUniforms);
        if (atomicCounterVar && m_diligent->pVisibleObjectAtomicCounter)
            atomicCounterVar->Set(m_diligent->pVisibleObjectAtomicCounter->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
        if (visibleObjectVar && m_diligent->pVisibleObjectBuffer)
            visibleObjectVar->Set(m_diligent->pVisibleObjectBuffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
        if (objectVar && m_diligent->pObjectBuffer)
            objectVar->Set(m_diligent->pObjectBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
        if (meshInfoVar && m_diligent->pMeshInfoBuffer)
            meshInfoVar->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
        if (renderableVar && m_diligent->pRenderableBuffer)
            renderableVar->Set(m_diligent->pRenderableBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
    }

    if (m_diligent->pCommandGenPSO) {
        m_diligent->pCommandGenSRB.Release();
        m_diligent->pCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pCommandGenSRB, true);

        if (!m_diligent->pCommandGenSRB) {
            Lit::Log::Error("Failed to create CommandGen SRB");
        } else {
            if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer"))
                var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));

            if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer"))
                var->Set(m_diligent->pDrawCommandBuffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));

            if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer"))
                var->Set(m_diligent->pRenderableBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));

            if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer"))
                var->Set(m_diligent->pVisibleObjectBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));

            if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawAtomicCounterBuffer"))
                var->Set(m_diligent->pDrawAtomicCounterBuffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));

            if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CommandGenConstants"))
                var->Set(m_diligent->pCommandGenConstants);
        }
    }

    if (m_diligent->pLargeObjectCullPSO) {
        m_diligent->pLargeObjectCullSRB.Release();
        m_diligent->pLargeObjectCullPSO->CreateShaderResourceBinding(&m_diligent->pLargeObjectCullSRB, true);

        if (m_diligent->pLargeObjectCullSRB) {

            if (auto* var = m_diligent->pLargeObjectCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SceneUniforms"))
                var->Set(m_diligent->pSceneUBO);

            if (auto* var = m_diligent->pLargeObjectCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer"))
                var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));

            if (auto* var = m_diligent->pLargeObjectCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectAtomicCounter"))
                var->Set(m_diligent->pVisibleLargeObjectAtomicCounter->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));

            if (auto* var = m_diligent->pLargeObjectCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "LargeObjectCullConstants"))
                var->Set(m_diligent->pLargeObjectCullConstants);
        }
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

    auto resizeBuffer = [&](Diligent::RefCntAutoPtr<Diligent::IBuffer>& pBuffer, size_t currentSize, size_t newSize,
                            GLuint& nativeHandle, bool isIndexBuffer) {
        Diligent::RefCntAutoPtr<Diligent::IBuffer> pNewBuffer;
        if (isIndexBuffer) {
            pNewBuffer = CreateIndexBuffer(m_diligent->pDevice, newSize);
        } else {
            pNewBuffer = CreateVertexBuffer(m_diligent->pDevice, newSize);
        }

        if (currentSize > 0) {
            m_diligent->pImmediateContext->CopyBuffer(pBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                                      pNewBuffer, 0, currentSize, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        pBuffer = pNewBuffer;
        nativeHandle = (GLuint)(size_t)pBuffer->GetNativeHandle();
    };

    if (s_totalVertexSize + vertexDataSize > m_vboSize || s_totalIndexSize + indexDataSize > m_eboSize) {
        m_vboSize = std::max(m_vboSize * 2, s_totalVertexSize + vertexDataSize);
        m_eboSize = std::max(m_eboSize * 2, s_totalIndexSize + indexDataSize);

        resizeBuffer(m_diligent->pVBO, s_totalVertexSize, m_vboSize, m_vbo, false);
        resizeBuffer(m_diligent->pEBO, s_totalIndexSize, m_eboSize, m_ebo, true);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBindVertexArray(0);
    }

    if (vertexDataSize > 0) {
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pVBO, s_totalVertexSize, vertexDataSize,
                                                    mesh.vertices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    if (indexDataSize > 0) {
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pEBO, s_totalIndexSize, indexDataSize,
                                                    mesh.indices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

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

    auto ResetAtomicCounter = [&](Diligent::IBuffer* pBuffer) {
        unsigned int zero = 0;
        m_diligent->pImmediateContext->UpdateBuffer(pBuffer, 0, sizeof(unsigned int), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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

    const unsigned int workgroupSize = 256;
    const unsigned int numWorkgroups = (numObjects + workgroupSize - 1) / workgroupSize;

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pCullStartQuery[m_currentFrame]);

    ResetAtomicCounter(m_diligent->pVisibleObjectAtomicCounter);

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

    m_diligent->pImmediateContext->InvalidateState();

    CullingUniforms cullingUniforms;

    cullingUniforms.objectCount = numObjects;
    cullingUniforms.maxDraws = static_cast<uint32_t>(m_maxObjects);
    cullingUniforms.baseIndex = m_currentFrame * m_maxObjects;
    cullingUniforms.smallObjectThreshold = m_smallObjectThreshold;
    cullingUniforms.hizMaxMipLevel = static_cast<float>(m_maxMipLevel - 1);
    cullingUniforms.hizTextureSizeX = static_cast<float>(m_windowWidth);
    cullingUniforms.hizTextureSizeY = static_cast<float>(m_windowHeight);
    cullingUniforms.padding = 0;

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pCullingUniforms, 0, sizeof(cullingUniforms), &cullingUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    auto* pHiZVar = m_diligent->pCullingSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "u_hizTexture");
    if (pHiZVar) {
        pHiZVar->Set(m_diligent->pHiZTextures[previousFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    }

    m_diligent->pImmediateContext->SetPipelineState(m_diligent->pCullingPSO);
    m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pCullingSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DispatchComputeAttribs CullDispatchAttrs;
    CullDispatchAttrs.ThreadGroupCountX = numWorkgroups;
    CullDispatchAttrs.ThreadGroupCountY = 1;
    CullDispatchAttrs.ThreadGroupCountZ = 1;
    m_diligent->pImmediateContext->DispatchCompute(CullDispatchAttrs);

    m_diligent->pImmediateContext->WaitForIdle();
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    unsigned int visibleObjectCount = ReadAtomicCounter(m_diligent->pVisibleObjectAtomicCounter);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pCullEndQuery[m_currentFrame]);

    if (visibleObjectCount > 1) {
        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pOpaqueSortStartQuery[m_currentFrame]);

        {
            auto* pVisibleBufVar = m_diligent->pOpaqueSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer");
            if (pVisibleBufVar)
                pVisibleBufVar->Set(m_diligent->pVisibleObjectBuffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));

            auto* pRenderableBufVar = m_diligent->pOpaqueSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer");
            if (pRenderableBufVar)
                pRenderableBufVar->Set(m_diligent->pRenderableBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
        }

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pOpaqueSortPSO);

        const unsigned int numElements = nextPowerOfTwo(visibleObjectCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {

                {
                    Diligent::MapHelper<SortConstants> Constants(m_diligent->pImmediateContext, m_diligent->pOpaqueSortConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
                    Constants->k = k;
                    Constants->j = j;
                }

                m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pOpaqueSortSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                const unsigned int numSortWorkgroups = (numElements + 1023) / 1024;
                Diligent::DispatchComputeAttribs SortDispatchAttrs;
                SortDispatchAttrs.ThreadGroupCountX = numSortWorkgroups;
                m_diligent->pImmediateContext->DispatchCompute(SortDispatchAttrs);

                Diligent::StateTransitionDesc Barrier;
                Barrier.pResource = m_diligent->pVisibleObjectBuffer;
                Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
                Barrier.NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
                Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
                Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
                m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);
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

    if (visibleObjectCount > 0) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pCommandGenPSO);

        {
            Diligent::MapHelper<CommandGenUniforms> ConstData(m_diligent->pImmediateContext, m_diligent->pCommandGenConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->visibleObjectCount = visibleObjectCount;
            ConstData->maxDraws = (uint32_t)m_maxObjects;
        }

        Diligent::BufferViewDesc DrawCmdViewDesc;
        DrawCmdViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
        DrawCmdViewDesc.ByteOffset = frameOffset * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders;
        DrawCmdViewDesc.ByteWidth = m_maxObjects * sizeof(DrawElementsIndirectCommand) * m_numDrawingShaders;
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pDrawCmdView;
        m_diligent->pDrawCommandBuffer->CreateView(DrawCmdViewDesc, &pDrawCmdView);

        if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer"))
            var->Set(pDrawCmdView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        Diligent::BufferViewDesc VisibleObjViewDesc;
        VisibleObjViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        VisibleObjViewDesc.ByteOffset = frameOffset * sizeof(unsigned int);
        VisibleObjViewDesc.ByteWidth = m_maxObjects * sizeof(unsigned int);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pVisibleObjView;
        m_diligent->pVisibleObjectBuffer->CreateView(VisibleObjViewDesc, &pVisibleObjView);

        if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer"))
            var->Set(pVisibleObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        Diligent::BufferViewDesc RenderableViewDesc;
        RenderableViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        RenderableViewDesc.ByteOffset = frameOffset * sizeof(RenderableComponent);
        RenderableViewDesc.ByteWidth = m_maxObjects * sizeof(RenderableComponent);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pRenderableView;
        m_diligent->pRenderableBuffer->CreateView(RenderableViewDesc, &pRenderableView);

        if (auto* var = m_diligent->pCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer"))
            var->Set(pRenderableView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pCommandGenSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));

        Diligent::StateTransitionDesc Barrier;
        Barrier.pResource = m_diligent->pDrawCommandBuffer;
        Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        Barrier.NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
        Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pCommandGenEndQuery[m_currentFrame]);

    while (glGetError() != GL_NO_ERROR)
        ;
    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectCullStartQuery[m_currentFrame]);

    ResetAtomicCounter(m_diligent->pVisibleLargeObjectAtomicCounter);

    m_diligent->pImmediateContext->SetPipelineState(m_diligent->pLargeObjectCullPSO);

    {
        Diligent::MapHelper<LargeObjectCullUniforms> Constants(m_diligent->pImmediateContext, m_diligent->pLargeObjectCullConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        Constants->objectCount = numObjects;
        Constants->maxDraws = (uint32_t)m_maxObjects;
        Constants->largeObjectThreshold = m_largeObjectThreshold;
    }

    Diligent::BufferViewDesc ObjViewDesc;
    ObjViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
    ObjViewDesc.ByteOffset = frameOffset * sizeof(TransformComponent);
    ObjViewDesc.ByteWidth = m_maxObjects * sizeof(TransformComponent);
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pObjView;
    m_diligent->pObjectBuffer->CreateView(ObjViewDesc, &pObjView);
    if (auto* var = m_diligent->pLargeObjectCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "ObjectBuffer"))
        var->Set(pObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    Diligent::BufferViewDesc RenderableViewDesc;
    RenderableViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
    RenderableViewDesc.ByteOffset = frameOffset * sizeof(RenderableComponent);
    RenderableViewDesc.ByteWidth = m_maxObjects * sizeof(RenderableComponent);
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pRenderableView;
    m_diligent->pRenderableBuffer->CreateView(RenderableViewDesc, &pRenderableView);
    if (auto* var = m_diligent->pLargeObjectCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer"))
        var->Set(pRenderableView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    Diligent::BufferViewDesc VisLargeObjViewDesc;
    VisLargeObjViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
    VisLargeObjViewDesc.ByteOffset = frameOffset * sizeof(unsigned int);
    VisLargeObjViewDesc.ByteWidth = m_maxObjects * sizeof(unsigned int);
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pVisLargeObjView;
    m_diligent->pVisibleLargeObjectBuffer->CreateView(VisLargeObjViewDesc, &pVisLargeObjView);
    if (auto* var = m_diligent->pLargeObjectCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer"))
        var->Set(pVisLargeObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pLargeObjectCullSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(numWorkgroups, 1, 1));

    Diligent::StateTransitionDesc Barrier;
    Barrier.pResource = m_diligent->pVisibleLargeObjectAtomicCounter;
    Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier.NewState = Diligent::RESOURCE_STATE_COPY_SOURCE;
    Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);

    unsigned int visibleLargeObjectCount = ReadAtomicCounter(m_diligent->pVisibleLargeObjectAtomicCounter);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectCullEndQuery[m_currentFrame]);

    if (visibleLargeObjectCount > 1) {
        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pLargeObjectSortStartQuery[m_currentFrame]);

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pLargeObjectSortPSO);

        Diligent::BufferViewDesc VisObjViewDesc;
        VisObjViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
        VisObjViewDesc.ByteOffset = frameOffset * sizeof(unsigned int);
        VisObjViewDesc.ByteWidth = m_maxObjects * sizeof(unsigned int);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pVisObjView;
        m_diligent->pVisibleLargeObjectBuffer->CreateView(VisObjViewDesc, &pVisObjView);

        if (auto* var = m_diligent->pLargeObjectSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer"))
            var->Set(pVisObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        Diligent::BufferViewDesc RenderableViewDesc;
        RenderableViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        RenderableViewDesc.ByteOffset = frameOffset * sizeof(RenderableComponent);
        RenderableViewDesc.ByteWidth = m_maxObjects * sizeof(RenderableComponent);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pRenderableView;
        m_diligent->pRenderableBuffer->CreateView(RenderableViewDesc, &pRenderableView);

        if (auto* var = m_diligent->pLargeObjectSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer"))
            var->Set(pRenderableView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        const unsigned int numElements = nextPowerOfTwo(visibleLargeObjectCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {

                {
                    Diligent::MapHelper<SortConstants> Constants(m_diligent->pImmediateContext, m_diligent->pLargeObjectSortConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
                    Constants->k = k;
                    Constants->j = j;
                }

                m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pLargeObjectSortSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                const unsigned int numSortWorkgroups = (numElements + 1023) / 1024;
                m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(numSortWorkgroups, 1, 1));

                Diligent::StateTransitionDesc Barrier;
                Barrier.pResource = m_diligent->pVisibleLargeObjectBuffer;
                Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
                Barrier.NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
                Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
                Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
                m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);
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
    ResetAtomicCounter(m_diligent->pDepthPrepassAtomicCounter);

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

    glBindBufferRange(GL_UNIFORM_BUFFER, 0, (GLuint)(size_t)m_diligent->pSceneUBO->GetNativeHandle(), uboFrameOffset, sizeof(SceneUniforms));

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

    glBindBufferRange(GL_UNIFORM_BUFFER, 0, (GLuint)(size_t)m_diligent->pSceneUBO->GetNativeHandle(), uboFrameOffset, sizeof(SceneUniforms));

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

    ResetAtomicCounter(m_diligent->pTransparentAtomicCounter);

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentCullStartQuery[m_currentFrame]);
    {
        TransparentCullUniforms uniforms;
        uniforms.objectCount = numObjects;
        uniforms.cameraPos = camera.getPosition();
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentCullUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransparentCullPSO);

        if (auto* var = m_diligent->pTransparentCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer"))
            var->Set(m_diligent->pTransparentAtomicCounter->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        Diligent::BufferViewDesc VisTransObjViewDesc;
        VisTransObjViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
        VisTransObjViewDesc.ByteOffset = frameOffset * sizeof(VisibleTransparentObject);
        VisTransObjViewDesc.ByteWidth = m_maxObjects * sizeof(VisibleTransparentObject);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pVisTransObjView;
        m_diligent->pVisibleTransparentObjectIdsBuffer->CreateView(VisTransObjViewDesc, &pVisTransObjView);
        if (auto* var = m_diligent->pTransparentCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer"))
            var->Set(pVisTransObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        Diligent::BufferViewDesc ObjViewDesc;
        ObjViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        ObjViewDesc.ByteOffset = frameOffset * sizeof(TransformComponent);
        ObjViewDesc.ByteWidth = m_maxObjects * sizeof(TransformComponent);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pObjView;
        m_diligent->pObjectBuffer->CreateView(ObjViewDesc, &pObjView);
        if (auto* var = m_diligent->pTransparentCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "ObjectBuffer"))
            var->Set(pObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        if (auto* var = m_diligent->pTransparentCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer"))
            var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        Diligent::BufferViewDesc RenderableViewDesc;
        RenderableViewDesc.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
        RenderableViewDesc.ByteOffset = frameOffset * sizeof(RenderableComponent);
        RenderableViewDesc.ByteWidth = m_maxObjects * sizeof(RenderableComponent);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pRenderableView;
        m_diligent->pRenderableBuffer->CreateView(RenderableViewDesc, &pRenderableView);
        if (auto* var = m_diligent->pTransparentCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer"))
            var->Set(pRenderableView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        if (auto* var = m_diligent->pTransparentCullSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransparentCullUniforms"))
            var->Set(m_diligent->pTransparentCullUniforms, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransparentCullSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DispatchComputeAttribs DispatchAttrs;
        DispatchAttrs.ThreadGroupCountX = numWorkgroups;
        DispatchAttrs.ThreadGroupCountY = 1;
        DispatchAttrs.ThreadGroupCountZ = 1;
        m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);

        Diligent::StateTransitionDesc Barrier;
        Barrier.pResource = m_diligent->pTransparentAtomicCounter;
        Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        Barrier.NewState = Diligent::RESOURCE_STATE_COPY_SOURCE;
        Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);
    }

    while (glGetError() != GL_NO_ERROR)
        ;
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentCullEndQuery[m_currentFrame]);

    unsigned int visibleTransparentCount = ReadAtomicCounter(m_diligent->pTransparentAtomicCounter);

    if (visibleTransparentCount > 1) {
        while (glGetError() != GL_NO_ERROR)
            ;
        m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentSortStartQuery[m_currentFrame]);
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransparentSortPSO);

        Diligent::BufferViewDesc VisTransObjViewDesc;
        VisTransObjViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
        VisTransObjViewDesc.ByteOffset = frameOffset * sizeof(VisibleTransparentObject);
        VisTransObjViewDesc.ByteWidth = m_maxObjects * sizeof(VisibleTransparentObject);
        Diligent::RefCntAutoPtr<Diligent::IBufferView> pVisTransObjView;
        m_diligent->pVisibleTransparentObjectIdsBuffer->CreateView(VisTransObjViewDesc, &pVisTransObjView);
        if (auto* var = m_diligent->pTransparentSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer"))
            var->Set(pVisTransObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        if (auto* var = m_diligent->pTransparentSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortConstants"))
            var->Set(m_diligent->pTransparentSortConstants, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        const unsigned int numElements = nextPowerOfTwo(visibleTransparentCount);

        for (unsigned int k = 2; k <= numElements; k <<= 1) {
            for (unsigned int j = k >> 1; j > 0; j >>= 1) {
                SortConstants constants;
                constants.k = k;
                constants.j = j;
                m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentSortConstants, 0, sizeof(SortConstants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransparentSortSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                Diligent::DispatchComputeAttribs DispatchAttrs;
                DispatchAttrs.ThreadGroupCountX = (numElements + 511) / 512;
                DispatchAttrs.ThreadGroupCountY = 1;
                DispatchAttrs.ThreadGroupCountZ = 1;
                m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);
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
    m_diligent->pImmediateContext->EndQuery(m_diligent->pTransparentCommandGenStartQuery[m_currentFrame]);

    m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransparentCommandGenPSO);

    TransparentCommandGenUniforms uniforms;
    uniforms.visibleTransparentCount = visibleTransparentCount;
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentCommandGenUniforms, 0, sizeof(TransparentCommandGenUniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (auto* var = m_diligent->pTransparentCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransparentCommandGenUniforms"))
        var->Set(m_diligent->pTransparentCommandGenUniforms, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    Diligent::BufferViewDesc VisTransObjViewDesc;
    VisTransObjViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
    VisTransObjViewDesc.ByteOffset = frameOffset * sizeof(VisibleTransparentObject);
    VisTransObjViewDesc.ByteWidth = m_maxObjects * sizeof(VisibleTransparentObject);
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pVisTransObjView;
    m_diligent->pVisibleTransparentObjectIdsBuffer->CreateView(VisTransObjViewDesc, &pVisTransObjView);
    if (auto* var = m_diligent->pTransparentCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer"))
        var->Set(pVisTransObjView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    if (auto* var = m_diligent->pTransparentCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer"))
        var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    RenderableViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
    RenderableViewDesc.ByteOffset = frameOffset * sizeof(RenderableComponent);
    RenderableViewDesc.ByteWidth = m_maxObjects * sizeof(RenderableComponent);
    pRenderableView.Release();
    m_diligent->pRenderableBuffer->CreateView(RenderableViewDesc, &pRenderableView);
    if (auto* var = m_diligent->pTransparentCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer"))
        var->Set(pRenderableView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    Diligent::BufferViewDesc TransCmdViewDesc;
    TransCmdViewDesc.ViewType = Diligent::BUFFER_VIEW_UNORDERED_ACCESS;
    TransCmdViewDesc.ByteOffset = frameOffset * sizeof(DrawElementsIndirectCommand);
    TransCmdViewDesc.ByteWidth = m_maxObjects * sizeof(DrawElementsIndirectCommand);
    Diligent::RefCntAutoPtr<Diligent::IBufferView> pTransCmdView;
    m_diligent->pTransparentDrawCommandBuffer->CreateView(TransCmdViewDesc, &pTransCmdView);
    if (auto* var = m_diligent->pTransparentCommandGenSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransparentDrawCommandBuffer"))
        var->Set(pTransCmdView, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

    m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransparentCommandGenSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const unsigned int transparentWorkgroups = (visibleTransparentCount + workgroupSize - 1) / workgroupSize;
    if (visibleTransparentCount > 0) {
        Diligent::DispatchComputeAttribs DispatchAttrs;
        DispatchAttrs.ThreadGroupCountX = transparentWorkgroups;
        DispatchAttrs.ThreadGroupCountY = 1;
        DispatchAttrs.ThreadGroupCountZ = 1;
        m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);
    }

    Barrier.pResource = m_diligent->pTransparentDrawCommandBuffer;
    Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier.NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
    Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);

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

void Renderer::createTransparentCullPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/transparent_cull.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load transparent cull compute shader source.");
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
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Transparent Cull CS";
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create transparent cull shader");
        return;
    }

    Diligent::ComputePipelineStateCreateInfo PSODesc;
    PSODesc.PSODesc.Name = "Transparent Cull PSO";
    PSODesc.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSODesc.pCS = pCS;

    PSODesc.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_COMPUTE, "TransparentCullUniforms", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "ObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};
    PSODesc.PSODesc.ResourceLayout.Variables = Vars.data();
    PSODesc.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pTransparentCullPSO.Release();
    m_diligent->pDevice->CreateComputePipelineState(PSODesc, &m_diligent->pTransparentCullPSO);
    if (!m_diligent->pTransparentCullPSO) {
        Lit::Log::Error("Failed to create Transparent Cull PSO");
    } else {
        m_diligent->pTransparentCullPSO->CreateShaderResourceBinding(&m_diligent->pTransparentCullSRB, true);
    }
}

void Renderer::createTransparentSortPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/bitonic_sort.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load transparent sort compute shader source.");
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
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Transparent Sort CS";
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create transparent sort shader");
        return;
    }

    Diligent::ComputePipelineStateCreateInfo PSODesc;
    PSODesc.PSODesc.Name = "Transparent Sort PSO";
    PSODesc.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSODesc.pCS = pCS;

    PSODesc.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_COMPUTE, "SortConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};
    PSODesc.PSODesc.ResourceLayout.Variables = Vars.data();
    PSODesc.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pTransparentSortPSO.Release();
    m_diligent->pDevice->CreateComputePipelineState(PSODesc, &m_diligent->pTransparentSortPSO);
    if (!m_diligent->pTransparentSortPSO) {
        Lit::Log::Error("Failed to create Transparent Sort PSO");
    } else {
        m_diligent->pTransparentSortPSO->CreateShaderResourceBinding(&m_diligent->pTransparentSortSRB, true);
    }
}

void Renderer::createTransparentCommandGenPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/transparent_command_gen.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load transparent command gen compute shader source.");
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
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Transparent Command Gen CS";
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create transparent command gen shader");
        return;
    }

    Diligent::ComputePipelineStateCreateInfo PSODesc;
    PSODesc.PSODesc.Name = "Transparent Command Gen PSO";
    PSODesc.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSODesc.pCS = pCS;

    PSODesc.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_COMPUTE, "TransparentCommandGenUniforms", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TransparentDrawCommandBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};
    PSODesc.PSODesc.ResourceLayout.Variables = Vars.data();
    PSODesc.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pTransparentCommandGenPSO.Release();
    m_diligent->pDevice->CreateComputePipelineState(PSODesc, &m_diligent->pTransparentCommandGenPSO);
    if (!m_diligent->pTransparentCommandGenPSO) {
        Lit::Log::Error("Failed to create Transparent Command Gen PSO");
    } else {
        m_diligent->pTransparentCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pTransparentCommandGenSRB, true);
    }
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

void Renderer::createCullingPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/cull.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load culling compute shader source.");
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
    ShaderCI.Desc.Name = "Culling compute shader";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create culling compute shader.");
        return;
    }

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "u_hizTexture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Culling compute PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pCullingPSO);
    if (!m_diligent->pCullingPSO) {
        Lit::Log::Error("Failed to create culling compute PSO.");
        return;
    }

    Diligent::BufferDesc BuffDesc;
    BuffDesc.Name = "Culling parameters UBO";
    BuffDesc.Usage = Diligent::USAGE_DEFAULT;
    BuffDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    BuffDesc.Size = 256;
    m_diligent->pDevice->CreateBuffer(BuffDesc, nullptr, &m_diligent->pCullingUniforms);
}

void Renderer::createOpaqueSortPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/opaque_sort.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load opaque sort compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos)
            source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Opaque Sort CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS)
        return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "SortConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Opaque Sort PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pOpaqueSortPSO);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Sort Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(SortConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pOpaqueSortConstants);

    m_diligent->pOpaqueSortPSO->CreateShaderResourceBinding(&m_diligent->pOpaqueSortSRB, true);

    if (auto* var = m_diligent->pOpaqueSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortConstants"))
        var->Set(m_diligent->pOpaqueSortConstants);
}

void Renderer::createCommandGenPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/command_gen.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load command generation compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos)
            source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Command Gen CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS)
        return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "DrawAtomicCounterBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "CommandGenConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Command Gen PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pCommandGenPSO);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Command Gen Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(CommandGenUniforms);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pCommandGenConstants);
}

void Renderer::createLargeObjectCullPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/large_object_cull.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load large object cull compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos)
            source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Large Object Cull CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS)
        return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "SceneUniforms", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectAtomicCounter", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "ObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "LargeObjectCullConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Large Object Cull PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pLargeObjectCullPSO);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Large Object Cull Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(LargeObjectCullUniforms);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pLargeObjectCullConstants);
}

void Renderer::createLargeObjectSortPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/large_object_sort.comp");
    if (source.empty())
        return;

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos)
            source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Large Object Sort CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to compile Large Object Sort Shader");
        return;
    }

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "SortConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}};

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Large Object Sort PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pLargeObjectSortPSO);
    if (!m_diligent->pLargeObjectSortPSO) {
        Lit::Log::Error("Failed to create Large Object Sort PSO");
        return;
    }

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Large Object Sort Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(SortConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pLargeObjectSortConstants);

    m_diligent->pLargeObjectSortPSO->CreateShaderResourceBinding(&m_diligent->pLargeObjectSortSRB, true);

    if (auto* var = m_diligent->pLargeObjectSortSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortConstants"))
        var->Set(m_diligent->pLargeObjectSortConstants);
}