module;

#include "DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/SwapChain.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Fence.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Query.h"
#include "DiligentCore/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Shader.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Sampler.h"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

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
#include <format>
#include <string_view>
#include "Engine/Log/Log.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
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
    alignas(16) glm::vec4 dirLightDir;
    alignas(16) glm::vec4 dirLightColor;
    alignas(16) glm::vec4 pointLight0Pos;
    alignas(16) glm::vec4 pointLight0Color;
    alignas(16) glm::vec4 pointLight1Pos;
    alignas(16) glm::vec4 pointLight1Color;
    alignas(16) glm::vec4 screenParams;
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
    uint32_t numShaders;
    float lodBias;
    int32_t forcedLod;
};

constexpr uint32_t MAX_MESHES = 2048;

constexpr uint32_t MAX_DIRTY_PER_FRAME = 65536;

constexpr uint32_t INVALID_MESH_UUID = 0xFFFFFFFFu;

struct CommandGenUniforms {
    uint32_t maxBucketsPerShader;
    uint32_t totalBuckets;
    uint32_t padding0;
    uint32_t padding1;
};

struct PrefixSumConstants {
    uint32_t bucketCount;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
};

struct DispatchArgsConstants {
    uint32_t maxCount;
    uint32_t workgroupSize;
    uint32_t padding0;
    uint32_t padding1;
};

struct ScatterConstants {
    uint32_t maxDraws;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
};

struct ApplyDirtyConstants {
    uint32_t dirtyCount;
    uint32_t epoch;
    uint32_t padding0;
    uint32_t padding1;
};

struct MarkTouchedConstants {
    uint32_t dirtyCount;
    uint32_t epoch;
    uint32_t padding0;
    uint32_t padding1;
};

struct LargeObjectCullUniforms {
    uint32_t objectCount;
    uint32_t maxDraws;
    float largeObjectThreshold;
    uint32_t numShaders;
};

struct LargeObjectCommandGenUniforms {
    unsigned int maxDraws;
    unsigned int padding0;
    unsigned int padding1;
    unsigned int padding2;
};

struct TransparentCommandGenUniforms {
    unsigned int maxDraws;
    unsigned int padding0;
    unsigned int padding1;
    unsigned int padding2;
};

struct TransparentCullUniforms {
    uint32_t objectCount;
    uint32_t maxDraws;
    float lodBias;
    int32_t forcedLod;
    alignas(16) glm::vec3 cameraPos;
    float padding3;
};

constexpr uint32_t kNormalSamples = 64;
std::vector<MeshInfo> s_meshInfos;
std::vector<glm::vec4> s_normalSamples;
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
    planes[4] = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    for (int i = 0; i < 6; i++) {
        const glm::vec3 n(planes[i]);
        planes[i] /= glm::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
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

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateStructuredBuffer(Diligent::IRenderDevice* pDevice, const char* name, Diligent::Uint32 elementSize, Diligent::Uint32 elementCount, void* pInitData = nullptr, Diligent::BIND_FLAGS extraFlags = Diligent::BIND_NONE) {
    Diligent::BufferDesc Desc;
    Desc.Name = name;
    Desc.Usage = Diligent::USAGE_DEFAULT;
    Desc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS | extraFlags;
    Desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = elementSize;
    Desc.Size = static_cast<Diligent::Uint64>(elementSize) * static_cast<Diligent::Uint64>(elementCount);

    Diligent::BufferData InitData;
    if (pInitData) {
        InitData.pData = pInitData;
        InitData.DataSize = Desc.Size;
    }

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    pDevice->CreateBuffer(Desc, pInitData ? &InitData : nullptr, &pBuffer);
    if (!pBuffer) { Lit::Log::Error("Failed to allocate structured buffer '{}' ({} bytes).", name, Desc.Size); }
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
    if (!pBuffer) { Lit::Log::Error("Failed to allocate indirect buffer '{}' ({} bytes).", name, size); }
    return pBuffer;
}

}  // namespace

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

struct DebugLineVertex {
    glm::vec3 position;
    glm::vec4 color;
};

struct DiligentData {
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> pDevice;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> pImmediateContext;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> pSwapChain;
    Diligent::IEngineFactoryVk* pFactoryVk = nullptr;

    static constexpr int NumFrames = 3;
    Diligent::RefCntAutoPtr<Diligent::IFence> pFences[NumFrames];
    Diligent::Uint64 FenceValues[NumFrames] = {0};
    Diligent::Uint64 CurrentFenceValue = 0;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pObjectBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pWorldMatrixBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCullSphereBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCullOrientationBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDirtyIndexBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDirtyPayloadBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTouchedEpochBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pHierarchyBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pRenderableBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSortedHierarchyBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleObjectBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSortedVisibleObjectBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBucketCountBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBucketOffsetBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBucketWriteHeadBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDrawCommandBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleTransparentObjectIdsBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentDrawCommandBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleLargeObjectBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDepthPrepassDrawCommandBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSceneUBO[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleObjectAtomicCounter[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDrawAtomicCounterBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibleLargeObjectAtomicCounter[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentAtomicCounter[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDepthPrepassAtomicCounter[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pMeshInfoBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pNormalSampleBuffer;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransformPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransformSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransformUniforms;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBasePositionBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pAnimConstants;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pAnimPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pAnimSRB[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVBO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pEBO;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pHiZTextures[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::ITexture> pDepthRenderbuffers[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::ISampler> pHiZSampler;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pHiZMipViewsSource[NumFrames][32];
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pHiZMipViewsDest[NumFrames][32];

    Diligent::RefCntAutoPtr<Diligent::IQuery> pFrameStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pFrameEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pUploadStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pUploadEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pAnimStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pAnimEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransformStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransformEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCullStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCullEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCommandGenStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pCommandGenEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectCommandGenStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pLargeObjectCommandGenEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pDepthPrePassStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pDepthPrePassEndQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentCullStartQuery[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IQuery> pTransparentCullEndQuery[NumFrames];
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
    bool TransparentDrawActive[NumFrames] = {false};

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pHiZMipmapSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pCullingSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pCommandGenSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pPrefixSumSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pScatterSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pApplyDirtySRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pMarkTouchedSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargeObjectCullSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentCullSRB[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pHiZMipmapPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCullingPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCommandGenPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPrefixSumPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pScatterPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pApplyDirtyPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pMarkTouchedPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pLargeObjectCullPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentCullPSO;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pStagingBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCullingUniforms;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCommandGenConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pPrefixSumConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pScatterConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pApplyDirtyConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pMarkTouchedConstants;

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentCommandGenSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentCommandGenPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCommandGenUniforms;

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargeObjectCommandGenSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pLargeObjectCommandGenPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeObjectCommandGenUniforms;

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDepthPrepassSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDepthPrepassPSO;

    std::vector<std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>>> pOpaqueSRBs;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentSRB[NumFrames];
    std::vector<Diligent::RefCntAutoPtr<Diligent::IPipelineState>> pOpaquePSOs;
    std::vector<Diligent::RefCntAutoPtr<Diligent::IPipelineState>> pPointPSOs;
    std::vector<std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>>> pPointSRBs;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pPointCommandBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentIdBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pPointDrawCounterBuffer[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeObjectCullConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCullUniforms;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDebugDepthPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDebugLinePSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDebugLineSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDebugLineVB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDebugLineUBO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDebugDepthSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDebugDepthUniforms;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDispatchArgsPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDispatchArgsConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pScatterDispatchArgsConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentDispatchArgs[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeObjectDispatchArgs[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pScatterDispatchArgs[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargeObjectDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pScatterDispatchArgsSRB[NumFrames];

    Diligent::IShaderResourceVariable* pHiZSourceMipVar = nullptr;
    Diligent::IShaderResourceVariable* pHiZDestMipVar = nullptr;

    Diligent::IShaderResourceVariable* pCullHiZTextureVar[NumFrames] = {};

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeBucketCountBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeBucketOffsetBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeBucketWriteHeadBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSortedVisibleLargeObjectBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeDrawAtomicCounterBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargePrefixSumSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargeScatterSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLargeCommandGenSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLargeScatterConstants;
};

Renderer::Renderer() : m_initialized(false), m_vboSize(0), m_eboSize(0), m_numDrawingShaders(0), fullProfiling(false) {}

void Renderer::init(GLFWwindow* window, const int windowWidth, const int windowHeight) {
    if (m_initialized) return;

    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

    m_diligent = new DiligentData();
    m_diligent->pFactoryVk = Diligent::GetEngineFactoryVk();

    Diligent::EngineVkCreateInfo EngineCI;
    EngineCI.Features.GeometryShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
    m_diligent->pFactoryVk->CreateDeviceAndContextsVk(EngineCI, &m_diligent->pDevice, &m_diligent->pImmediateContext);

    Diligent::SwapChainDesc SCDesc;
    SCDesc.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
    SCDesc.DepthBufferFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    SCDesc.Width = windowWidth;
    SCDesc.Height = windowHeight;

    Diligent::NativeWindow WinDesc;
#if defined(_WIN32)
    WinDesc.hWnd = glfwGetWin32Window(window);
#elif defined(__linux__)
    WinDesc.WindowId = static_cast<Diligent::Uint32>(glfwGetX11Window(window));
    WinDesc.pDisplay = glfwGetX11Display();
#elif defined(__APPLE__)
    WinDesc.pNSView = glfwGetCocoaWindow(window);
#endif

    m_diligent->pFactoryVk->CreateSwapChainVk(m_diligent->pDevice, m_diligent->pImmediateContext, SCDesc, WinDesc, &m_diligent->pSwapChain);

    m_uiManager = new UIManager();
    m_uiManager->init(m_diligent->pDevice, m_diligent->pImmediateContext, m_diligent->pSwapChain, windowWidth, windowHeight);

    createDepthPrepassPSO();
    createOpaquePSOs();
    createTransparentPSO();
    createTransformPSO();
    createAnimPSO();
    createHiZPSO();
    createCullingPSO();
    createCommandGenPSO();
    createPrefixSumPSO();
    createScatterPSO();
    createDispatchArgsPSO();
    createApplyDirtyPSO();
    createMarkTouchedPSO();
    createLargeObjectCullPSO();

    createLargeObjectCommandGenPSO();
    if (m_diligent->pLargeObjectCommandGenUniforms == nullptr) {
        Diligent::BufferDesc CBDesc;
        CBDesc.Name = "Large Object Command Gen Uniforms";
        CBDesc.Usage = Diligent::USAGE_DEFAULT;
        CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        CBDesc.Size = sizeof(LargeObjectCommandGenUniforms);
        m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pLargeObjectCommandGenUniforms);
    }
    createTransparentCullPSO();

    if (m_diligent->pTransparentCullUniforms == nullptr) {
        Diligent::BufferDesc CBDesc;
        CBDesc.Name = "Transparent Cull Uniforms";
        CBDesc.Usage = Diligent::USAGE_DEFAULT;
        CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        CBDesc.Size = sizeof(TransparentCullUniforms);
        m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pTransparentCullUniforms);
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

    createDebugDepthPSO();
    createDebugLinePSO();

    m_numDrawingShaders = m_diligent->pOpaquePSOs.size();
    const unsigned int zero = 0;
    std::vector<unsigned int> drawZeros(m_numDrawingShaders, 0);

    for (int i = 0; i < DiligentData::NumFrames; ++i) {
        m_diligent->pVisibleObjectAtomicCounter[i] = CreateStructuredBuffer(m_diligent->pDevice, "Visible Object Atomic Counter", sizeof(unsigned int), 1, (void*)&zero);
        m_diligent->pPointDrawCounterBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Point Draw Counter Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_numDrawingShaders), drawZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
        m_diligent->pDrawAtomicCounterBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Draw Atomic Counter Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_numDrawingShaders), drawZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
        m_diligent->pVisibleLargeObjectAtomicCounter[i] = CreateStructuredBuffer(m_diligent->pDevice, "Visible Large Object Atomic Counter", sizeof(unsigned int), 1, (void*)&zero);
        m_diligent->pTransparentAtomicCounter[i] = CreateStructuredBuffer(m_diligent->pDevice, "Transparent Atomic Counter", sizeof(unsigned int), 1, (void*)&zero, Diligent::BIND_INDIRECT_DRAW_ARGS);
        m_diligent->pDepthPrepassAtomicCounter[i] = CreateStructuredBuffer(m_diligent->pDevice, "Depth Prepass Atomic Counter", sizeof(unsigned int), 1, (void*)&zero, Diligent::BIND_INDIRECT_DRAW_ARGS);
    }

    m_maxObjects = 500000;
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
    CREATE_QUERY_PAIR("Upload", pUploadStartQuery, pUploadEndQuery);
    CREATE_QUERY_PAIR("Anim", pAnimStartQuery, pAnimEndQuery);
    CREATE_QUERY_PAIR("Transform", pTransformStartQuery, pTransformEndQuery);
    CREATE_QUERY_PAIR("Cull", pCullStartQuery, pCullEndQuery);
    CREATE_QUERY_PAIR("Command Gen", pCommandGenStartQuery, pCommandGenEndQuery);
    CREATE_QUERY_PAIR("Large Object Command Gen", pLargeObjectCommandGenStartQuery, pLargeObjectCommandGenEndQuery);
    CREATE_QUERY_PAIR("Depth Pre-Pass", pDepthPrePassStartQuery, pDepthPrePassEndQuery);
    CREATE_QUERY_PAIR("Transparent Cull", pTransparentCullStartQuery, pTransparentCullEndQuery);
    CREATE_QUERY_PAIR("Transparent Command Gen", pTransparentCommandGenStartQuery, pTransparentCommandGenEndQuery);
    CREATE_QUERY_PAIR("Opaque Draw", pOpaqueDrawStartQuery, pOpaqueDrawEndQuery);
    CREATE_QUERY_PAIR("Transparent Draw", pTransparentDrawStartQuery, pTransparentDrawEndQuery);
    CREATE_QUERY_PAIR("Hi-Z Mipmap", pHizMipmapStartQuery, pHizMipmapEndQuery);
    CREATE_QUERY_PAIR("UI", pUiStartQuery, pUiEndQuery);
    CREATE_QUERY_PAIR("Large Object Cull", pLargeObjectCullStartQuery, pLargeObjectCullEndQuery);

#undef CREATE_QUERY_PAIR

    m_vboSize = 1024 * 1024 * 10;
    m_eboSize = 1024 * 1024 * 4;

    Diligent::BufferDesc VBODesc;
    VBODesc.Name = "VBO";
    VBODesc.Usage = Diligent::USAGE_DEFAULT;
    VBODesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    VBODesc.Size = m_vboSize;
    m_diligent->pVBO.Release();
    m_diligent->pDevice->CreateBuffer(VBODesc, nullptr, &m_diligent->pVBO);

    Diligent::BufferDesc EBODesc;
    EBODesc.Name = "EBO";
    EBODesc.Usage = Diligent::USAGE_DEFAULT;
    EBODesc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    EBODesc.Size = m_eboSize;
    m_diligent->pEBO.Release();
    m_diligent->pDevice->CreateBuffer(EBODesc, nullptr, &m_diligent->pEBO);

    m_maxMipLevel = static_cast<int>(std::floor(std::log2(std::max(windowWidth, windowHeight))));

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
    }

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_diligent->pHiZTextures[i]) {
            const float clearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            m_diligent->pImmediateContext->ClearRenderTarget(m_diligent->pHiZTextures[i]->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET), clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
        if (m_diligent->pHiZTextures[i]) {
            auto* pView = m_diligent->pHiZTextures[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
            pView->SetSampler(m_diligent->pHiZSampler);

            for (int mip = 1; mip < m_maxMipLevel && mip < 32; ++mip) {
                Diligent::TextureViewDesc SourceViewDesc;
                SourceViewDesc.ViewType = Diligent::TEXTURE_VIEW_UNORDERED_ACCESS;
                SourceViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_READ;
                SourceViewDesc.MostDetailedMip = mip - 1;
                SourceViewDesc.NumMipLevels = 1;
                m_diligent->pHiZTextures[i]->CreateView(SourceViewDesc, &m_diligent->pHiZMipViewsSource[i][mip]);

                Diligent::TextureViewDesc DestViewDesc;
                DestViewDesc.ViewType = Diligent::TEXTURE_VIEW_UNORDERED_ACCESS;
                DestViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_WRITE;
                DestViewDesc.MostDetailedMip = mip;
                DestViewDesc.NumMipLevels = 1;
                m_diligent->pHiZTextures[i]->CreateView(DestViewDesc, &m_diligent->pHiZMipViewsDest[i][mip]);
            }
        }
    }

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

    m_diligent->pMeshInfoBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Mesh Info Buffer", sizeof(MeshInfo), MAX_MESHES);
    m_diligent->pNormalSampleBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Normal Sample Buffer", sizeof(glm::vec4), MAX_MESHES * kNormalSamples);
    m_meshInfoDirty = true;

    const uint32_t totalBuckets = static_cast<uint32_t>(m_numDrawingShaders) * MAX_MESHES;
    m_bucketZeros.assign(totalBuckets, 0);

    const size_t subPassMaxObjects = std::max<size_t>(m_maxObjects / 4, 1024);

    m_diligent->pOpaqueSRBs.clear();
    m_diligent->pOpaqueSRBs.resize(NUM_FRAMES_IN_FLIGHT);
    m_diligent->pPointSRBs.clear();
    m_diligent->pPointSRBs.resize(NUM_FRAMES_IN_FLIGHT);
    for (int f = 0; f < NUM_FRAMES_IN_FLIGHT; ++f) {
        m_diligent->pOpaqueSRBs[f].resize(m_diligent->pOpaquePSOs.size());
        m_diligent->pPointSRBs[f].resize(m_diligent->pPointPSOs.size());
    }

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_diligent->pObjectBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Object Buffer", sizeof(TransformComponent), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pObjectBuffer[0];
        m_diligent->pWorldMatrixBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "World Matrix Buffer", sizeof(glm::vec4) * 3, static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pWorldMatrixBuffer[0];
        m_diligent->pCullOrientationBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Cull Orientation Buffer", sizeof(glm::vec4), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pCullOrientationBuffer[0];
        m_diligent->pCullSphereBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Cull Sphere Buffer", sizeof(glm::vec4), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pCullSphereBuffer[0];
        m_diligent->pDirtyIndexBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Dirty Index Buffer", sizeof(unsigned int), MAX_DIRTY_PER_FRAME) : m_diligent->pDirtyIndexBuffer[0];
        m_diligent->pDirtyPayloadBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Dirty Payload Buffer", sizeof(glm::mat4), MAX_DIRTY_PER_FRAME) : m_diligent->pDirtyPayloadBuffer[0];
        m_diligent->pTouchedEpochBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Touched Epoch Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pTouchedEpochBuffer[0];
        m_diligent->pHierarchyBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Hierarchy Buffer", sizeof(HierarchyComponent), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pHierarchyBuffer[0];
        m_diligent->pRenderableBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Renderable Buffer", sizeof(RenderableComponent), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pRenderableBuffer[0];
        m_diligent->pSortedHierarchyBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Sorted Hierarchy Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pSortedHierarchyBuffer[0];
        m_diligent->pVisibleObjectBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Visible Objects Buffer", sizeof(unsigned int) * 2, static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pVisibleObjectBuffer[0];
        m_diligent->pSortedVisibleObjectBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Sorted Visible Objects Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pSortedVisibleObjectBuffer[0];
        m_diligent->pBucketCountBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Count Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pBucketOffsetBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Offset Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pBucketWriteHeadBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Write Head Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pTransparentIdBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Transparent Id Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pTransparentIdBuffer[0];
        m_diligent->pPointCommandBuffer[i] = CreateIndirectBuffer(m_diligent->pDevice, "Point Command Buffer", static_cast<size_t>(MAX_MESHES) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand));
        m_diligent->pDrawCommandBuffer[i] = CreateIndirectBuffer(m_diligent->pDevice, "Draw Command Buffer", static_cast<size_t>(MAX_MESHES) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand));

        m_diligent->pVisibleLargeObjectBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Visible Large Objects Buffer", sizeof(unsigned int) * 2, static_cast<Diligent::Uint32>(subPassMaxObjects));
        m_diligent->pSortedVisibleLargeObjectBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Sorted Visible Large Objects Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(subPassMaxObjects));
        m_diligent->pLargeBucketCountBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Large Object Bucket Count Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pLargeBucketOffsetBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Large Object Bucket Offset Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pLargeBucketWriteHeadBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Large Object Bucket Write Head Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pLargeDrawAtomicCounterBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Large Object Draw Atomic Counter Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_numDrawingShaders), m_bucketZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);

        m_diligent->pDepthPrepassDrawCommandBuffer[i] = CreateIndirectBuffer(m_diligent->pDevice, "Depth Pre-pass Draw Command Buffer", static_cast<size_t>(MAX_MESHES) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand));
        m_diligent->pVisibleTransparentObjectIdsBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Visible Transparent Object IDs Buffer", sizeof(VisibleTransparentObject), static_cast<Diligent::Uint32>(subPassMaxObjects));
        m_diligent->pTransparentDrawCommandBuffer[i] = CreateIndirectBuffer(m_diligent->pDevice, "Transparent Draw Command Buffer", subPassMaxObjects * sizeof(DrawElementsIndirectCommand));

        Diligent::BufferDesc DispatchArgsDesc;
        DispatchArgsDesc.Usage = Diligent::USAGE_DEFAULT;
        DispatchArgsDesc.BindFlags = Diligent::BIND_INDIRECT_DRAW_ARGS | Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_SHADER_RESOURCE;
        DispatchArgsDesc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
        DispatchArgsDesc.ElementByteStride = sizeof(unsigned int);
        DispatchArgsDesc.Size = sizeof(unsigned int) * 4;

        DispatchArgsDesc.Name = "Transparent Dispatch Args";
        m_diligent->pTransparentDispatchArgs[i].Release();
        m_diligent->pDevice->CreateBuffer(DispatchArgsDesc, nullptr, &m_diligent->pTransparentDispatchArgs[i]);

        DispatchArgsDesc.Name = "Large Object Dispatch Args";
        m_diligent->pLargeObjectDispatchArgs[i].Release();
        m_diligent->pDevice->CreateBuffer(DispatchArgsDesc, nullptr, &m_diligent->pLargeObjectDispatchArgs[i]);

        DispatchArgsDesc.Name = "Scatter Dispatch Args";
        m_diligent->pScatterDispatchArgs[i].Release();
        m_diligent->pDevice->CreateBuffer(DispatchArgsDesc, nullptr, &m_diligent->pScatterDispatchArgs[i]);

        Diligent::BufferDesc SceneUBODesc;
        SceneUBODesc.Name = "Scene UBO";
        SceneUBODesc.Usage = Diligent::USAGE_DEFAULT;
        SceneUBODesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        SceneUBODesc.Size = (sizeof(SceneUniforms) + 255) & ~255;
        m_diligent->pSceneUBO[i].Release();
        m_diligent->pDevice->CreateBuffer(SceneUBODesc, nullptr, &m_diligent->pSceneUBO[i]);
    }

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        const bool allocationsOk = m_diligent->pObjectBuffer[i] && m_diligent->pWorldMatrixBuffer[i] && m_diligent->pCullSphereBuffer[i] && m_diligent->pCullOrientationBuffer[i] &&
                                   m_diligent->pHierarchyBuffer[i] && m_diligent->pRenderableBuffer[i] && m_diligent->pSortedHierarchyBuffer[i] &&
                                   m_diligent->pVisibleObjectBuffer[i] && m_diligent->pSortedVisibleObjectBuffer[i] && m_diligent->pTouchedEpochBuffer[i] &&
                                   m_diligent->pDirtyIndexBuffer[i] && m_diligent->pDirtyPayloadBuffer[i] && m_diligent->pBucketCountBuffer[i] &&
                                   m_diligent->pBucketOffsetBuffer[i] && m_diligent->pBucketWriteHeadBuffer[i] && m_diligent->pDrawCommandBuffer[i] && m_diligent->pPointCommandBuffer[i] && m_diligent->pPointDrawCounterBuffer[i] && m_diligent->pTransparentIdBuffer[i] &&
                                   m_diligent->pVisibleLargeObjectBuffer[i] && m_diligent->pDepthPrepassDrawCommandBuffer[i] &&
                                   m_diligent->pVisibleTransparentObjectIdsBuffer[i] && m_diligent->pTransparentDrawCommandBuffer[i] && m_diligent->pSceneUBO[i];
        if (!allocationsOk) {
            Lit::Log::Fatal("Renderer buffer allocation failed for {} objects; rendering is disabled.", m_maxObjects);
            m_initialized = false;
            m_maxObjects = 0;
            return;
        }
    }

    if (!m_diligent->pBasePositionBuffer) {
        glm::vec4 zero(0.0f);
        m_diligent->pBasePositionBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Base Position Buffer", sizeof(glm::vec4), 1, &zero);
    }

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_diligent->pAnimPSO) {
            m_diligent->pAnimSRB[i].Release();
            m_diligent->pAnimPSO->CreateShaderResourceBinding(&m_diligent->pAnimSRB[i], true);
            if (m_diligent->pAnimSRB[i]) {
                auto* cbVar = m_diligent->pAnimSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AnimConstants");
                auto* baseVar = m_diligent->pAnimSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BasePositionBuffer");
                auto* transformVar = m_diligent->pAnimSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransformBuffer");

                if (cbVar && m_diligent->pAnimConstants) cbVar->Set(m_diligent->pAnimConstants);
                if (baseVar && m_diligent->pBasePositionBuffer) baseVar->Set(m_diligent->pBasePositionBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (transformVar && m_diligent->pObjectBuffer[i]) transformVar->Set(m_diligent->pObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pTransformPSO) {
            m_diligent->pTransformSRB[i].Release();
            m_diligent->pTransformPSO->CreateShaderResourceBinding(&m_diligent->pTransformSRB[i], true);
            if (m_diligent->pTransformSRB[i]) {
                auto* transformBuf = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransformBuffer");
                auto* worldMatrixBuf = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "WorldMatrixBuffer");
                auto* hierarchyBuf = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "HierarchyBuffer");
                auto* sortedBuf = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortedHierarchyBuffer");
                auto* uniformsVar = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransformUniforms");
                auto* touchedEpochBuf = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TouchedEpochBuffer");

                if (transformBuf && m_diligent->pObjectBuffer[i]) transformBuf->Set(m_diligent->pObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (worldMatrixBuf && m_diligent->pWorldMatrixBuffer[i]) worldMatrixBuf->Set(m_diligent->pWorldMatrixBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (touchedEpochBuf && m_diligent->pTouchedEpochBuffer[i]) touchedEpochBuf->Set(m_diligent->pTouchedEpochBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (hierarchyBuf && m_diligent->pHierarchyBuffer[i]) hierarchyBuf->Set(m_diligent->pHierarchyBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (sortedBuf && m_diligent->pSortedHierarchyBuffer[i]) sortedBuf->Set(m_diligent->pSortedHierarchyBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (uniformsVar && m_diligent->pTransformUniforms) uniformsVar->Set(m_diligent->pTransformUniforms);
                if (auto* var = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CullOrientationBuffer")) var->Set(m_diligent->pCullOrientationBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CullSphereBuffer")) var->Set(m_diligent->pCullSphereBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer")) var->Set(m_diligent->pRenderableBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer"); var && m_diligent->pMeshInfoBuffer) var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* baseVar = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BasePositionBuffer"); baseVar && m_diligent->pBasePositionBuffer) baseVar->Set(m_diligent->pBasePositionBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
            }
        }

        if (m_diligent->pCullingPSO) {
            m_diligent->pCullingSRB[i].Release();
            m_diligent->pCullingPSO->CreateShaderResourceBinding(&m_diligent->pCullingSRB[i], true);
            if (m_diligent->pCullingSRB[i]) {
                auto* sceneDataVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SceneData");
                auto* cullingUniformsVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CullingUniforms");
                auto* atomicCounterVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer");
                auto* visibleObjectVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer");
                auto* objectVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "WorldMatrixBuffer");
                auto* meshInfoVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer");
                auto* renderableVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer");
                auto* bucketCountVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketCountBuffer");

                if (sceneDataVar && m_diligent->pSceneUBO[i]) sceneDataVar->Set(m_diligent->pSceneUBO[i]);
                if (cullingUniformsVar && m_diligent->pCullingUniforms) cullingUniformsVar->Set(m_diligent->pCullingUniforms);
                if (atomicCounterVar && m_diligent->pVisibleObjectAtomicCounter[i]) atomicCounterVar->Set(m_diligent->pVisibleObjectAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (visibleObjectVar && m_diligent->pVisibleObjectBuffer[i]) visibleObjectVar->Set(m_diligent->pVisibleObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* sphereVar = m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CullSphereBuffer")) sphereVar->Set(m_diligent->pCullSphereBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (objectVar && m_diligent->pWorldMatrixBuffer[i]) objectVar->Set(m_diligent->pWorldMatrixBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (meshInfoVar && m_diligent->pMeshInfoBuffer) meshInfoVar->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (renderableVar && m_diligent->pRenderableBuffer[i]) renderableVar->Set(m_diligent->pRenderableBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (bucketCountVar && m_diligent->pBucketCountBuffer[i]) bucketCountVar->Set(m_diligent->pBucketCountBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pPrefixSumPSO) {
            m_diligent->pPrefixSumSRB[i].Release();
            m_diligent->pPrefixSumPSO->CreateShaderResourceBinding(&m_diligent->pPrefixSumSRB[i], true);
            if (m_diligent->pPrefixSumSRB[i]) {
                if (auto* var = m_diligent->pPrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "PrefixSumConstants")) var->Set(m_diligent->pPrefixSumConstants);
                if (auto* var = m_diligent->pPrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketCountBuffer")) var->Set(m_diligent->pBucketCountBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pPrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketOffsetBuffer")) var->Set(m_diligent->pBucketOffsetBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pPrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketWriteHeadBuffer")) var->Set(m_diligent->pBucketWriteHeadBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pScatterPSO) {
            m_diligent->pScatterSRB[i].Release();
            m_diligent->pScatterPSO->CreateShaderResourceBinding(&m_diligent->pScatterSRB[i], true);
            if (m_diligent->pScatterSRB[i]) {
                if (auto* var = m_diligent->pScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "ScatterConstants")) var->Set(m_diligent->pScatterConstants);
                if (auto* var = m_diligent->pScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer")) var->Set(m_diligent->pVisibleObjectAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer")) var->Set(m_diligent->pVisibleObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer")) var->Set(m_diligent->pRenderableBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketWriteHeadBuffer")) var->Set(m_diligent->pBucketWriteHeadBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortedVisibleObjectBuffer")) var->Set(m_diligent->pSortedVisibleObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pApplyDirtyPSO) {
            m_diligent->pApplyDirtySRB[i].Release();
            m_diligent->pApplyDirtyPSO->CreateShaderResourceBinding(&m_diligent->pApplyDirtySRB[i], true);
            if (m_diligent->pApplyDirtySRB[i]) {
                if (auto* var = m_diligent->pApplyDirtySRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DirtyIndexBuffer")) var->Set(m_diligent->pDirtyIndexBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pApplyDirtySRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DirtyPayloadBuffer")) var->Set(m_diligent->pDirtyPayloadBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pApplyDirtySRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransformBuffer")) var->Set(m_diligent->pObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pApplyDirtySRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TouchedEpochBuffer")) var->Set(m_diligent->pTouchedEpochBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pApplyDirtySRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "ApplyDirtyConstants")) var->Set(m_diligent->pApplyDirtyConstants);
            }
        }

        if (m_diligent->pMarkTouchedPSO) {
            m_diligent->pMarkTouchedSRB[i].Release();
            m_diligent->pMarkTouchedPSO->CreateShaderResourceBinding(&m_diligent->pMarkTouchedSRB[i], true);
            if (m_diligent->pMarkTouchedSRB[i]) {
                if (auto* var = m_diligent->pMarkTouchedSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DirtyIndexBuffer")) var->Set(m_diligent->pDirtyIndexBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pMarkTouchedSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TouchedEpochBuffer")) var->Set(m_diligent->pTouchedEpochBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pMarkTouchedSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MarkTouchedConstants")) var->Set(m_diligent->pMarkTouchedConstants);
            }
        }

        if (m_diligent->pCommandGenPSO) {
            m_diligent->pCommandGenSRB[i].Release();
            m_diligent->pCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pCommandGenSRB[i], true);
            if (m_diligent->pCommandGenSRB[i]) {
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "PointCounterBuffer")) var->Set(m_diligent->pPointDrawCounterBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "PointCommandBuffer")) var->Set(m_diligent->pPointCommandBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawAtomicCounterBuffer")) var->Set(m_diligent->pDrawAtomicCounterBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer")) var->Set(m_diligent->pDrawCommandBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer")) var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketCountBuffer")) var->Set(m_diligent->pBucketCountBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketOffsetBuffer")) var->Set(m_diligent->pBucketOffsetBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CommandGenConstants")) var->Set(m_diligent->pCommandGenConstants);
            }
        }

        if (m_diligent->pLargeObjectCullPSO) {
            m_diligent->pLargeObjectCullSRB[i].Release();
            m_diligent->pLargeObjectCullPSO->CreateShaderResourceBinding(&m_diligent->pLargeObjectCullSRB[i], true);
            if (m_diligent->pLargeObjectCullSRB[i]) {
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SceneUniforms")) var->Set(m_diligent->pSceneUBO[i]);
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer")) var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectAtomicCounter")) var->Set(m_diligent->pVisibleLargeObjectAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "LargeObjectCullConstants")) var->Set(m_diligent->pLargeObjectCullConstants);
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CullSphereBuffer")) var->Set(m_diligent->pCullSphereBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer")) var->Set(m_diligent->pRenderableBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer")) var->Set(m_diligent->pVisibleLargeObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeObjectCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "LargeBucketCountBuffer")) var->Set(m_diligent->pLargeBucketCountBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pPrefixSumPSO) {
            m_diligent->pLargePrefixSumSRB[i].Release();
            m_diligent->pPrefixSumPSO->CreateShaderResourceBinding(&m_diligent->pLargePrefixSumSRB[i], true);
            if (m_diligent->pLargePrefixSumSRB[i]) {
                if (auto* var = m_diligent->pLargePrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "PrefixSumConstants")) var->Set(m_diligent->pPrefixSumConstants);
                if (auto* var = m_diligent->pLargePrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketCountBuffer")) var->Set(m_diligent->pLargeBucketCountBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargePrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketOffsetBuffer")) var->Set(m_diligent->pLargeBucketOffsetBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargePrefixSumSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketWriteHeadBuffer")) var->Set(m_diligent->pLargeBucketWriteHeadBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pScatterPSO) {
            m_diligent->pLargeScatterSRB[i].Release();
            m_diligent->pScatterPSO->CreateShaderResourceBinding(&m_diligent->pLargeScatterSRB[i], true);
            if (m_diligent->pLargeScatterSRB[i]) {
                if (auto* var = m_diligent->pLargeScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "ScatterConstants")) var->Set(m_diligent->pLargeScatterConstants);
                if (auto* var = m_diligent->pLargeScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer")) var->Set(m_diligent->pVisibleLargeObjectAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer")) var->Set(m_diligent->pVisibleLargeObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer")) var->Set(m_diligent->pRenderableBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketWriteHeadBuffer")) var->Set(m_diligent->pLargeBucketWriteHeadBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeScatterSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SortedVisibleObjectBuffer")) var->Set(m_diligent->pSortedVisibleLargeObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pCommandGenPSO) {
            m_diligent->pLargeCommandGenSRB[i].Release();
            m_diligent->pCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pLargeCommandGenSRB[i], true);
            if (m_diligent->pLargeCommandGenSRB[i]) {
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "PointCounterBuffer")) var->Set(m_diligent->pPointDrawCounterBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "PointCommandBuffer")) var->Set(m_diligent->pPointCommandBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawAtomicCounterBuffer")) var->Set(m_diligent->pLargeDrawAtomicCounterBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer")) var->Set(m_diligent->pDepthPrepassDrawCommandBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer")) var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketCountBuffer")) var->Set(m_diligent->pLargeBucketCountBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BucketOffsetBuffer")) var->Set(m_diligent->pLargeBucketOffsetBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CommandGenConstants")) var->Set(m_diligent->pCommandGenConstants);
            }
        }

        if (m_diligent->pLargeObjectCommandGenPSO) {
            m_diligent->pLargeObjectCommandGenSRB[i].Release();
            m_diligent->pLargeObjectCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pLargeObjectCommandGenSRB[i], true);
            if (m_diligent->pLargeObjectCommandGenSRB[i]) {
                if (auto* var = m_diligent->pLargeObjectCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "LargeObjectCommandGenUniforms")) var->Set(m_diligent->pLargeObjectCommandGenUniforms);
                if (auto* var = m_diligent->pLargeObjectCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectAtomicCounter")) var->Set(m_diligent->pVisibleLargeObjectAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeObjectCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer")) var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeObjectCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer")) var->Set(m_diligent->pDepthPrepassAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeObjectCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer")) var->Set(m_diligent->pDepthPrepassDrawCommandBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pLargeObjectCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer")) var->Set(m_diligent->pRenderableBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeObjectCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer")) var->Set(m_diligent->pVisibleLargeObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
            }
        }

        if (m_diligent->pTransparentCullPSO) {
            m_diligent->pTransparentCullSRB[i].Release();
            m_diligent->pTransparentCullPSO->CreateShaderResourceBinding(&m_diligent->pTransparentCullSRB[i], true);
            if (m_diligent->pTransparentCullSRB[i]) {
                if (auto* var = m_diligent->pTransparentCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                if (auto* var = m_diligent->pTransparentCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer")) var->Set(m_diligent->pTransparentAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pTransparentCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer")) var->Set(m_diligent->pVisibleTransparentObjectIdsBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pTransparentCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "CullSphereBuffer")) var->Set(m_diligent->pCullSphereBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer")) var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransparentIdBuffer")) var->Set(m_diligent->pTransparentIdBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentCullSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransparentCullUniforms")) var->Set(m_diligent->pTransparentCullUniforms);
            }
        }

        if (m_diligent->pTransparentCommandGenPSO) {
            m_diligent->pTransparentCommandGenSRB[i].Release();
            m_diligent->pTransparentCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pTransparentCommandGenSRB[i], true);
            if (m_diligent->pTransparentCommandGenSRB[i]) {
                if (auto* var = m_diligent->pTransparentCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer")) var->Set(m_diligent->pVisibleTransparentObjectIdsBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer")) var->Set(m_diligent->pMeshInfoBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer")) var->Set(m_diligent->pRenderableBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransparentDrawCommandBuffer")) var->Set(m_diligent->pTransparentDrawCommandBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
                if (auto* var = m_diligent->pTransparentCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer")) var->Set(m_diligent->pTransparentAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentCommandGenSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "TransparentCommandGenUniforms")) var->Set(m_diligent->pTransparentCommandGenUniforms);
            }
        }

        if (m_diligent->pDispatchArgsPSO) {
            m_diligent->pTransparentDispatchArgsSRB[i].Release();
            m_diligent->pDispatchArgsPSO->CreateShaderResourceBinding(&m_diligent->pTransparentDispatchArgsSRB[i], true);
            if (m_diligent->pTransparentDispatchArgsSRB[i]) {
                if (auto* var = m_diligent->pTransparentDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsConstants")) var->Set(m_diligent->pDispatchArgsConstants);
                if (auto* var = m_diligent->pTransparentDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SourceCounterBuffer")) var->Set(m_diligent->pTransparentAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsBuffer")) var->Set(m_diligent->pTransparentDispatchArgs[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }

            m_diligent->pLargeObjectDispatchArgsSRB[i].Release();
            m_diligent->pDispatchArgsPSO->CreateShaderResourceBinding(&m_diligent->pLargeObjectDispatchArgsSRB[i], true);
            if (m_diligent->pLargeObjectDispatchArgsSRB[i]) {
                if (auto* var = m_diligent->pLargeObjectDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsConstants")) var->Set(m_diligent->pDispatchArgsConstants);
                if (auto* var = m_diligent->pLargeObjectDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SourceCounterBuffer")) var->Set(m_diligent->pVisibleLargeObjectAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pLargeObjectDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsBuffer")) var->Set(m_diligent->pLargeObjectDispatchArgs[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }

            m_diligent->pScatterDispatchArgsSRB[i].Release();
            m_diligent->pDispatchArgsPSO->CreateShaderResourceBinding(&m_diligent->pScatterDispatchArgsSRB[i], true);
            if (m_diligent->pScatterDispatchArgsSRB[i]) {
                if (auto* var = m_diligent->pScatterDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsConstants")) var->Set(m_diligent->pScatterDispatchArgsConstants);
                if (auto* var = m_diligent->pScatterDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "SourceCounterBuffer")) var->Set(m_diligent->pVisibleObjectAtomicCounter[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pScatterDispatchArgsSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsBuffer")) var->Set(m_diligent->pScatterDispatchArgs[i]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
            }
        }

        if (m_diligent->pDepthPrepassPSO) {
            m_diligent->pDepthPrepassSRB[i].Release();
            m_diligent->pDepthPrepassPSO->CreateShaderResourceBinding(&m_diligent->pDepthPrepassSRB[i], true);
            if (m_diligent->pDepthPrepassSRB[i]) {
                if (auto* var = m_diligent->pDepthPrepassSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                if (auto* var = m_diligent->pDepthPrepassSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "WorldMatrixBuffer")) var->Set(m_diligent->pWorldMatrixBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pDepthPrepassSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "VisibleLargeObjectBuffer")) var->Set(m_diligent->pSortedVisibleLargeObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
            }
        }

        for (size_t s = 0; s < m_diligent->pOpaquePSOs.size(); ++s) {
            if (m_diligent->pOpaquePSOs[s]) {
                m_diligent->pOpaqueSRBs[i][s].Release();
                m_diligent->pOpaquePSOs[s]->CreateShaderResourceBinding(&m_diligent->pOpaqueSRBs[i][s], true);
                if (m_diligent->pOpaqueSRBs[i][s]) {
                    if (auto* var = m_diligent->pOpaqueSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                    if (auto* var = m_diligent->pOpaqueSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                    if (auto* var = m_diligent->pOpaqueSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "CullSphereBuffer")) var->Set(m_diligent->pCullSphereBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                    if (auto* var = m_diligent->pOpaqueSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "WorldMatrixBuffer")) var->Set(m_diligent->pWorldMatrixBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                    if (auto* var = m_diligent->pOpaqueSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "VisibleObjectBuffer")) var->Set(m_diligent->pSortedVisibleObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                }
            }
        }

        for (size_t s = 0; s < m_diligent->pPointPSOs.size(); ++s) {
            if (m_diligent->pPointPSOs[s]) {
                m_diligent->pPointSRBs[i][s].Release();
                m_diligent->pPointPSOs[s]->CreateShaderResourceBinding(&m_diligent->pPointSRBs[i][s], true);
                if (m_diligent->pPointSRBs[i][s]) {
                    if (auto* var = m_diligent->pPointSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                    if (auto* var = m_diligent->pPointSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                    if (auto* var = m_diligent->pPointSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "CullSphereBuffer")) var->Set(m_diligent->pCullSphereBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                    if (auto* var = m_diligent->pPointSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "CullOrientationBuffer")) var->Set(m_diligent->pCullOrientationBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                    if (auto* var = m_diligent->pPointSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "NormalSampleBuffer")) var->Set(m_diligent->pNormalSampleBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                    if (auto* var = m_diligent->pPointSRBs[i][s]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "VisibleObjectBuffer")) var->Set(m_diligent->pSortedVisibleObjectBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                }
            }
        }

        if (m_diligent->pTransparentPSO) {
            m_diligent->pTransparentSRB[i].Release();
            m_diligent->pTransparentPSO->CreateShaderResourceBinding(&m_diligent->pTransparentSRB[i], true);
            if (m_diligent->pTransparentSRB[i]) {
                if (auto* var = m_diligent->pTransparentSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                if (auto* var = m_diligent->pTransparentSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "SceneData")) var->Set(m_diligent->pSceneUBO[i]);
                if (auto* var = m_diligent->pTransparentSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "CullSphereBuffer")) var->Set(m_diligent->pCullSphereBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "WorldMatrixBuffer")) var->Set(m_diligent->pWorldMatrixBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
                if (auto* var = m_diligent->pTransparentSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "VisibleTransparentObjectBuffer")) var->Set(m_diligent->pVisibleTransparentObjectIdsBuffer[i]->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
            }
        }
    }

    if (m_diligent->pLargeObjectCommandGenUniforms) {
        LargeObjectCommandGenUniforms uniforms{};
        uniforms.maxDraws = static_cast<unsigned int>(subPassMaxObjects);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pLargeObjectCommandGenUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    if (m_diligent->pTransparentCommandGenUniforms) {
        TransparentCommandGenUniforms uniforms{};
        uniforms.maxDraws = static_cast<unsigned int>(subPassMaxObjects);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentCommandGenUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    if (m_diligent->pDispatchArgsConstants) {
        DispatchArgsConstants dispatchArgsConstants{};
        dispatchArgsConstants.maxCount = static_cast<uint32_t>(subPassMaxObjects);
        dispatchArgsConstants.workgroupSize = 256;
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pDispatchArgsConstants, 0, sizeof(dispatchArgsConstants), &dispatchArgsConstants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    if (m_diligent->pScatterDispatchArgsConstants) {
        DispatchArgsConstants dispatchArgsConstants{};
        dispatchArgsConstants.maxCount = static_cast<uint32_t>(m_maxObjects);
        dispatchArgsConstants.workgroupSize = 256;
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pScatterDispatchArgsConstants, 0, sizeof(dispatchArgsConstants), &dispatchArgsConstants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_diligent->pCullHiZTextureVar[i] = m_diligent->pCullingSRB[i] ? m_diligent->pCullingSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "u_hizTexture") : nullptr;
    }
    if (m_diligent->pHiZMipmapSRB) {
        m_diligent->pHiZSourceMipVar = m_diligent->pHiZMipmapSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "u_sourceMip");
        m_diligent->pHiZDestMipVar = m_diligent->pHiZMipmapSRB->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "u_destMip");
    }

    m_fullTransformUpdateCounter = 1;
    m_transformUpdateCounter = 0;
    m_renderableUpdateCounter = 1;
    m_hierarchyUpdateCounter = 1;
}

void Renderer::present() {
    if (m_diligent && m_diligent->pSwapChain) { m_diligent->pSwapChain->Present(0); }
}

void Renderer::cleanup() {
    if (!m_initialized) return;

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) { m_diligent->pFences[i].Release(); }

    delete m_uiManager;
    m_uiManager = nullptr;

    if (m_diligent) {
        delete m_diligent;
        m_diligent = nullptr;
    }

    m_initialized = false;
}

Renderer::~Renderer() { cleanup(); }

uint32_t Renderer::uploadMeshSlot(const Mesh& mesh) {
    if (s_meshInfos.size() >= MAX_MESHES) {
        Lit::Log::Error("Mesh slot limit of {} reached; refusing to upload another mesh.", MAX_MESHES);
        return INVALID_MESH_UUID;
    }

    const size_t vertexDataSize = mesh.vertices.size() * sizeof(float);
    const size_t indexDataSize = mesh.indices.size() * sizeof(unsigned int);

    Lit::Log::Info("Uploading mesh: {} vertices ({} bytes), {} indices ({} bytes)", mesh.vertices.size() / 6, vertexDataSize, mesh.indices.size(), indexDataSize);

    auto resizeBuffer = [&](Diligent::RefCntAutoPtr<Diligent::IBuffer>& pBuffer, size_t currentSize, size_t newSize, bool isIndexBuffer) {
        Diligent::RefCntAutoPtr<Diligent::IBuffer> pNewBuffer;
        if (isIndexBuffer) {
            pNewBuffer = CreateIndexBuffer(m_diligent->pDevice, newSize);
        } else {
            pNewBuffer = CreateVertexBuffer(m_diligent->pDevice, newSize);
        }

        if (currentSize > 0) { m_diligent->pImmediateContext->CopyBuffer(pBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, pNewBuffer, 0, currentSize, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION); }

        pBuffer = pNewBuffer;
    };

    if (s_totalVertexSize + vertexDataSize > m_vboSize || s_totalIndexSize + indexDataSize > m_eboSize) {
        m_vboSize = std::max(m_vboSize * 2, s_totalVertexSize + vertexDataSize);
        m_eboSize = std::max(m_eboSize * 2, s_totalIndexSize + indexDataSize);

        resizeBuffer(m_diligent->pVBO, s_totalVertexSize, m_vboSize, false);
        resizeBuffer(m_diligent->pEBO, s_totalIndexSize, m_eboSize, true);
    }

    if (vertexDataSize > 0) { m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pVBO, s_totalVertexSize, vertexDataSize, mesh.vertices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION); }
    if (indexDataSize > 0) { m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pEBO, s_totalIndexSize, indexDataSize, mesh.indices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION); }

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
        if (distSq > maxRadiusSq) { maxRadiusSq = distSq; }
    }
    const float radius = glm::sqrt(maxRadiusSq);

    const uint32_t meshUuid = static_cast<uint32_t>(s_meshInfos.size());
    s_meshInfos.push_back({.indexCount = static_cast<unsigned int>(mesh.indices.size()), .firstIndex = static_cast<unsigned int>(s_totalIndexSize / sizeof(unsigned int)), .baseVertex = static_cast<unsigned int>(s_totalVertexSize / (6 * sizeof(float))), .boundingRadius = radius, .boundingCenter = glm::vec4(center, 1.0f)});

    s_totalVertexSize += vertexDataSize;
    s_totalIndexSize += indexDataSize;

    m_meshInfoDirty = true;
    return meshUuid;
}

glm::vec4 Renderer::getMeshBounds(uint32_t meshId) const {
    if (meshId >= s_meshInfos.size()) return glm::vec4(0.0f);
    const MeshInfo& info = s_meshInfos[meshId];
    return glm::vec4(glm::vec3(info.boundingCenter), info.boundingRadius);
}

uint32_t Renderer::uploadMesh(const Mesh& source) {
    if (source.vertices.empty() || source.indices.empty()) { return 0; }
    const Mesh mesh = source.optimized();

    if (s_meshInfos.size() + kLodLevelCount > MAX_MESHES) {
        Lit::Log::Error("Mesh slot limit of {} reached ({} slots per mesh); refusing to upload another mesh.", MAX_MESHES, kLodLevelCount);
        return INVALID_MESH_UUID;
    }

    const uint32_t baseUuid = uploadMeshSlot(mesh);
    if (baseUuid == INVALID_MESH_UUID) { return INVALID_MESH_UUID; }

    if (s_normalSamples.empty()) { s_normalSamples.assign(static_cast<size_t>(MAX_MESHES) * kNormalSamples, glm::vec4(0.0f)); }
    const std::vector<float> samples = mesh.normalSamples(kNormalSamples);
    const float boundingRadius = s_meshInfos[baseUuid].boundingRadius;
    const float silhouetteRatio = boundingRadius > 0.0f ? std::min(1.0f, std::sqrt(mesh.surfaceArea() / (4.0f * 3.14159265f)) / boundingRadius) : 1.0f;
    const float packedShape = mesh.isRound() ? -silhouetteRatio : silhouetteRatio;
    for (uint32_t i = 0; i < kNormalSamples; ++i) {
        s_normalSamples[static_cast<size_t>(baseUuid) * kNormalSamples + i] = glm::vec4(samples[i * 4], samples[i * 4 + 1], samples[i * 4 + 2], packedShape);
    }

    const MeshInfo baseInfo = s_meshInfos[baseUuid];
    size_t previousIndexCount = mesh.indices.size();

    const std::vector<Mesh> chain = mesh.generateLODChain();
    for (size_t level = 0; level < chain.size(); ++level) {
        const Mesh& lod = chain[level];
        const bool isImpostor = (level + 1 == chain.size());
        if (!isImpostor && (lod.indices.empty() || lod.indices.size() >= previousIndexCount)) {
            s_meshInfos.push_back(s_meshInfos.back());
            continue;
        }

        const uint32_t slot = uploadMeshSlot(lod);
        s_meshInfos[slot].boundingRadius = baseInfo.boundingRadius;
        s_meshInfos[slot].boundingCenter = baseInfo.boundingCenter;
        previousIndexCount = lod.indices.size();
    }

    Lit::Log::Info("Mesh {} uploaded with {} LOD levels (index counts: {} -> {})", baseUuid, kLodLevelCount, mesh.indices.size(), s_meshInfos.back().indexCount);
    return baseUuid;
}

void Renderer::setSmallObjectThreshold(float threshold) { m_smallObjectThreshold = threshold; }
void Renderer::setLodBias(float bias) { m_lodBias = std::max(bias, 0.0f); }
void Renderer::setForcedLod(int lod) { m_forcedLod = (lod < 0) ? -1 : std::min(lod, static_cast<int>(kLodLevelCount) - 1); }
int Renderer::getForcedLod() const { return m_forcedLod; }
void Renderer::setLargeObjectThreshold(float threshold) { m_largeObjectThreshold = threshold; }
void Renderer::setDebugDepthMode(bool enabled) { m_debugDepthMode = enabled; }
bool Renderer::isDebugDepthMode() const { return m_debugDepthMode; }
void Renderer::setFullProfiling(bool enabled) { fullProfiling = enabled; }

void Renderer::drawScene(SceneDatabase& sceneDatabase, const Camera& camera) {
    static double s_lastUploadTime = 0;
    static double s_lastAnimTime = 0;
    static double s_lastTransformTime = 0;
    static double s_lastLargeObjCullTime = 0;
    static double s_lastLargeObjCmdGenTime = 0;
    static double s_lastDepthPrePassTime = 0;
    static double s_lastHizTime = 0;
    static double s_lastOpaqueCullTime = 0;
    static double s_lastOpaqueCmdGenTime = 0;
    static double s_lastOpaqueDrawTime = 0;
    static double s_lastTransCullTime = 0;
    static double s_lastTransCmdGenTime = 0;
    static double s_lastTransDrawTime = 0;
    static double s_lastUiTime = 0;
    static double s_lastGpuTotal = 0;
    static double s_lastFenceWaitMs = 0;
    static float s_logTimer = 0.0f;

    float currentFrameTime = glfwGetTime();
    float deltaTime = currentFrameTime - m_lastFrameTime;
    m_lastFrameTime = currentFrameTime;

    const unsigned int numObjects = sceneDatabase.renderables.size();
    if (numObjects > m_maxObjects) {
        m_diligent->pImmediateContext->WaitForIdle();
        reallocateBuffers(static_cast<size_t>(numObjects) + numObjects / 100 + 1024);
    }

    m_currentFrame = (m_currentFrame + 1) % NUM_FRAMES_IN_FLIGHT;

    const bool profileThisFrame = fullProfiling;
    auto Timestamp = [&](Diligent::IQuery* pQuery) {
        if (profileThisFrame && pQuery) m_diligent->pImmediateContext->EndQuery(pQuery);
    };

    {
        double fenceWaitStart = glfwGetTime();
        Diligent::Uint64 FenceValue = m_diligent->FenceValues[m_currentFrame];
        m_diligent->pFences[m_currentFrame]->Wait(FenceValue);
        s_lastFenceWaitMs = (glfwGetTime() - fenceWaitStart) * 1000.0;
    }

    if (m_diligent->QueryReady[m_currentFrame]) {
        auto GetQueryDataRef = [&](Diligent::IQuery* pStartQuery, Diligent::IQuery* pEndQuery) -> double {
            if (!pStartQuery || !pEndQuery) return 0.0;
            Diligent::QueryDataTimestamp StartData = {};
            Diligent::QueryDataTimestamp EndData = {};

            if (pStartQuery->GetData(&StartData, sizeof(StartData), false) &&
                pEndQuery->GetData(&EndData, sizeof(EndData), false)) {
                if (EndData.Counter > StartData.Counter && EndData.Frequency > 0) {
                    return (double)(EndData.Counter - StartData.Counter) / (double)EndData.Frequency * 1000.0;
                }
            }
            return 0.0;
        };

        s_lastUploadTime = GetQueryDataRef(m_diligent->pUploadStartQuery[m_currentFrame], m_diligent->pUploadEndQuery[m_currentFrame]);
        s_lastAnimTime = GetQueryDataRef(m_diligent->pAnimStartQuery[m_currentFrame], m_diligent->pAnimEndQuery[m_currentFrame]);
        s_lastTransformTime = GetQueryDataRef(m_diligent->pTransformStartQuery[m_currentFrame], m_diligent->pTransformEndQuery[m_currentFrame]);
        s_lastLargeObjCullTime = GetQueryDataRef(m_diligent->pLargeObjectCullStartQuery[m_currentFrame], m_diligent->pLargeObjectCullEndQuery[m_currentFrame]);
        s_lastLargeObjCmdGenTime = GetQueryDataRef(m_diligent->pLargeObjectCommandGenStartQuery[m_currentFrame], m_diligent->pLargeObjectCommandGenEndQuery[m_currentFrame]);
        s_lastDepthPrePassTime = GetQueryDataRef(m_diligent->pDepthPrePassStartQuery[m_currentFrame], m_diligent->pDepthPrePassEndQuery[m_currentFrame]);
        s_lastHizTime = GetQueryDataRef(m_diligent->pHizMipmapStartQuery[m_currentFrame], m_diligent->pHizMipmapEndQuery[m_currentFrame]);
        s_lastOpaqueCullTime = GetQueryDataRef(m_diligent->pCullStartQuery[m_currentFrame], m_diligent->pCullEndQuery[m_currentFrame]);
        s_lastOpaqueCmdGenTime = GetQueryDataRef(m_diligent->pCommandGenStartQuery[m_currentFrame], m_diligent->pCommandGenEndQuery[m_currentFrame]);
        s_lastOpaqueDrawTime = GetQueryDataRef(m_diligent->pOpaqueDrawStartQuery[m_currentFrame], m_diligent->pOpaqueDrawEndQuery[m_currentFrame]);
        s_lastTransCullTime = GetQueryDataRef(m_diligent->pTransparentCullStartQuery[m_currentFrame], m_diligent->pTransparentCullEndQuery[m_currentFrame]);
        s_lastTransCmdGenTime = GetQueryDataRef(m_diligent->pTransparentCommandGenStartQuery[m_currentFrame], m_diligent->pTransparentCommandGenEndQuery[m_currentFrame]);

        if (m_diligent->TransparentDrawActive[m_currentFrame])
            s_lastTransDrawTime = GetQueryDataRef(m_diligent->pTransparentDrawStartQuery[m_currentFrame], m_diligent->pTransparentDrawEndQuery[m_currentFrame]);
        else
            s_lastTransDrawTime = 0.0;

        s_lastUiTime = GetQueryDataRef(m_diligent->pUiStartQuery[m_currentFrame], m_diligent->pUiEndQuery[m_currentFrame]);

        s_lastGpuTotal = s_lastUploadTime + s_lastAnimTime + s_lastTransformTime + s_lastLargeObjCullTime + s_lastLargeObjCmdGenTime +
                         s_lastDepthPrePassTime + s_lastHizTime + s_lastOpaqueCullTime + s_lastOpaqueCmdGenTime +
                         s_lastOpaqueDrawTime + s_lastTransCullTime + s_lastTransCmdGenTime + s_lastTransDrawTime + s_lastUiTime;

        s_logTimer += deltaTime;
        if (fullProfiling && s_logTimer >= 0.5f) {
            Lit::Log::Info("--- Profiling (GPU & Sync) ---");
            Lit::Log::Info("GPU Total: {:.2f} ms | CPU Fence Wait: {:.2f} ms", s_lastGpuTotal, s_lastFenceWaitMs);
            Lit::Log::Info("  Upload: {:.2f} ms | GPU Anim: {:.2f} ms | Transform: {:.2f} ms", s_lastUploadTime, s_lastAnimTime, s_lastTransformTime);
            Lit::Log::Info("  Large Cull: {:.2f} ms | Large CmdGen: {:.2f} ms | Depth Pre-Pass: {:.2f} ms | Hi-Z: {:.2f} ms", s_lastLargeObjCullTime, s_lastLargeObjCmdGenTime, s_lastDepthPrePassTime, s_lastHizTime);
            Lit::Log::Info("  Opaque Cull: {:.2f} ms | Opaque CmdGen: {:.2f} ms | Opaque Draw: {:.2f} ms", s_lastOpaqueCullTime, s_lastOpaqueCmdGenTime, s_lastOpaqueDrawTime);
            Lit::Log::Info("  Trans Cull: {:.2f} ms | Trans CmdGen: {:.2f} ms | Trans Draw: {:.2f} ms", s_lastTransCullTime, s_lastTransCmdGenTime, s_lastTransDrawTime);
            Lit::Log::Info("  UI: {:.2f} ms", s_lastUiTime);
            s_logTimer = 0.0f;
        }
    }

    if (m_processedHierarchyVersion < sceneDatabase.m_hierarchyVersion) {
        sceneDatabase.updateHierarchy();
        m_hierarchyUpdateCounter = 1;
        m_processedHierarchyVersion = sceneDatabase.m_hierarchyVersion;
    }

    if (!m_initialized || s_meshInfos.empty()) {
        m_diligent->QueryReady[m_currentFrame] = false;
        return;
    }

    if (numObjects == 0) {
        m_diligent->QueryReady[m_currentFrame] = false;
        m_diligent->pImmediateContext->ClearRenderTarget(m_diligent->pSwapChain->GetCurrentBackBufferRTV(), glm::value_ptr(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pImmediateContext->ClearDepthStencil(m_diligent->pSwapChain->GetDepthBufferDSV(), Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return;
    }

    Timestamp(m_diligent->pUploadStartQuery[m_currentFrame]);

    auto ResetAtomicCounter = [&](Diligent::IBuffer* pBuffer) {
        unsigned int zero = 0;
        m_diligent->pImmediateContext->UpdateBuffer(pBuffer, 0, sizeof(unsigned int), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    };

    const bool hierarchyJustChanged = (m_hierarchyUpdateCounter > 0);
    bool dirtyUsesBulkPath = false;

    if (m_processedTransformVersion < sceneDatabase.m_transformVersion) {
        m_transformUpdateCounter = 1;
        dirtyUsesBulkPath = true;
        m_processedTransformVersion = sceneDatabase.m_transformVersion;
    }

    if (m_processedDataVersion < sceneDatabase.m_dataVersion) {
        m_fullTransformUpdateCounter = 1;
        m_renderableUpdateCounter = 1;
        dirtyUsesBulkPath = true;
        m_processedDataVersion = sceneDatabase.m_dataVersion;
        m_transparentIdsStale = true;
    }

    const std::vector<Entity>& newlyDirty = sceneDatabase.m_dirtyList;
    if (!newlyDirty.empty()) {
        if (newlyDirty.size() > MAX_DIRTY_PER_FRAME) {
            dirtyUsesBulkPath = true;
            m_transformUpdateCounter = 1;
        } else if (!dirtyUsesBulkPath) {
            m_pendingDirty[0].insert(m_pendingDirty[0].end(), newlyDirty.begin(), newlyDirty.end());
        }
        sceneDatabase.clearDirty();
    }

    if (dirtyUsesBulkPath) {
        m_pendingDirty[0].clear();
    }

    if (m_hierarchyUpdateCounter > 0) {
        const size_t dataSize = sceneDatabase.hierarchies.size() * sizeof(HierarchyComponent);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pHierarchyBuffer[m_currentFrame], 0, dataSize, sceneDatabase.hierarchies.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const size_t sortedHierarchyDataSize = sceneDatabase.sortedHierarchyList.size() * sizeof(unsigned int);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pSortedHierarchyBuffer[m_currentFrame], 0, sortedHierarchyDataSize, sceneDatabase.sortedHierarchyList.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_hierarchyUpdateCounter--;
    }

    const bool fullTransformUpdateActive = (m_fullTransformUpdateCounter > 0);
    const bool localMatricesUploading = (m_fullTransformUpdateCounter > 0) || (m_transformUpdateCounter > 0);

    if (m_fullTransformUpdateCounter > 0) {
        const size_t dataSize = sceneDatabase.transforms.size() * sizeof(TransformComponent);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pObjectBuffer[m_currentFrame], 0, dataSize, sceneDatabase.transforms.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_fullTransformUpdateCounter--;
        if (m_transformUpdateCounter > 0) m_transformUpdateCounter--;
    } else if (m_transformUpdateCounter > 0) {
        const size_t count = (sceneDatabase.m_movingCount > 0) ? std::min<size_t>(sceneDatabase.m_movingCount, sceneDatabase.transforms.size()) : sceneDatabase.transforms.size();
        const size_t dataSize = count * sizeof(TransformComponent);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pObjectBuffer[m_currentFrame], 0, dataSize, sceneDatabase.transforms.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_transformUpdateCounter--;
    }

    if (m_renderableUpdateCounter > 0) {
        const size_t renderableDataSize = sceneDatabase.renderables.size() * sizeof(RenderableComponent);
        if (m_transparentIdsStale) {
            m_transparentIds.clear();
            for (size_t e = 0; e < sceneDatabase.renderables.size(); ++e) {
                if (sceneDatabase.renderables[e].shaderId == 2) { m_transparentIds.push_back(static_cast<uint32_t>(e)); }
            }
            m_transparentIdsStale = false;
        }
        m_transparentIdCounts[0] = static_cast<uint32_t>(m_transparentIds.size());
        if (!m_transparentIds.empty()) {
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentIdBuffer[m_currentFrame], 0, m_transparentIds.size() * sizeof(uint32_t), m_transparentIds.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pRenderableBuffer[m_currentFrame], 0, renderableDataSize, sceneDatabase.renderables.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_renderableUpdateCounter--;
    }

    m_transformEpoch++;

    const bool forceAllLevels = hierarchyJustChanged || dirtyUsesBulkPath || fullTransformUpdateActive || (m_animMovingCount > 0);
    std::vector<uint8_t>& levelNeedsProcessing = m_levelNeedsProcessing;
    levelNeedsProcessing.assign(sceneDatabase.m_maxHierarchyDepth + 1, forceAllLevels ? 1u : 0u);

    if (!dirtyUsesBulkPath && !m_pendingDirty[0].empty()) {
        auto& pending = m_pendingDirty[0];
        m_dirtyIndexScratch.clear();
        m_dirtyIndexScratch.reserve(std::min<size_t>(pending.size(), MAX_DIRTY_PER_FRAME));
        for (Entity e : pending) {
            if (m_dirtyIndexScratch.size() >= MAX_DIRTY_PER_FRAME) break;
            if (e < m_maxObjects && e < sceneDatabase.transforms.size()) { m_dirtyIndexScratch.push_back(e); }
        }
        const size_t dirtyCount = m_dirtyIndexScratch.size();
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pDirtyIndexBuffer[m_currentFrame], 0, dirtyCount * sizeof(unsigned int), m_dirtyIndexScratch.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (!hierarchyJustChanged) {
            for (Entity e : m_dirtyIndexScratch) {
                uint32_t lvl = sceneDatabase.hierarchies[e].level;
                if (lvl < levelNeedsProcessing.size()) levelNeedsProcessing[lvl] = true;
            }
            for (size_t l = 1; l < levelNeedsProcessing.size(); ++l) {
                if (levelNeedsProcessing[l - 1]) levelNeedsProcessing[l] = true;
            }
        }

        m_dirtyPayloadScratch.resize(dirtyCount);
        for (size_t i = 0; i < dirtyCount; ++i) { m_dirtyPayloadScratch[i] = sceneDatabase.transforms[m_dirtyIndexScratch[i]].localMatrix; }
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pDirtyPayloadBuffer[m_currentFrame], 0, dirtyCount * sizeof(glm::mat4), m_dirtyPayloadScratch.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        {
            Diligent::MapHelper<ApplyDirtyConstants> ConstData(m_diligent->pImmediateContext, m_diligent->pApplyDirtyConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->dirtyCount = static_cast<uint32_t>(dirtyCount);
            ConstData->epoch = m_transformEpoch;
        }

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pApplyDirtyPSO);
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pApplyDirtySRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const unsigned int applyDirtyWorkgroups = (static_cast<unsigned int>(dirtyCount) + 255) / 256;
        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(applyDirtyWorkgroups, 1, 1));

        pending.clear();
    }

    Timestamp(m_diligent->pUploadEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pAnimStartQuery[m_currentFrame]);

    Timestamp(m_diligent->pAnimEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pTransformStartQuery[m_currentFrame]);

    if (m_meshInfoDirty) {
        const size_t meshInfoCount = std::min<size_t>(s_meshInfos.size(), MAX_MESHES);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pMeshInfoBuffer, 0, meshInfoCount * sizeof(MeshInfo), s_meshInfos.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (!s_normalSamples.empty()) {
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pNormalSampleBuffer, 0, s_normalSamples.size() * sizeof(glm::vec4), s_normalSamples.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        m_meshInfoDirty = false;
    }

    const unsigned int transformWorkgroupSize = 256;

    struct TransformUniforms {
        unsigned int objectCount;
        unsigned int currentHierarchyLevel;
        unsigned int sortedListOffset;
        unsigned int frameOffset;
        unsigned int epoch;
        unsigned int forceFullRecompute;
        float animTime;
        unsigned int animMovingCount;
        unsigned int animEntityOffset;
        unsigned int animPadding;
    } transformUniforms;

    const bool animActive = m_animMovingCount > 0 && m_diligent->pBasePositionBuffer;

    {
        uint32_t currentBaseIndexOffset = 0;

        for (uint32_t level = 0; level <= sceneDatabase.m_maxHierarchyDepth; ++level) {
            uint32_t levelCount = 0;
            if (level < sceneDatabase.m_levelCounts.size()) { levelCount = sceneDatabase.m_levelCounts[level]; }

            if (levelCount == 0) continue;

            if (level >= levelNeedsProcessing.size() || !levelNeedsProcessing[level]) {
                currentBaseIndexOffset += levelCount;
                continue;
            }

            transformUniforms.objectCount = levelCount;
            transformUniforms.currentHierarchyLevel = level;
            transformUniforms.sortedListOffset = currentBaseIndexOffset;
            transformUniforms.frameOffset = 0;
            transformUniforms.epoch = m_transformEpoch;
            transformUniforms.animTime = m_animTime;
            transformUniforms.animMovingCount = animActive ? m_animMovingCount : 0u;
            transformUniforms.animEntityOffset = m_animEntityOffset;
            transformUniforms.animPadding = 0u;
            transformUniforms.forceFullRecompute = (hierarchyJustChanged || dirtyUsesBulkPath || localMatricesUploading) ? 1u : 0u;

            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransformUniforms, 0, sizeof(transformUniforms), &transformUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransformPSO);
            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransformSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            const unsigned int transformNumWorkgroups = (levelCount + transformWorkgroupSize - 1) / transformWorkgroupSize;

            Diligent::DispatchComputeAttribs DispatchAttrs;
            DispatchAttrs.ThreadGroupCountX = transformNumWorkgroups;
            DispatchAttrs.ThreadGroupCountY = 1;
            DispatchAttrs.ThreadGroupCountZ = 1;
            m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);

            Diligent::StateTransitionDesc LevelBarriers[2];
            LevelBarriers[0].pResource = m_diligent->pWorldMatrixBuffer[m_currentFrame];
            LevelBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            LevelBarriers[0].NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            LevelBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
            LevelBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;

            LevelBarriers[1].pResource = m_diligent->pTouchedEpochBuffer[m_currentFrame];
            LevelBarriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            LevelBarriers[1].NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            LevelBarriers[1].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
            LevelBarriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
            m_diligent->pImmediateContext->TransitionResourceStates(2, LevelBarriers);

            currentBaseIndexOffset += levelCount;
        }
    }

    Timestamp(m_diligent->pTransformEndQuery[m_currentFrame]);

    SceneUniforms sceneUniforms;
    sceneUniforms.projection = camera.getProjectionMatrix();
    sceneUniforms.view = camera.getViewMatrix();
    sceneUniforms.lightPos = glm::vec3(0.0f, 10.0f, 0.0f);
    sceneUniforms.viewPos = camera.getPosition();
    sceneUniforms.lightColor = glm::vec3(1.0f, 1.0f, 1.0f);
    extractFrustumPlanes(sceneUniforms.projection * sceneUniforms.view, sceneUniforms.frustumPlanes);

    sceneUniforms.dirLightDir = glm::vec4(glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f)), 1.0f);
    sceneUniforms.dirLightColor = glm::vec4(1.0f, 0.95f, 0.88f, 1.0f);

    sceneUniforms.pointLight0Pos = glm::vec4(-80.0f, 50.0f, -50.0f, 250.0f);
    sceneUniforms.pointLight0Color = glm::vec4(0.1f, 0.75f, 1.0f, 2.5f);

    sceneUniforms.pointLight1Pos = glm::vec4(80.0f, -30.0f, 50.0f, 250.0f);
    sceneUniforms.pointLight1Color = glm::vec4(1.0f, 0.45f, 0.15f, 2.5f);
    const auto& swapChainDesc = m_diligent->pSwapChain->GetDesc();
    sceneUniforms.screenParams = glm::vec4(static_cast<float>(swapChainDesc.Width), static_cast<float>(swapChainDesc.Height), 0.0f, 0.0f);

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &sceneUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const unsigned int workgroupSize = 256;
    const unsigned int numWorkgroups = (numObjects + workgroupSize - 1) / workgroupSize;
    const size_t subPassMaxObjects = std::max<size_t>(m_maxObjects / 4, 1024);

    Timestamp(m_diligent->pLargeObjectCullStartQuery[m_currentFrame]);

    const uint32_t totalBucketCount = static_cast<uint32_t>(m_numDrawingShaders) * MAX_MESHES;

    ResetAtomicCounter(m_diligent->pVisibleLargeObjectAtomicCounter[m_currentFrame]);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pLargeBucketCountBuffer[m_currentFrame], 0, sizeof(unsigned int) * m_bucketZeros.size(), m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_diligent->pImmediateContext->SetPipelineState(m_diligent->pLargeObjectCullPSO);

    {
        Diligent::MapHelper<LargeObjectCullUniforms> Constants(m_diligent->pImmediateContext, m_diligent->pLargeObjectCullConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        Constants->objectCount = numObjects;
        Constants->maxDraws = static_cast<uint32_t>(subPassMaxObjects);
        Constants->largeObjectThreshold = m_largeObjectThreshold;
        Constants->numShaders = static_cast<uint32_t>(m_numDrawingShaders);
    }

    m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pLargeObjectCullSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(numWorkgroups, 1, 1));

    Timestamp(m_diligent->pLargeObjectCullEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pLargeObjectCommandGenStartQuery[m_currentFrame]);

    const bool largeObjectIndirectDispatch =
        m_diligent->pDispatchArgsPSO && m_diligent->pLargeObjectDispatchArgsSRB[m_currentFrame] && m_diligent->pLargeObjectDispatchArgs[m_currentFrame];

    if (numObjects > 0 && m_diligent->pLargePrefixSumSRB[m_currentFrame] && m_diligent->pLargeScatterSRB[m_currentFrame] && m_diligent->pLargeCommandGenSRB[m_currentFrame]) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pPrefixSumPSO);
        {
            Diligent::MapHelper<PrefixSumConstants> ConstData(m_diligent->pImmediateContext, m_diligent->pPrefixSumConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->bucketCount = totalBucketCount;
        }
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pLargePrefixSumSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));

        Diligent::StateTransitionDesc LargePrefixSumBarriers[2];
        LargePrefixSumBarriers[0].pResource = m_diligent->pLargeBucketOffsetBuffer[m_currentFrame];
        LargePrefixSumBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        LargePrefixSumBarriers[0].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        LargePrefixSumBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        LargePrefixSumBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;

        LargePrefixSumBarriers[1].pResource = m_diligent->pLargeBucketWriteHeadBuffer[m_currentFrame];
        LargePrefixSumBarriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        LargePrefixSumBarriers[1].NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        LargePrefixSumBarriers[1].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        LargePrefixSumBarriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(2, LargePrefixSumBarriers);

        if (largeObjectIndirectDispatch) {
            m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDispatchArgsPSO);
            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pLargeObjectDispatchArgsSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));
        }

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pScatterPSO);
        {
            Diligent::MapHelper<ScatterConstants> ConstData(m_diligent->pImmediateContext, m_diligent->pLargeScatterConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->maxDraws = static_cast<uint32_t>(subPassMaxObjects);
        }
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pLargeScatterSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (largeObjectIndirectDispatch) {
            m_diligent->pImmediateContext->DispatchComputeIndirect(
                Diligent::DispatchComputeIndirectAttribs{m_diligent->pLargeObjectDispatchArgs[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        } else {
            m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs((static_cast<unsigned int>(subPassMaxObjects) + 255) / 256, 1, 1));
        }

        Diligent::StateTransitionDesc LargeScatterBarrier;
        LargeScatterBarrier.pResource = m_diligent->pSortedVisibleLargeObjectBuffer[m_currentFrame];
        LargeScatterBarrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        LargeScatterBarrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        LargeScatterBarrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        LargeScatterBarrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(1, &LargeScatterBarrier);

        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pLargeDrawAtomicCounterBuffer[m_currentFrame], 0, sizeof(unsigned int) * m_numDrawingShaders, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pCommandGenPSO);
        {
            Diligent::MapHelper<CommandGenUniforms> ConstData(m_diligent->pImmediateContext, m_diligent->pCommandGenConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->maxBucketsPerShader = MAX_MESHES;
            ConstData->totalBuckets = totalBucketCount;
        }
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pLargeCommandGenSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs((totalBucketCount + 255) / 256, 1, 1));
    }

    Diligent::StateTransitionDesc Barrier;
    Barrier.pResource = m_diligent->pDepthPrepassDrawCommandBuffer[m_currentFrame];
    Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier.NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
    Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);

    Barrier.pResource = m_diligent->pLargeDrawAtomicCounterBuffer[m_currentFrame];
    Barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier.NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
    Barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    Barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    m_diligent->pImmediateContext->TransitionResourceStates(1, &Barrier);

    Timestamp(m_diligent->pLargeObjectCommandGenEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pDepthPrePassStartQuery[m_currentFrame]);

    Diligent::Viewport VP;
    VP.Width = static_cast<float>(m_windowWidth);
    VP.Height = static_cast<float>(m_windowHeight);
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    VP.TopLeftX = 0;
    VP.TopLeftY = 0;
    m_diligent->pImmediateContext->SetViewports(1, &VP, m_windowWidth, m_windowHeight);

    Diligent::ITextureView* pRTVs[] = {m_diligent->pHiZTextures[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
    m_diligent->pImmediateContext->SetRenderTargets(1, pRTVs, m_diligent->pDepthRenderbuffers[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->ClearRenderTarget(pRTVs[0], glm::value_ptr(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->ClearDepthStencil(m_diligent->pDepthRenderbuffers[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDepthPrepassPSO);
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pDepthPrepassSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::IBuffer* pVBs[] = {m_diligent->pVBO};
        m_diligent->pImmediateContext->SetVertexBuffers(0, 1, pVBs, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_diligent->pImmediateContext->SetIndexBuffer(m_diligent->pEBO, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedIndirectAttribs DrawAttrs;
        DrawAttrs.IndexType = Diligent::VT_UINT32;
        DrawAttrs.Flags = Diligent::DRAW_FLAG_NONE;
        DrawAttrs.pAttribsBuffer = m_diligent->pDepthPrepassDrawCommandBuffer[m_currentFrame];
        DrawAttrs.pCounterBuffer = m_diligent->pLargeDrawAtomicCounterBuffer[m_currentFrame];
        DrawAttrs.DrawCount = MAX_MESHES;
        DrawAttrs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
        DrawAttrs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        DrawAttrs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        for (uint32_t shaderId = 0; shaderId < static_cast<uint32_t>(m_numDrawingShaders); ++shaderId) {
            DrawAttrs.DrawArgsOffset = static_cast<Diligent::Uint64>(shaderId) * MAX_MESHES * sizeof(DrawElementsIndirectCommand);
            DrawAttrs.CounterOffset = shaderId * sizeof(unsigned int);
            m_diligent->pImmediateContext->DrawIndexedIndirect(DrawAttrs);
        }
    }

    m_diligent->pImmediateContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

    Timestamp(m_diligent->pDepthPrePassEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pHizMipmapStartQuery[m_currentFrame]);

    if (m_diligent->pHiZMipmapPSO) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pHiZMipmapPSO);
        for (int i = 1; i < m_maxMipLevel && i < 32; ++i) {
            if (m_diligent->pHiZSourceMipVar) m_diligent->pHiZSourceMipVar->Set(m_diligent->pHiZMipViewsSource[m_currentFrame][i]);
            if (m_diligent->pHiZDestMipVar) m_diligent->pHiZDestMipVar->Set(m_diligent->pHiZMipViewsDest[m_currentFrame][i]);

            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pHiZMipmapSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            const int currentMipWidth = std::max(1, m_windowWidth >> i);
            const int currentMipHeight = std::max(1, m_windowHeight >> i);

            Diligent::DispatchComputeAttribs DispatchAttrs;
            DispatchAttrs.ThreadGroupCountX = (currentMipWidth + 7) / 8;
            DispatchAttrs.ThreadGroupCountY = (currentMipHeight + 7) / 8;
            DispatchAttrs.ThreadGroupCountZ = 1;

            m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);

            Diligent::StateTransitionDesc HiZBarrier;
            HiZBarrier.pResource = m_diligent->pHiZTextures[m_currentFrame];
            HiZBarrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            HiZBarrier.NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            HiZBarrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
            HiZBarrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
            m_diligent->pImmediateContext->TransitionResourceStates(1, &HiZBarrier);
        }

        Diligent::StateTransitionDesc HiZSRVBarrier;
        HiZSRVBarrier.pResource = m_diligent->pHiZTextures[m_currentFrame];
        HiZSRVBarrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        HiZSRVBarrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        HiZSRVBarrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        HiZSRVBarrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(1, &HiZSRVBarrier);
    }

    Timestamp(m_diligent->pHizMipmapEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pCullStartQuery[m_currentFrame]);

    ResetAtomicCounter(m_diligent->pVisibleObjectAtomicCounter[m_currentFrame]);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pBucketCountBuffer[m_currentFrame], 0, sizeof(unsigned int) * m_bucketZeros.size(), m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    CullingUniforms cullingUniforms;
    cullingUniforms.objectCount = numObjects;
    cullingUniforms.maxDraws = static_cast<uint32_t>(m_maxObjects);
    cullingUniforms.baseIndex = 0;
    cullingUniforms.smallObjectThreshold = m_smallObjectThreshold;
    cullingUniforms.hizMaxMipLevel = static_cast<float>(m_maxMipLevel - 1);
    cullingUniforms.hizTextureSizeX = static_cast<float>(m_windowWidth);
    cullingUniforms.hizTextureSizeY = static_cast<float>(m_windowHeight);
    cullingUniforms.numShaders = static_cast<uint32_t>(m_numDrawingShaders);
    cullingUniforms.lodBias = m_lodBias;
    cullingUniforms.forcedLod = m_forcedLod;

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pCullingUniforms, 0, sizeof(cullingUniforms), &cullingUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (auto* pHiZVar = m_diligent->pCullHiZTextureVar[m_currentFrame]) { pHiZVar->Set(m_diligent->pHiZTextures[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)); }

    m_diligent->pImmediateContext->SetPipelineState(m_diligent->pCullingPSO);
    m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pCullingSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DispatchComputeAttribs CullDispatchAttrs;
    CullDispatchAttrs.ThreadGroupCountX = numWorkgroups;
    CullDispatchAttrs.ThreadGroupCountY = 1;
    CullDispatchAttrs.ThreadGroupCountZ = 1;
    m_diligent->pImmediateContext->DispatchCompute(CullDispatchAttrs);

    Diligent::StateTransitionDesc CullBarriers[2];
    CullBarriers[0].pResource = m_diligent->pVisibleObjectAtomicCounter[m_currentFrame];
    CullBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    CullBarriers[0].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
    CullBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    CullBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;

    CullBarriers[1].pResource = m_diligent->pVisibleObjectBuffer[m_currentFrame];
    CullBarriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    CullBarriers[1].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
    CullBarriers[1].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    CullBarriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    m_diligent->pImmediateContext->TransitionResourceStates(2, CullBarriers);

    Timestamp(m_diligent->pCullEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pCommandGenStartQuery[m_currentFrame]);

    const uint32_t totalBuckets = static_cast<uint32_t>(m_numDrawingShaders) * MAX_MESHES;

    if (numObjects > 0) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pPrefixSumPSO);
        {
            Diligent::MapHelper<PrefixSumConstants> ConstData(m_diligent->pImmediateContext, m_diligent->pPrefixSumConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->bucketCount = totalBuckets;
        }
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pPrefixSumSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));

        Diligent::StateTransitionDesc PrefixSumBarriers[2];
        PrefixSumBarriers[0].pResource = m_diligent->pBucketOffsetBuffer[m_currentFrame];
        PrefixSumBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        PrefixSumBarriers[0].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        PrefixSumBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        PrefixSumBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;

        PrefixSumBarriers[1].pResource = m_diligent->pBucketWriteHeadBuffer[m_currentFrame];
        PrefixSumBarriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        PrefixSumBarriers[1].NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        PrefixSumBarriers[1].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        PrefixSumBarriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(2, PrefixSumBarriers);

        const bool scatterIndirectDispatch =
            m_diligent->pDispatchArgsPSO && m_diligent->pScatterDispatchArgsSRB[m_currentFrame] && m_diligent->pScatterDispatchArgs[m_currentFrame];

        if (scatterIndirectDispatch) {
            m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDispatchArgsPSO);
            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pScatterDispatchArgsSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));
        }

        m_diligent->pScatterPSO ? m_diligent->pImmediateContext->SetPipelineState(m_diligent->pScatterPSO) : void();
        {
            Diligent::MapHelper<ScatterConstants> ConstData(m_diligent->pImmediateContext, m_diligent->pScatterConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->maxDraws = static_cast<uint32_t>(m_maxObjects);
        }
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pScatterSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (scatterIndirectDispatch) {
            m_diligent->pImmediateContext->DispatchComputeIndirect(
                Diligent::DispatchComputeIndirectAttribs{m_diligent->pScatterDispatchArgs[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        } else {
            m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(numWorkgroups, 1, 1));
        }

        Diligent::StateTransitionDesc ScatterBarriers[1];
        ScatterBarriers[0].pResource = m_diligent->pSortedVisibleObjectBuffer[m_currentFrame];
        ScatterBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        ScatterBarriers[0].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        ScatterBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        ScatterBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(1, ScatterBarriers);
    }

    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pDrawAtomicCounterBuffer[m_currentFrame], 0, sizeof(unsigned int) * m_numDrawingShaders, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pPointDrawCounterBuffer[m_currentFrame], 0, sizeof(unsigned int) * m_numDrawingShaders, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (numObjects > 0) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pCommandGenPSO);

        {
            Diligent::MapHelper<CommandGenUniforms> ConstData(m_diligent->pImmediateContext, m_diligent->pCommandGenConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            ConstData->maxBucketsPerShader = MAX_MESHES;
            ConstData->totalBuckets = totalBuckets;
        }

        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pCommandGenSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const unsigned int commandGenWorkgroupSize = 256;
        const unsigned int numCommandGenWorkgroups = (totalBuckets + commandGenWorkgroupSize - 1) / commandGenWorkgroupSize;
        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(numCommandGenWorkgroups, 1, 1));

        Diligent::StateTransitionDesc CmdGenBarriers[4];
        CmdGenBarriers[2].pResource = m_diligent->pPointCommandBuffer[m_currentFrame];
        CmdGenBarriers[2].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        CmdGenBarriers[2].NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
        CmdGenBarriers[2].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        CmdGenBarriers[2].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        CmdGenBarriers[3].pResource = m_diligent->pPointDrawCounterBuffer[m_currentFrame];
        CmdGenBarriers[3].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        CmdGenBarriers[3].NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
        CmdGenBarriers[3].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        CmdGenBarriers[3].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        CmdGenBarriers[0].pResource = m_diligent->pDrawCommandBuffer[m_currentFrame];
        CmdGenBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        CmdGenBarriers[0].NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
        CmdGenBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        CmdGenBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;

        CmdGenBarriers[1].pResource = m_diligent->pDrawAtomicCounterBuffer[m_currentFrame];
        CmdGenBarriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        CmdGenBarriers[1].NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
        CmdGenBarriers[1].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        CmdGenBarriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(4, CmdGenBarriers);
    }

    Timestamp(m_diligent->pCommandGenEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pOpaqueDrawStartQuery[m_currentFrame]);

    auto* pRTV = m_diligent->pSwapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_diligent->pSwapChain->GetDepthBufferDSV();

    const auto& SCDesc = m_diligent->pSwapChain->GetDesc();
    Diligent::Viewport SwapChainVP;
    SwapChainVP.Width = static_cast<float>(SCDesc.Width);
    SwapChainVP.Height = static_cast<float>(SCDesc.Height);
    SwapChainVP.MinDepth = 0.0f;
    SwapChainVP.MaxDepth = 1.0f;
    SwapChainVP.TopLeftX = 0;
    SwapChainVP.TopLeftY = 0;
    m_diligent->pImmediateContext->SetViewports(1, &SwapChainVP, SCDesc.Width, SCDesc.Height);

    m_diligent->pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->ClearRenderTarget(pRTV, glm::value_ptr(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_diligent->pImmediateContext->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    for (uint32_t shaderId = 0; shaderId < m_diligent->pOpaquePSOs.size(); ++shaderId) {
        if (!m_diligent->pOpaquePSOs[shaderId]) continue;

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pOpaquePSOs[shaderId]);
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pOpaqueSRBs[m_currentFrame][shaderId], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedIndirectAttribs DrawAttrs;
        DrawAttrs.IndexType = Diligent::VT_UINT32;
        DrawAttrs.Flags = Diligent::DRAW_FLAG_NONE;
        DrawAttrs.DrawArgsOffset = static_cast<Diligent::Uint64>(shaderId) * MAX_MESHES * sizeof(DrawElementsIndirectCommand);
        DrawAttrs.pAttribsBuffer = m_diligent->pDrawCommandBuffer[m_currentFrame];
        DrawAttrs.DrawCount = MAX_MESHES;
        DrawAttrs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
        DrawAttrs.pCounterBuffer = m_diligent->pDrawAtomicCounterBuffer[m_currentFrame];
        DrawAttrs.CounterOffset = shaderId * sizeof(unsigned int);
        DrawAttrs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        DrawAttrs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_diligent->pImmediateContext->DrawIndexedIndirect(DrawAttrs);

        if (shaderId < m_diligent->pPointPSOs.size() && m_diligent->pPointPSOs[shaderId]) {
            m_diligent->pImmediateContext->SetPipelineState(m_diligent->pPointPSOs[shaderId]);
            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pPointSRBs[m_currentFrame][shaderId], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            Diligent::DrawIndirectAttribs PointAttrs;
            PointAttrs.Flags = Diligent::DRAW_FLAG_NONE;
            PointAttrs.DrawArgsOffset = static_cast<Diligent::Uint64>(shaderId) * MAX_MESHES * sizeof(DrawElementsIndirectCommand);
            PointAttrs.pAttribsBuffer = m_diligent->pPointCommandBuffer[m_currentFrame];
            PointAttrs.DrawCount = MAX_MESHES;
            PointAttrs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
            PointAttrs.pCounterBuffer = m_diligent->pPointDrawCounterBuffer[m_currentFrame];
            PointAttrs.CounterOffset = shaderId * sizeof(unsigned int);
            PointAttrs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            PointAttrs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

            m_diligent->pImmediateContext->DrawIndirect(PointAttrs);
        }
    }

    Timestamp(m_diligent->pOpaqueDrawEndQuery[m_currentFrame]);

    ResetAtomicCounter(m_diligent->pTransparentAtomicCounter[m_currentFrame]);

    Timestamp(m_diligent->pTransparentCullStartQuery[m_currentFrame]);
    {
        TransparentCullUniforms uniforms{};
        uniforms.objectCount = m_transparentIdCounts[0];
        uniforms.maxDraws = static_cast<uint32_t>(subPassMaxObjects);
        uniforms.lodBias = m_lodBias;
        uniforms.forcedLod = m_forcedLod;
        uniforms.cameraPos = camera.getPosition();
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentCullUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransparentCullPSO);

        if (auto* pVar = m_diligent->pTransparentCullSRB[m_currentFrame]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "u_hizTexture")) {
            pVar->Set(m_diligent->pHiZTextures[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
        }

        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransparentCullSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DispatchComputeAttribs DispatchAttrs;
        DispatchAttrs.ThreadGroupCountX = std::max(1u, (m_transparentIdCounts[0] + 255) / 256);
        DispatchAttrs.ThreadGroupCountY = 1;
        DispatchAttrs.ThreadGroupCountZ = 1;
        m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);

        Diligent::StateTransitionDesc TransCullBarriers[2];
        TransCullBarriers[0].pResource = m_diligent->pTransparentAtomicCounter[m_currentFrame];
        TransCullBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        TransCullBarriers[0].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        TransCullBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        TransCullBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;

        TransCullBarriers[1].pResource = m_diligent->pVisibleTransparentObjectIdsBuffer[m_currentFrame];
        TransCullBarriers[1].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        TransCullBarriers[1].NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        TransCullBarriers[1].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        TransCullBarriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        m_diligent->pImmediateContext->TransitionResourceStates(2, TransCullBarriers);
    }

    Timestamp(m_diligent->pTransparentCullEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pTransparentCommandGenStartQuery[m_currentFrame]);

    const bool transparentIndirectDispatch =
        m_diligent->pDispatchArgsPSO && m_diligent->pTransparentDispatchArgsSRB[m_currentFrame] && m_diligent->pTransparentDispatchArgs[m_currentFrame];

    if (transparentIndirectDispatch) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDispatchArgsPSO);
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransparentDispatchArgsSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));
    }

    m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransparentCommandGenPSO);
    m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransparentCommandGenSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (numObjects > 0) {
        if (transparentIndirectDispatch) {
            m_diligent->pImmediateContext->DispatchComputeIndirect(
                Diligent::DispatchComputeIndirectAttribs{m_diligent->pTransparentDispatchArgs[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        } else {
            Diligent::DispatchComputeAttribs DispatchAttrs;
            DispatchAttrs.ThreadGroupCountX = (static_cast<unsigned int>(subPassMaxObjects) + workgroupSize - 1) / workgroupSize;
            DispatchAttrs.ThreadGroupCountY = 1;
            DispatchAttrs.ThreadGroupCountZ = 1;
            m_diligent->pImmediateContext->DispatchCompute(DispatchAttrs);
        }
    }

    Diligent::StateTransitionDesc TransCmdBarriers[2];
    TransCmdBarriers[0].pResource = m_diligent->pTransparentDrawCommandBuffer[m_currentFrame];
    TransCmdBarriers[0].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    TransCmdBarriers[0].NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
    TransCmdBarriers[0].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    TransCmdBarriers[0].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;

    TransCmdBarriers[1].pResource = m_diligent->pTransparentAtomicCounter[m_currentFrame];
    TransCmdBarriers[1].OldState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
    TransCmdBarriers[1].NewState = Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
    TransCmdBarriers[1].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    TransCmdBarriers[1].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    m_diligent->pImmediateContext->TransitionResourceStates(2, TransCmdBarriers);

    Timestamp(m_diligent->pTransparentCommandGenEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pTransparentDrawStartQuery[m_currentFrame]);

    if (numObjects > 0) {
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransparentPSO);
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pTransparentSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedIndirectAttribs DrawAttrs;
        DrawAttrs.IndexType = Diligent::VT_UINT32;
        DrawAttrs.Flags = Diligent::DRAW_FLAG_NONE;
        DrawAttrs.DrawArgsOffset = 0;
        DrawAttrs.pAttribsBuffer = m_diligent->pTransparentDrawCommandBuffer[m_currentFrame];
        DrawAttrs.pCounterBuffer = m_diligent->pTransparentAtomicCounter[m_currentFrame];
        DrawAttrs.CounterOffset = 0;
        DrawAttrs.DrawCount = static_cast<Diligent::Uint32>(std::min<size_t>(numObjects, subPassMaxObjects));
        DrawAttrs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
        DrawAttrs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        DrawAttrs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_diligent->pImmediateContext->DrawIndexedIndirect(DrawAttrs);

        Timestamp(m_diligent->pTransparentDrawEndQuery[m_currentFrame]);
        m_diligent->TransparentDrawActive[m_currentFrame] = true;
    } else {
        m_diligent->TransparentDrawActive[m_currentFrame] = false;
    }

    Timestamp(m_diligent->pUiStartQuery[m_currentFrame]);

    if (m_debugDepthMode && m_diligent->pDebugDepthPSO && m_diligent->pDebugDepthSRB) {
        struct DebugDepthUniforms {
            float nearPlane;
            float farPlane;
            float mipLevel;
            float padding;
        };

        DebugDepthUniforms debugUniforms;
        debugUniforms.nearPlane = camera.getNearPlane();
        debugUniforms.farPlane = camera.getFarPlane();
        debugUniforms.mipLevel = 0.0f;
        debugUniforms.padding = 0.0f;

        Diligent::MapHelper<DebugDepthUniforms> pData(m_diligent->pImmediateContext, m_diligent->pDebugDepthUniforms, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        *pData = debugUniforms;

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDebugDepthPSO);

        if (auto* var = m_diligent->pDebugDepthSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "u_depthTexture")) var->Set(m_diligent->pHiZTextures[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        if (auto* var = m_diligent->pDebugDepthSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "DebugDepthUniforms")) var->Set(m_diligent->pDebugDepthUniforms, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pDebugDepthSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawAttribs DrawAttrs;
        DrawAttrs.NumVertices = 3;
        DrawAttrs.Flags = Diligent::DRAW_FLAG_NONE;
        m_diligent->pImmediateContext->Draw(DrawAttrs);
    }

    if (!m_debugLines.empty() && m_diligent->pDebugLinePSO && m_diligent->pDebugLineSRB) {
        constexpr size_t kMaxDebugVertices = 1u << 17;
        constexpr size_t kFloatsPerVertex = sizeof(DebugLineVertex) / sizeof(float);
        const size_t vertexCount = std::min(m_debugLines.size() / kFloatsPerVertex, kMaxDebugVertices);
        {
            Diligent::MapHelper<DebugLineVertex> vertices(m_diligent->pImmediateContext, m_diligent->pDebugLineVB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            std::memcpy(static_cast<DebugLineVertex*>(vertices), m_debugLines.data(), vertexCount * sizeof(DebugLineVertex));
        }
        {
            Diligent::MapHelper<glm::mat4> viewProjection(m_diligent->pImmediateContext, m_diligent->pDebugLineUBO, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            *viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
        }
        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDebugLinePSO);
        m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pDebugLineSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::IBuffer* pBuffers[] = {m_diligent->pDebugLineVB};
        const Diligent::Uint64 offsets[] = {0};
        m_diligent->pImmediateContext->SetVertexBuffers(0, 1, pBuffers, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        Diligent::DrawAttribs lineDraw;
        lineDraw.NumVertices = static_cast<Diligent::Uint32>(vertexCount);
        lineDraw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        m_diligent->pImmediateContext->Draw(lineDraw);
    }
    m_debugLines.clear();

    if (fullProfiling) {
        float y = 120.0f;
        const float step = 18.0f;
        const float scale = 0.4f;
        const glm::vec3 colHeader(0.3f, 1.0f, 0.5f);
        const glm::vec3 colText(0.9f, 0.9f, 0.9f);
        const glm::vec3 colWait(1.0f, 0.4f, 0.4f);

        char buf[128];
        auto line = [&](const glm::vec3& color, const char* label, double milliseconds) {
            const auto result = std::format_to_n(buf, sizeof(buf), "{}{:.3f} ms", label, milliseconds);
            m_uiManager->addText(std::string_view(buf, static_cast<size_t>(result.out - buf)), 10.0f, y, scale, color);
            y += step;
        };

        m_uiManager->addText("--- Profiling HUD ---", 10.0f, y, scale, colHeader); y += step;
        line(colWait,   "CPU Fence Wait: ",    s_lastFenceWaitMs);
        line(colHeader, "GPU Total: ",         s_lastGpuTotal);
        line(colText,   "  Upload: ",          s_lastUploadTime);
        line(colText,   "  GPU Anim: ",        s_lastAnimTime);
        line(colText,   "  Transform: ",       s_lastTransformTime);
        line(colText,   "  Large Obj Cull: ",  s_lastLargeObjCullTime);
        line(colText,   "  Large Obj CmdGen: ", s_lastLargeObjCmdGenTime);
        line(colText,   "  Depth Pre-Pass: ",  s_lastDepthPrePassTime);
        line(colText,   "  Hi-Z Mipmap: ",     s_lastHizTime);
        line(colText,   "  Opaque Cull: ",     s_lastOpaqueCullTime);
        line(colText,   "  Opaque CmdGen: ",   s_lastOpaqueCmdGenTime);
        line(colText,   "  Opaque Draw: ",     s_lastOpaqueDrawTime);
        line(colText,   "  Trans Cull: ",      s_lastTransCullTime);
        line(colText,   "  Trans CmdGen: ",    s_lastTransCmdGenTime);
        line(colText,   "  Trans Draw: ",      s_lastTransDrawTime);
        line(colText,   "  UI Pass: ",         s_lastUiTime);
    }

    m_uiManager->render();

    Timestamp(m_diligent->pUiEndQuery[m_currentFrame]);
    Timestamp(m_diligent->pFrameEndQuery[m_currentFrame]);
    m_diligent->QueryReady[m_currentFrame] = profileThisFrame;

    m_diligent->CurrentFenceValue++;
    m_diligent->pImmediateContext->EnqueueSignal(m_diligent->pFences[m_currentFrame], m_diligent->CurrentFenceValue);
    m_diligent->FenceValues[m_currentFrame] = m_diligent->CurrentFenceValue;
}

void Renderer::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) { m_uiManager->addText(text, x, y, scale, color); }

void Renderer::createTransformPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/transform.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load transform compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) { source = source.substr(nextLine + 1); }
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
        if (nextLine != std::string::npos) { source = source.substr(nextLine + 1); }
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
        {Diligent::SHADER_TYPE_COMPUTE, "TransparentCullUniforms",        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer",            Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "CullSphereBuffer",              Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TransparentIdBuffer",            Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "u_hizTexture",                   Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
    PSODesc.PSODesc.ResourceLayout.Variables = Vars.data();
    PSODesc.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pTransparentCullPSO.Release();
    m_diligent->pDevice->CreateComputePipelineState(PSODesc, &m_diligent->pTransparentCullPSO);
    if (!m_diligent->pTransparentCullPSO) {
        Lit::Log::Error("Failed to create Transparent Cull PSO");
    } else {
        for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pTransparentCullPSO->CreateShaderResourceBinding(&m_diligent->pTransparentCullSRB[f], true); }
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
        if (nextLine != std::string::npos) { source = source.substr(nextLine + 1); }
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
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleTransparentObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer",                 Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer",               Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TransparentDrawCommandBuffer",   Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer",            Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TransparentCommandGenUniforms",  Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    PSODesc.PSODesc.ResourceLayout.Variables = Vars.data();
    PSODesc.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pTransparentCommandGenPSO.Release();
    m_diligent->pDevice->CreateComputePipelineState(PSODesc, &m_diligent->pTransparentCommandGenPSO);
    if (!m_diligent->pTransparentCommandGenPSO) {
        Lit::Log::Error("Failed to create Transparent Command Gen PSO");
    } else {
        for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pTransparentCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pTransparentCommandGenSRB[f], true); }
    }
}

void Renderer::createLargeObjectCommandGenPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/large_object_command_gen.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load large object command gen compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) { source = source.substr(nextLine + 1); }
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Large Object Command Gen CS";
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create large object command gen shader");
        return;
    }

    Diligent::ComputePipelineStateCreateInfo PSODesc;
    PSODesc.PSODesc.Name = "Large Object Command Gen PSO";
    PSODesc.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSODesc.pCS = pCS;

    PSODesc.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_COMPUTE, "LargeObjectCommandGenUniforms",   Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectAtomicCounter", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer",             Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer",               Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer",                  Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer",                Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer",        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    PSODesc.PSODesc.ResourceLayout.Variables = Vars.data();
    PSODesc.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pLargeObjectCommandGenPSO.Release();
    m_diligent->pDevice->CreateComputePipelineState(PSODesc, &m_diligent->pLargeObjectCommandGenPSO);
    if (!m_diligent->pLargeObjectCommandGenPSO) {
        Lit::Log::Error("Failed to create Large Object Command Gen PSO");
    } else {
        for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pLargeObjectCommandGenPSO->CreateShaderResourceBinding(&m_diligent->pLargeObjectCommandGenSRB[f], true); }
    }
}

void Renderer::createDepthPrepassPSO() {
    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Depth Prepass PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = Diligent::TEX_FORMAT_R32_FLOAT;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = true;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    std::string vertSource = LoadSourceFromFile("resources/shaders/depth_prepass.vert");
    std::string fragSource = LoadSourceFromFile("resources/shaders/depth_prepass.frag");

    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
        ShaderCI.Desc.Name = "Depth Prepass VS";
        size_t versionPos = vertSource.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = vertSource.find('\n', versionPos);
            if (nextLine != std::string::npos) vertSource = vertSource.substr(nextLine + 1);
        }
        ShaderCI.Source = vertSource.c_str();
        m_diligent->pDevice->CreateShader(ShaderCI, &pVS);
        if (!pVS) {
            Lit::Log::Error("Failed to create Depth Prepass VS");
            return;
        }
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name = "Depth Prepass PS";
        size_t versionPos = fragSource.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = fragSource.find('\n', versionPos);
            if (nextLine != std::string::npos) fragSource = fragSource.substr(nextLine + 1);
        }
        ShaderCI.Source = fragSource.c_str();
        m_diligent->pDevice->CreateShader(ShaderCI, &pPS);
        if (!pPS) {
            Lit::Log::Error("Failed to create Depth Prepass PS");
            return;
        }
    }

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    Diligent::LayoutElement LayoutElems[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false, 0, 6 * sizeof(float)}
    };
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_VERTEX, "SceneData",                Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "WorldMatrixBuffer",        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "VisibleLargeObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars.data();
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pDepthPrepassPSO.Release();
    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pDepthPrepassPSO);

    if (!m_diligent->pDepthPrepassPSO) {
        Lit::Log::Error("Failed to create Depth Prepass PSO");
    } else {
        for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pDepthPrepassPSO->CreateShaderResourceBinding(&m_diligent->pDepthPrepassSRB[f], true); }
    }
}

void Renderer::createOpaquePSOs() {
    m_diligent->pOpaquePSOs.clear();
    m_diligent->pOpaqueSRBs.clear();
    m_diligent->pPointPSOs.clear();
    m_diligent->pPointSRBs.clear();

    struct ShaderInfo {
        std::string vert;
        std::string frag;
        std::string name;
    };
    std::vector<ShaderInfo> shaderInfos = {
        {"resources/shaders/cube.vert", "resources/shaders/cube.frag",        "Cube PSO"             },
        {"resources/shaders/cube.vert", "resources/shaders/red.frag",         "Red PSO"              },
        {"resources/shaders/cube.vert", "resources/shaders/transparent.frag", "Transparent Proxy PSO"}
    };

    m_diligent->pOpaquePSOs.resize(shaderInfos.size());
    m_diligent->pOpaqueSRBs.resize(DiligentData::NumFrames);
    for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pOpaqueSRBs[f].resize(shaderInfos.size()); }
    m_diligent->pPointPSOs.resize(shaderInfos.size());
    m_diligent->pPointSRBs.resize(DiligentData::NumFrames);
    for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pPointSRBs[f].resize(shaderInfos.size()); }

    for (size_t i = 0; i < shaderInfos.size(); ++i) {
      for (int variant = 0; variant < 2; ++variant) {
        const bool point = variant == 1;
        auto& pso = point ? m_diligent->pPointPSOs[i] : m_diligent->pOpaquePSOs[i];
        auto& srbs = point ? m_diligent->pPointSRBs : m_diligent->pOpaqueSRBs;
        const std::string psoName = shaderInfos[i].name + (point ? " (points)" : "");
        Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PSOCreateInfo.PSODesc.Name = psoName.c_str();
        PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;

        PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = m_diligent->pSwapChain->GetDesc().ColorBufferFormat;
        PSOCreateInfo.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
        PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = point ? Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST : Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = true;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;

        Diligent::ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
        ShaderCI.Desc.UseCombinedTextureSamplers = true;

        std::string vertSource = LoadSourceFromFile(point ? std::string("resources/shaders/point.vert") : shaderInfos[i].vert);
        std::string fragSource = LoadSourceFromFile(shaderInfos[i].frag);

        Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
            ShaderCI.Desc.Name = "Opaque VS";
            size_t versionPos = vertSource.find("#version");
            if (versionPos != std::string::npos) {
                size_t nextLine = vertSource.find('\n', versionPos);
                if (nextLine != std::string::npos) vertSource = vertSource.substr(nextLine + 1);
            }
            ShaderCI.Source = vertSource.c_str();
            m_diligent->pDevice->CreateShader(ShaderCI, &pVS);
        }

        Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
            ShaderCI.Desc.Name = "Opaque PS";
            size_t versionPos = fragSource.find("#version");
            if (versionPos != std::string::npos) {
                size_t nextLine = fragSource.find('\n', versionPos);
                if (nextLine != std::string::npos) fragSource = fragSource.substr(nextLine + 1);
            }
            ShaderCI.Source = fragSource.c_str();
            const Diligent::ShaderMacro pointMacros[] = {{"POINT_SPRITE", "1"}, {nullptr, nullptr}};
            ShaderCI.Macros = point ? Diligent::ShaderMacroArray{pointMacros, 1} : Diligent::ShaderMacroArray{};
            m_diligent->pDevice->CreateShader(ShaderCI, &pPS);
            ShaderCI.Macros = {};
        }

        if (!pVS || !pPS) {
            Lit::Log::Error("Failed to create shaders for PSO: {}", psoName);
            continue;
        }

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        Diligent::LayoutElement LayoutElems[] = {
            Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
            Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false}
        };
        PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
        PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = point ? 0 : _countof(LayoutElems);

        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

        std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
            {Diligent::SHADER_TYPE_VERTEX, "SceneData",           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {Diligent::SHADER_TYPE_PIXEL,  "SceneData",           Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {Diligent::SHADER_TYPE_VERTEX, "VisibleObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
        };
        if (!point) { Vars.push_back({Diligent::SHADER_TYPE_VERTEX, "WorldMatrixBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}); }
        if (point) {
            Vars.push_back({Diligent::SHADER_TYPE_VERTEX, "CullOrientationBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
            Vars.push_back({Diligent::SHADER_TYPE_VERTEX, "CullSphereBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
            Vars.push_back({Diligent::SHADER_TYPE_VERTEX, "NormalSampleBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});
        }
        PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars.data();
        PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = Vars.size();

        pso.Release();
        m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pso);

        if (!pso) {
            Lit::Log::Error("Failed to create Opaque PSO: {}", psoName);
        } else {
            for (int f = 0; f < DiligentData::NumFrames; ++f) { pso->CreateShaderResourceBinding(&srbs[f][i], true); }
        }
      }
    }
}

void Renderer::createTransparentPSO() {
    m_diligent->pTransparentPSO.Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pTransparentSRB[f].Release(); }

    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Transparent PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;

    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = m_diligent->pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = true;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;

    auto& RT0 = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    RT0.BlendEnable = true;
    RT0.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    RT0.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOp = Diligent::BLEND_OPERATION_ADD;
    RT0.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    RT0.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    std::string vertSource = LoadSourceFromFile("resources/shaders/transparent.vert");
    std::string fragSource = LoadSourceFromFile("resources/shaders/transparent.frag");

    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
        ShaderCI.Desc.Name = "Transparent VS";
        size_t versionPos = vertSource.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = vertSource.find('\n', versionPos);
            if (nextLine != std::string::npos) vertSource = vertSource.substr(nextLine + 1);
        }
        ShaderCI.Source = vertSource.c_str();
        m_diligent->pDevice->CreateShader(ShaderCI, &pVS);
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name = "Transparent PS";
        size_t versionPos = fragSource.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = fragSource.find('\n', versionPos);
            if (nextLine != std::string::npos) fragSource = fragSource.substr(nextLine + 1);
        }
        ShaderCI.Source = fragSource.c_str();
        m_diligent->pDevice->CreateShader(ShaderCI, &pPS);
    }

    if (!pVS || !pPS) {
        Lit::Log::Error("Failed to create Global Transparent shaders");
        return;
    }

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    Diligent::LayoutElement LayoutElems[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
        Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false}
    };
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_VERTEX, "SceneData",                      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_PIXEL,  "SceneData",                      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "WorldMatrixBuffer",              Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "CullSphereBuffer",               Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_VERTEX, "VisibleTransparentObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars.data();
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = Vars.size();

    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pTransparentPSO);

    if (m_diligent->pTransparentPSO) {
        for (int f = 0; f < DiligentData::NumFrames; ++f) { m_diligent->pTransparentPSO->CreateShaderResourceBinding(&m_diligent->pTransparentSRB[f], true); }
    } else {
        Lit::Log::Error("Failed to create Transparent PSO");
    }
}

void Renderer::createHiZPSO() {
    Diligent::ComputePipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Hi-Z Mipmap PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "u_sourceMip", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {Diligent::SHADER_TYPE_COMPUTE, "u_destMip",   Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };

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
        if (nextLine != std::string::npos) { source = source.substr(nextLine + 1); }
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
        if (nextLine != std::string::npos) { source = source.substr(nextLine + 1); }
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
        {Diligent::SHADER_TYPE_COMPUTE, "u_hizTexture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };

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

void Renderer::createCommandGenPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/command_gen.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load command generation compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Command Gen CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "DrawAtomicCounterBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "PointCounterBuffer",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "PointCommandBuffer",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "DrawCommandBuffer",       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MeshInfoBuffer",          Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "BucketCountBuffer",       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "BucketOffsetBuffer",      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "CommandGenConstants",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

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

void Renderer::createPrefixSumPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/prefix_sum.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load prefix sum compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Prefix Sum CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "PrefixSumConstants",    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "BucketCountBuffer",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "BucketOffsetBuffer",    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "BucketWriteHeadBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Prefix Sum PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pPrefixSumPSO);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Prefix Sum Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(PrefixSumConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pPrefixSumConstants);
}

void Renderer::createScatterPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/scatter.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load scatter compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Scatter CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "ScatterConstants",          Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "AtomicCounterBuffer",       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleObjectBuffer",       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "BucketWriteHeadBuffer",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "SortedVisibleObjectBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Scatter PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pScatterPSO);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Scatter Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(ScatterConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pScatterConstants);

    CBDesc.Name = "Large Object Scatter Constants";
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pLargeScatterConstants);
}

void Renderer::createDispatchArgsPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/dispatch_args.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load dispatch args compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Dispatch Args CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) {
        Lit::Log::Error("Failed to create dispatch args compute shader.");
        return;
    }

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "SourceCounterBuffer",   Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "DispatchArgsBuffer",    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Dispatch Args PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pDispatchArgsPSO);
    if (!m_diligent->pDispatchArgsPSO) {
        Lit::Log::Error("Failed to create dispatch args PSO.");
        return;
    }

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Dispatch Args Constants";
    CBDesc.Usage = Diligent::USAGE_DEFAULT;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.Size = sizeof(DispatchArgsConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pDispatchArgsConstants);

    CBDesc.Name = "Scatter Dispatch Args Constants";
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pScatterDispatchArgsConstants);
}

void Renderer::createApplyDirtyPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/apply_dirty.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load apply dirty compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Apply Dirty CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "DirtyIndexBuffer",    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "DirtyPayloadBuffer",  Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TransformBuffer",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TouchedEpochBuffer",  Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "ApplyDirtyConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Apply Dirty PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pApplyDirtyPSO);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Apply Dirty Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(ApplyDirtyConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pApplyDirtyConstants);
}

void Renderer::createMarkTouchedPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/mark_touched.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load mark touched compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Mark Touched CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "DirtyIndexBuffer",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TouchedEpochBuffer",   Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "MarkTouchedConstants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Mark Touched PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pMarkTouchedPSO);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Mark Touched Constants";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(MarkTouchedConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pMarkTouchedConstants);
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
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Large Object Cull CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "SceneUniforms",                   Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectAtomicCounter", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "VisibleLargeObjectBuffer",        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "CullSphereBuffer",               Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "RenderableBuffer",                Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "LargeObjectCullConstants",        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "LargeBucketCountBuffer",          Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

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

void Renderer::createDebugDepthPSO() {
    std::string vsSource = LoadSourceFromFile("resources/shaders/debug_depth.vert");
    std::string fsSource = LoadSourceFromFile("resources/shaders/debug_depth.frag");
    if (vsSource.empty() || fsSource.empty()) {
        Lit::Log::Error("Failed to load debug depth shaders");
        return;
    }

    auto stripVersion = [](std::string& src) {
        size_t versionPos = src.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = src.find('\n', versionPos);
            if (nextLine != std::string::npos) src = src.substr(nextLine + 1);
        }
    };
    stripVersion(vsSource);
    stripVersion(fsSource);

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;

    ShaderCI.Source = vsSource.c_str();
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    ShaderCI.Desc.Name = "Debug Depth VS";
    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pVS);

    ShaderCI.Source = fsSource.c_str();
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    ShaderCI.Desc.Name = "Debug Depth PS";
    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pPS);

    if (!pVS || !pPS) {
        Lit::Log::Error("Failed to compile debug depth shaders");
        return;
    }

    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Debug Depth PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = m_diligent->pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = m_diligent->pSwapChain->GetDesc().DepthBufferFormat;
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_PIXEL, "u_depthTexture",     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_PIXEL, "DebugDepthUniforms", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    Diligent::ImmutableSamplerDesc Samplers[] = {
        {Diligent::SHADER_TYPE_PIXEL, "u_depthTexture", Diligent::SamplerDesc{Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP}}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = Samplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(Samplers);

    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pDebugDepthPSO);
    if (!m_diligent->pDebugDepthPSO) {
        Lit::Log::Error("Failed to create Debug Depth PSO");
        return;
    }

    struct DebugDepthUniforms {
        float nearPlane;
        float farPlane;
        float mipLevel;
        float padding;
    };

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Debug Depth Uniforms";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(DebugDepthUniforms);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pDebugDepthUniforms);

    m_diligent->pDebugDepthPSO->CreateShaderResourceBinding(&m_diligent->pDebugDepthSRB, true);
}

void Renderer::addDebugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) {
    const DebugLineVertex vertices[2] = {{from, color}, {to, color}};
    const float* raw = reinterpret_cast<const float*>(vertices);
    m_debugLines.insert(m_debugLines.end(), raw, raw + 2 * sizeof(DebugLineVertex) / sizeof(float));
}

void Renderer::createDebugLinePSO() {
    std::string vsSource = LoadSourceFromFile("resources/shaders/debug_line.vert");
    std::string fsSource = LoadSourceFromFile("resources/shaders/debug_line.frag");
    if (vsSource.empty() || fsSource.empty()) {
        Lit::Log::Error("Failed to load debug line shaders");
        return;
    }

    auto stripVersion = [](std::string& src) {
        size_t versionPos = src.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = src.find('\n', versionPos);
            if (nextLine != std::string::npos) src = src.substr(nextLine + 1);
        }
    };
    stripVersion(vsSource);
    stripVersion(fsSource);

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;

    ShaderCI.Source = vsSource.c_str();
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    ShaderCI.Desc.Name = "Debug Line VS";
    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pVS);

    ShaderCI.Source = fsSource.c_str();
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    ShaderCI.Desc.Name = "Debug Line PS";
    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pPS);

    if (!pVS || !pPS) {
        Lit::Log::Error("Failed to compile debug line shaders");
        return;
    }

    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Debug Line PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = m_diligent->pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = m_diligent->pSwapChain->GetDesc().DepthBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_LINE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    Diligent::LayoutElement LayoutElems[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
        Diligent::LayoutElement{1, 0, 4, Diligent::VT_FLOAT32, false}
    };
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_VERTEX, "DebugLineUniforms", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pDebugLinePSO);
    if (!m_diligent->pDebugLinePSO) {
        Lit::Log::Error("Failed to create Debug Line PSO");
        return;
    }

    Diligent::BufferDesc UBODesc;
    UBODesc.Name = "Debug Line Uniforms";
    UBODesc.Usage = Diligent::USAGE_DYNAMIC;
    UBODesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    UBODesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    UBODesc.Size = sizeof(glm::mat4);
    m_diligent->pDevice->CreateBuffer(UBODesc, nullptr, &m_diligent->pDebugLineUBO);

    Diligent::BufferDesc VBDesc;
    VBDesc.Name = "Debug Line Vertices";
    VBDesc.Usage = Diligent::USAGE_DYNAMIC;
    VBDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    VBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    VBDesc.Size = sizeof(DebugLineVertex) * (1u << 17);
    m_diligent->pDevice->CreateBuffer(VBDesc, nullptr, &m_diligent->pDebugLineVB);

    m_diligent->pDebugLinePSO->CreateShaderResourceBinding(&m_diligent->pDebugLineSRB, true);
    if (m_diligent->pDebugLineSRB) {
        if (auto* var = m_diligent->pDebugLineSRB->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "DebugLineUniforms")) var->Set(m_diligent->pDebugLineUBO);
    }
}

void Renderer::setAnimation(float time, uint32_t movingCount, uint32_t entityOffset) {
    m_animTime = time;
    m_animMovingCount = movingCount;
    m_animEntityOffset = entityOffset;
}

void Renderer::uploadBasePositions(const std::vector<glm::vec3>& basePositions) {
    if (basePositions.empty()) return;

    std::vector<glm::vec4> alignedPositions(basePositions.size());
    for (size_t i = 0; i < basePositions.size(); ++i) { alignedPositions[i] = glm::vec4(basePositions[i], 0.0f); }

    m_diligent->pBasePositionBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Base Position Buffer", sizeof(glm::vec4), static_cast<Diligent::Uint32>(alignedPositions.size()), alignedPositions.data());

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_diligent->pTransformSRB[i] && m_diligent->pBasePositionBuffer) {
            auto* var = m_diligent->pTransformSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BasePositionBuffer");
            if (var) var->Set(m_diligent->pBasePositionBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
        }
        if (m_diligent->pAnimSRB[i] && m_diligent->pBasePositionBuffer) {
            auto* var = m_diligent->pAnimSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BasePositionBuffer");
            if (var) var->Set(m_diligent->pBasePositionBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
        }
    }
}

void Renderer::createAnimPSO() {
    std::string source = LoadSourceFromFile("resources/shaders/anim.comp");
    if (source.empty()) {
        Lit::Log::Error("Failed to load animation compute shader source.");
        return;
    }

    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source = source.substr(nextLine + 1);
    }

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name = "Anim CS";

    Diligent::RefCntAutoPtr<Diligent::IShader> pCS;
    m_diligent->pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS) return;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_COMPUTE, "AnimConstants",      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "BasePositionBuffer", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {Diligent::SHADER_TYPE_COMPUTE, "TransformBuffer",    Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };

    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Anim PSO";
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    PSOCI.PSODesc.ResourceLayout.Variables = Vars;
    PSOCI.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

    m_diligent->pDevice->CreateComputePipelineState(PSOCI, &m_diligent->pAnimPSO);

    struct AnimConstants {
        float time;
        uint32_t movingCount;
        uint32_t entityOffset;
        float padding;
    };

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Anim Constants Buffer";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(AnimConstants);
    m_diligent->pDevice->CreateBuffer(CBDesc, nullptr, &m_diligent->pAnimConstants);
}
