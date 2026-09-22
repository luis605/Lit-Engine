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

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
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
    alignas(16) glm::vec4 lightInfo;
    alignas(16) glm::vec4 lightHead[8];
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
constexpr uint32_t MAX_TRANSFORM_WORK = 65536;
constexpr uint32_t kMaxMaterials = 1024;
constexpr uint32_t kMaxGpuLights = 64;

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

uint32_t PackRenderable(const RenderableComponent& renderable) {
    constexpr uint32_t translucentFlag = 4u;
    const uint32_t mesh = std::min(renderable.mesh_uuid, 0xFFFu);
    const uint32_t material = std::min(renderable.material_uuid, 0x3FFu);
    const uint32_t shader = std::min(renderable.shaderId, 0xFu);
    const uint32_t flags = (renderable.flags & (RENDER_HIDDEN | RENDER_NO_OCCLUDER)) | (renderable.alpha < 1.0f ? translucentFlag : 0u);
    return mesh | material << 12u | shader << 22u | flags << 26u;
}

Diligent::Uint32 SelectFastestAdapter(Diligent::IEngineFactory* pFactory, Diligent::Version minVersion) {
    Diligent::Uint32 adapterCount = 0;
    pFactory->EnumerateAdapters(minVersion, adapterCount, nullptr);
    std::vector<Diligent::GraphicsAdapterInfo> adapters(adapterCount);
    pFactory->EnumerateAdapters(minVersion, adapterCount, adapters.data());

    auto rank = [](const Diligent::GraphicsAdapterInfo& adapter) {
        switch (adapter.Type) {
            case Diligent::ADAPTER_TYPE_DISCRETE: return 3;
            case Diligent::ADAPTER_TYPE_INTEGRATED: return 2;
            case Diligent::ADAPTER_TYPE_SOFTWARE: return 0;
            default: return 1;
        }
    };
    const auto best = std::ranges::max_element(adapters, {}, rank);
    if (best == adapters.end()) return Diligent::DEFAULT_ADAPTER_ID;
    Lit::Log::Info("Selected GPU: {}", best->Description);
    return static_cast<Diligent::Uint32>(best - adapters.begin());
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateUniformBuffer(Diligent::IRenderDevice* pDevice, const char* name, Diligent::Uint64 size, Diligent::USAGE usage = Diligent::USAGE_DYNAMIC) {
    Diligent::BufferDesc Desc;
    Desc.Name = name;
    Desc.Usage = usage;
    Desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    Desc.CPUAccessFlags = usage == Diligent::USAGE_DYNAMIC ? Diligent::CPU_ACCESS_WRITE : Diligent::CPU_ACCESS_NONE;
    Desc.Size = size;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    pDevice->CreateBuffer(Desc, nullptr, &pBuffer);
    if (!pBuffer) { Lit::Log::Error("Failed to allocate uniform buffer '{}'.", name); }
    return pBuffer;
}

struct NamedBuffer {
    std::string_view name;
    Diligent::IBuffer* buffer;
};

Diligent::IDeviceObject* BufferResourceFor(Diligent::IBuffer* pBuffer, Diligent::SHADER_RESOURCE_TYPE type) {
    if (!pBuffer) return nullptr;
    switch (type) {
        case Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER: return pBuffer;
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV: return pBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV: return pBuffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
        default: return nullptr;
    }
}

void BindBuffers(Diligent::IShaderResourceBinding* pSRB, std::initializer_list<NamedBuffer> buffers) {
    if (!pSRB) return;
    const auto& signature = pSRB->GetPipelineResourceSignature()->GetDesc();
    for (Diligent::Uint32 r = 0; r < signature.NumResources; ++r) {
        const Diligent::PipelineResourceDesc& resource = signature.Resources[r];
        if (resource.VarType == Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC) continue;
        const auto match = std::ranges::find(buffers, std::string_view(resource.Name), &NamedBuffer::name);
        Diligent::IDeviceObject* pObject = match != buffers.end() ? BufferResourceFor(match->buffer, resource.ResourceType) : nullptr;
        if (!pObject) {
            Lit::Log::Error("No buffer bound to '{}' in '{}'.", resource.Name, signature.Name);
            continue;
        }
        for (Diligent::Uint32 stages = resource.ShaderStages; stages != 0; stages &= stages - 1) {
            const auto stage = static_cast<Diligent::SHADER_TYPE>(stages & ~(stages - 1));
            if (auto* pVariable = pSRB->GetVariableByName(stage, resource.Name)) pVariable->Set(pObject, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
    }
}

void CreateBoundSRB(Diligent::IPipelineState* pPSO, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb, std::initializer_list<NamedBuffer> buffers) {
    srb.Release();
    if (!pPSO) return;
    pPSO->CreateShaderResourceBinding(&srb, true);
    BindBuffers(srb, buffers);
}

Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::IRenderDevice* pDevice, const std::string& path, Diligent::SHADER_TYPE type, const Diligent::ShaderMacroArray& macros = {}) {
    std::string source = LoadSourceFromFile(path);
    if (source.empty()) return {};

    const size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        const size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source.erase(0, nextLine + 1);
    }

    static Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> includeFactory = [] {
        Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> factory;
        Diligent::GetEngineFactoryVk()->CreateDefaultShaderSourceStreamFactory("resources/shaders/include", &factory);
        return factory;
    }();

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = source.c_str();
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.pShaderSourceStreamFactory = includeFactory;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    ShaderCI.Desc.ShaderType = type;
    ShaderCI.Desc.Name = path.c_str();
    ShaderCI.Macros = macros;

    Diligent::RefCntAutoPtr<Diligent::IShader> pShader;
    pDevice->CreateShader(ShaderCI, &pShader);
    if (!pShader) { Lit::Log::Error("Failed to compile shader '{}'.", path); }
    return pShader;
}

bool AllShadersCompile(Diligent::IRenderDevice* pDevice) {
    const std::pair<std::string_view, Diligent::SHADER_TYPE> stages[] = {{".vert", Diligent::SHADER_TYPE_VERTEX}, {".frag", Diligent::SHADER_TYPE_PIXEL}, {".comp", Diligent::SHADER_TYPE_COMPUTE}};
    const Diligent::ShaderMacro pointMacros[] = {{"POINT_SPRITE", "1"}};
    bool allCompile = true;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("resources/shaders", ec)) {
        const std::string extension = entry.path().extension().string();
        for (const auto& [stageExtension, type] : stages) {
            if (extension != stageExtension) continue;
            const std::string path = entry.path().generic_string();
            allCompile &= CompileShader(pDevice, path, type) != nullptr;
            if (type == Diligent::SHADER_TYPE_PIXEL) allCompile &= CompileShader(pDevice, path, type, Diligent::ShaderMacroArray{pointMacros, 1}) != nullptr;
        }
    }
    return allCompile;
}

std::vector<Diligent::ShaderResourceVariableDesc> DescribeVariables(std::initializer_list<Diligent::IShader*> shaders, std::initializer_list<std::string_view> dynamicVariables = {}) {
    std::vector<Diligent::ShaderResourceVariableDesc> variables;
    for (Diligent::IShader* pShader : shaders) {
        const Diligent::SHADER_TYPE stage = pShader->GetDesc().ShaderType;
        for (Diligent::Uint32 i = 0; i < pShader->GetResourceCount(); ++i) {
            Diligent::ShaderResourceDesc resource;
            pShader->GetResourceDesc(i, resource);
            const bool isDynamic = std::ranges::find(dynamicVariables, std::string_view(resource.Name)) != dynamicVariables.end();
            const bool isStorageBuffer = resource.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV || resource.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV;
            variables.push_back({stage, resource.Name, isDynamic ? Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC : Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
                                 isStorageBuffer ? Diligent::SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS : Diligent::SHADER_VARIABLE_FLAG_NONE});
        }
    }
    return variables;
}

void UseVariables(Diligent::PipelineResourceLayoutDesc& layout, const std::vector<Diligent::ShaderResourceVariableDesc>& variables) {
    layout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    layout.Variables = variables.data();
    layout.NumVariables = static_cast<Diligent::Uint32>(variables.size());
}

Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateComputePipeline(Diligent::IRenderDevice* pDevice, const char* name, const std::string& path, std::initializer_list<std::string_view> dynamicVariables = {}) {
    Diligent::RefCntAutoPtr<Diligent::IShader> pCS = CompileShader(pDevice, path, Diligent::SHADER_TYPE_COMPUTE);
    if (!pCS) return {};

    const auto variables = DescribeVariables({pCS}, dynamicVariables);
    Diligent::ComputePipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = name;
    PSOCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    PSOCI.pCS = pCS;
    UseVariables(PSOCI.PSODesc.ResourceLayout, variables);

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPSO;
    pDevice->CreateComputePipelineState(PSOCI, &pPSO);
    if (!pPSO) { Lit::Log::Error("Failed to create pipeline '{}'.", name); }
    return pPSO;
}

void CreateBindings(Diligent::IPipelineState* pPSO, std::span<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> bindings) {
    for (auto& binding : bindings) {
        binding.Release();
        if (pPSO) pPSO->CreateShaderResourceBinding(&binding, true);
    }
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
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransformWorkBuffer;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransformWorkSRB[NumFrames];
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
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pMaterialBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLightBuffer;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDebugLinePSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDebugLineOverlayPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDebugLineOverlaySRB;
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

    m_window = window;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

    m_diligent = new DiligentData();
    m_diligent->pFactoryVk = Diligent::GetEngineFactoryVk();

    Diligent::EngineVkCreateInfo EngineCI;
    EngineCI.AdapterId = SelectFastestAdapter(m_diligent->pFactoryVk, EngineCI.GraphicsAPIVersion);
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

    Diligent::SamplerDesc SamplerCI;
    SamplerCI.Name = "Hi-Z Sampler";
    SamplerCI.MinFilter = Diligent::FILTER_TYPE_POINT;
    SamplerCI.MagFilter = Diligent::FILTER_TYPE_POINT;
    SamplerCI.MipFilter = Diligent::FILTER_TYPE_POINT;
    SamplerCI.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    SamplerCI.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    m_diligent->pDevice->CreateSampler(SamplerCI, &m_diligent->pHiZSampler);

    createScreenTargets();

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
    m_diligent->pTransformWorkBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Transform Work Buffer", sizeof(Entity), MAX_TRANSFORM_WORK);
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
        m_diligent->pCullOrientationBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Cull Orientation Buffer", sizeof(uint32_t), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pCullOrientationBuffer[0];
        m_diligent->pCullSphereBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Cull Sphere Buffer", sizeof(glm::vec4), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pCullSphereBuffer[0];
        m_diligent->pDirtyIndexBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Dirty Index Buffer", sizeof(unsigned int), MAX_DIRTY_PER_FRAME) : m_diligent->pDirtyIndexBuffer[0];
        m_diligent->pDirtyPayloadBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Dirty Payload Buffer", sizeof(glm::mat4), MAX_DIRTY_PER_FRAME) : m_diligent->pDirtyPayloadBuffer[0];
        m_diligent->pTouchedEpochBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Touched Epoch Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pTouchedEpochBuffer[0];
        m_diligent->pHierarchyBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Hierarchy Buffer", sizeof(HierarchyComponent), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pHierarchyBuffer[0];
        m_diligent->pRenderableBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Renderable Buffer", sizeof(uint32_t), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pRenderableBuffer[0];
        m_diligent->pSortedHierarchyBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Sorted Hierarchy Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pSortedHierarchyBuffer[0];
        m_diligent->pVisibleObjectBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Visible Objects Buffer", sizeof(unsigned int) * 2, static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pVisibleObjectBuffer[0];
        m_diligent->pSortedVisibleObjectBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Sorted Visible Objects Buffer", sizeof(unsigned int) * 2, static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pSortedVisibleObjectBuffer[0];
        m_diligent->pBucketCountBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Count Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pBucketOffsetBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Offset Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pBucketWriteHeadBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Write Head Buffer", sizeof(unsigned int), totalBuckets);
        m_diligent->pTransparentIdBuffer[i] = i == 0 ? CreateStructuredBuffer(m_diligent->pDevice, "Transparent Id Buffer", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_maxObjects)) : m_diligent->pTransparentIdBuffer[0];
        m_diligent->pPointCommandBuffer[i] = CreateIndirectBuffer(m_diligent->pDevice, "Point Command Buffer", static_cast<size_t>(MAX_MESHES) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand));
        m_diligent->pDrawCommandBuffer[i] = CreateIndirectBuffer(m_diligent->pDevice, "Draw Command Buffer", static_cast<size_t>(MAX_MESHES) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand));

        m_diligent->pVisibleLargeObjectBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Visible Large Objects Buffer", sizeof(unsigned int) * 2, static_cast<Diligent::Uint32>(subPassMaxObjects));
        m_diligent->pSortedVisibleLargeObjectBuffer[i] = CreateStructuredBuffer(m_diligent->pDevice, "Sorted Visible Large Objects Buffer", sizeof(unsigned int) * 2, static_cast<Diligent::Uint32>(subPassMaxObjects));
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

    if (!m_diligent->pLightBuffer) {
        std::vector<glm::vec4> lightData(static_cast<size_t>(kMaxGpuLights) * 4, glm::vec4(0.0f));
        Diligent::BufferDesc LightDesc;
        LightDesc.Name = "Light Block";
        LightDesc.Usage = Diligent::USAGE_DEFAULT;
        LightDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        LightDesc.Size = sizeof(glm::vec4) * kMaxGpuLights * 4;
        Diligent::BufferData LightInit;
        LightInit.pData = lightData.data();
        LightInit.DataSize = LightDesc.Size;
        m_diligent->pDevice->CreateBuffer(LightDesc, &LightInit, &m_diligent->pLightBuffer);
        m_lightDataDirty = true;
    }

    if (!m_diligent->pMaterialBuffer) {
        std::vector<glm::vec4> materials(kMaxMaterials, glm::vec4(0.0f));
        m_diligent->pMaterialBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Material Buffer", sizeof(glm::vec4), kMaxMaterials, materials.data());
    }

    if (!m_diligent->pBasePositionBuffer) {
        glm::vec4 zero(0.0f);
        m_diligent->pBasePositionBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Base Position Buffer", sizeof(glm::vec4), 1, &zero);
    }

    auto& d = *m_diligent;
    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        CreateBoundSRB(d.pAnimPSO, d.pAnimSRB[i], {{"AnimConstants", d.pAnimConstants}, {"BasePositionBuffer", d.pBasePositionBuffer}, {"TransformBuffer", d.pObjectBuffer[i]}});
        CreateBoundSRB(d.pTransformPSO, d.pTransformSRB[i],
                       {{"TransformUniforms", d.pTransformUniforms}, {"TransformBuffer", d.pObjectBuffer[i]}, {"WorldMatrixBuffer", d.pWorldMatrixBuffer[i]}, {"HierarchyBuffer", d.pHierarchyBuffer[i]},
                        {"SortedHierarchyBuffer", d.pSortedHierarchyBuffer[i]}, {"TouchedEpochBuffer", d.pTouchedEpochBuffer[i]}, {"CullOrientationBuffer", d.pCullOrientationBuffer[i]},
                        {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"RenderableBuffer", d.pRenderableBuffer[i]}, {"MeshInfoBuffer", d.pMeshInfoBuffer}, {"BasePositionBuffer", d.pBasePositionBuffer}});
        CreateBoundSRB(d.pTransformPSO, d.pTransformWorkSRB[i],
                       {{"TransformUniforms", d.pTransformUniforms}, {"TransformBuffer", d.pObjectBuffer[i]}, {"WorldMatrixBuffer", d.pWorldMatrixBuffer[i]}, {"HierarchyBuffer", d.pHierarchyBuffer[i]},
                        {"SortedHierarchyBuffer", d.pTransformWorkBuffer}, {"TouchedEpochBuffer", d.pTouchedEpochBuffer[i]}, {"CullOrientationBuffer", d.pCullOrientationBuffer[i]},
                        {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"RenderableBuffer", d.pRenderableBuffer[i]}, {"MeshInfoBuffer", d.pMeshInfoBuffer}, {"BasePositionBuffer", d.pBasePositionBuffer}});
        CreateBoundSRB(d.pCullingPSO, d.pCullingSRB[i],
                       {{"SceneData", d.pSceneUBO[i]}, {"CullingUniforms", d.pCullingUniforms}, {"AtomicCounterBuffer", d.pVisibleObjectAtomicCounter[i]}, {"VisibleObjectBuffer", d.pVisibleObjectBuffer[i]},
                        {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"WorldMatrixBuffer", d.pWorldMatrixBuffer[i]}, {"MeshInfoBuffer", d.pMeshInfoBuffer}, {"RenderableBuffer", d.pRenderableBuffer[i]},
                        {"BucketCountBuffer", d.pBucketCountBuffer[i]}});
        CreateBoundSRB(d.pPrefixSumPSO, d.pPrefixSumSRB[i],
                       {{"PrefixSumConstants", d.pPrefixSumConstants}, {"BucketCountBuffer", d.pBucketCountBuffer[i]}, {"BucketOffsetBuffer", d.pBucketOffsetBuffer[i]}, {"BucketWriteHeadBuffer", d.pBucketWriteHeadBuffer[i]}});
        CreateBoundSRB(d.pScatterPSO, d.pScatterSRB[i],
                       {{"ScatterConstants", d.pScatterConstants}, {"AtomicCounterBuffer", d.pVisibleObjectAtomicCounter[i]}, {"VisibleObjectBuffer", d.pVisibleObjectBuffer[i]},
                        {"BucketWriteHeadBuffer", d.pBucketWriteHeadBuffer[i]}, {"SortedVisibleObjectBuffer", d.pSortedVisibleObjectBuffer[i]}});
        CreateBoundSRB(d.pApplyDirtyPSO, d.pApplyDirtySRB[i],
                       {{"ApplyDirtyConstants", d.pApplyDirtyConstants}, {"DirtyIndexBuffer", d.pDirtyIndexBuffer[i]}, {"DirtyPayloadBuffer", d.pDirtyPayloadBuffer[i]}, {"TransformBuffer", d.pObjectBuffer[i]},
                        {"TouchedEpochBuffer", d.pTouchedEpochBuffer[i]}});
        CreateBoundSRB(d.pMarkTouchedPSO, d.pMarkTouchedSRB[i], {{"MarkTouchedConstants", d.pMarkTouchedConstants}, {"DirtyIndexBuffer", d.pDirtyIndexBuffer[i]}, {"TouchedEpochBuffer", d.pTouchedEpochBuffer[i]}});
        CreateBoundSRB(d.pCommandGenPSO, d.pCommandGenSRB[i],
                       {{"CommandGenConstants", d.pCommandGenConstants}, {"PointCounterBuffer", d.pPointDrawCounterBuffer[i]}, {"PointCommandBuffer", d.pPointCommandBuffer[i]},
                        {"DrawAtomicCounterBuffer", d.pDrawAtomicCounterBuffer[i]}, {"DrawCommandBuffer", d.pDrawCommandBuffer[i]}, {"MeshInfoBuffer", d.pMeshInfoBuffer},
                        {"BucketCountBuffer", d.pBucketCountBuffer[i]}, {"BucketOffsetBuffer", d.pBucketOffsetBuffer[i]}});

        CreateBoundSRB(d.pLargeObjectCullPSO, d.pLargeObjectCullSRB[i],
                       {{"SceneData", d.pSceneUBO[i]}, {"LargeObjectCullConstants", d.pLargeObjectCullConstants}, {"MeshInfoBuffer", d.pMeshInfoBuffer},
                        {"VisibleLargeObjectAtomicCounter", d.pVisibleLargeObjectAtomicCounter[i]}, {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"RenderableBuffer", d.pRenderableBuffer[i]},
                        {"VisibleLargeObjectBuffer", d.pVisibleLargeObjectBuffer[i]}, {"LargeBucketCountBuffer", d.pLargeBucketCountBuffer[i]}});
        CreateBoundSRB(d.pPrefixSumPSO, d.pLargePrefixSumSRB[i],
                       {{"PrefixSumConstants", d.pPrefixSumConstants}, {"BucketCountBuffer", d.pLargeBucketCountBuffer[i]}, {"BucketOffsetBuffer", d.pLargeBucketOffsetBuffer[i]},
                        {"BucketWriteHeadBuffer", d.pLargeBucketWriteHeadBuffer[i]}});
        CreateBoundSRB(d.pScatterPSO, d.pLargeScatterSRB[i],
                       {{"ScatterConstants", d.pLargeScatterConstants}, {"AtomicCounterBuffer", d.pVisibleLargeObjectAtomicCounter[i]}, {"VisibleObjectBuffer", d.pVisibleLargeObjectBuffer[i]},
                        {"BucketWriteHeadBuffer", d.pLargeBucketWriteHeadBuffer[i]}, {"SortedVisibleObjectBuffer", d.pSortedVisibleLargeObjectBuffer[i]}});
        CreateBoundSRB(d.pCommandGenPSO, d.pLargeCommandGenSRB[i],
                       {{"CommandGenConstants", d.pCommandGenConstants}, {"PointCounterBuffer", d.pPointDrawCounterBuffer[i]}, {"PointCommandBuffer", d.pPointCommandBuffer[i]},
                        {"DrawAtomicCounterBuffer", d.pLargeDrawAtomicCounterBuffer[i]}, {"DrawCommandBuffer", d.pDepthPrepassDrawCommandBuffer[i]}, {"MeshInfoBuffer", d.pMeshInfoBuffer},
                        {"BucketCountBuffer", d.pLargeBucketCountBuffer[i]}, {"BucketOffsetBuffer", d.pLargeBucketOffsetBuffer[i]}});

        CreateBoundSRB(d.pTransparentCullPSO, d.pTransparentCullSRB[i],
                       {{"SceneData", d.pSceneUBO[i]}, {"TransparentCullUniforms", d.pTransparentCullUniforms}, {"AtomicCounterBuffer", d.pTransparentAtomicCounter[i]},
                        {"VisibleTransparentObjectBuffer", d.pVisibleTransparentObjectIdsBuffer[i]}, {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"MeshInfoBuffer", d.pMeshInfoBuffer},
                        {"TransparentIdBuffer", d.pTransparentIdBuffer[i]}});
        CreateBoundSRB(d.pTransparentCommandGenPSO, d.pTransparentCommandGenSRB[i],
                       {{"TransparentCommandGenUniforms", d.pTransparentCommandGenUniforms}, {"VisibleTransparentObjectBuffer", d.pVisibleTransparentObjectIdsBuffer[i]},
                        {"MeshInfoBuffer", d.pMeshInfoBuffer}, {"RenderableBuffer", d.pRenderableBuffer[i]}, {"TransparentDrawCommandBuffer", d.pTransparentDrawCommandBuffer[i]},
                        {"AtomicCounterBuffer", d.pTransparentAtomicCounter[i]}});

        CreateBoundSRB(d.pDispatchArgsPSO, d.pTransparentDispatchArgsSRB[i],
                       {{"DispatchArgsConstants", d.pDispatchArgsConstants}, {"SourceCounterBuffer", d.pTransparentAtomicCounter[i]}, {"DispatchArgsBuffer", d.pTransparentDispatchArgs[i]}});
        CreateBoundSRB(d.pDispatchArgsPSO, d.pLargeObjectDispatchArgsSRB[i],
                       {{"DispatchArgsConstants", d.pDispatchArgsConstants}, {"SourceCounterBuffer", d.pVisibleLargeObjectAtomicCounter[i]}, {"DispatchArgsBuffer", d.pLargeObjectDispatchArgs[i]}});
        CreateBoundSRB(d.pDispatchArgsPSO, d.pScatterDispatchArgsSRB[i],
                       {{"DispatchArgsConstants", d.pScatterDispatchArgsConstants}, {"SourceCounterBuffer", d.pVisibleObjectAtomicCounter[i]}, {"DispatchArgsBuffer", d.pScatterDispatchArgs[i]}});

        CreateBoundSRB(d.pDepthPrepassPSO, d.pDepthPrepassSRB[i],
                       {{"SceneData", d.pSceneUBO[i]}, {"WorldMatrixBuffer", d.pWorldMatrixBuffer[i]}, {"VisibleLargeObjectBuffer", d.pSortedVisibleLargeObjectBuffer[i]}});

        const std::initializer_list<NamedBuffer> surfaceResources = {
            {"SceneData", d.pSceneUBO[i]}, {"LightBlock", d.pLightBuffer}, {"MaterialBuffer", d.pMaterialBuffer}, {"WorldMatrixBuffer", d.pWorldMatrixBuffer[i]},
            {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"CullOrientationBuffer", d.pCullOrientationBuffer[i]}, {"NormalSampleBuffer", d.pNormalSampleBuffer},
            {"RenderableBuffer", d.pRenderableBuffer[i]}, {"VisibleObjectBuffer", d.pSortedVisibleObjectBuffer[i]}, {"VisibleTransparentObjectBuffer", d.pVisibleTransparentObjectIdsBuffer[i]}};
        for (size_t s = 0; s < d.pOpaquePSOs.size(); ++s) CreateBoundSRB(d.pOpaquePSOs[s], d.pOpaqueSRBs[i][s], surfaceResources);
        for (size_t s = 0; s < d.pPointPSOs.size(); ++s) CreateBoundSRB(d.pPointPSOs[s], d.pPointSRBs[i][s], surfaceResources);
        CreateBoundSRB(d.pTransparentPSO, d.pTransparentSRB[i], surfaceResources);
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

void Renderer::createScreenTargets() {
    m_maxMipLevel = static_cast<int>(std::floor(std::log2(std::max(m_windowWidth, m_windowHeight))));

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        for (auto& view : m_diligent->pHiZMipViewsSource[i]) view.Release();
        for (auto& view : m_diligent->pHiZMipViewsDest[i]) view.Release();

        Diligent::TextureDesc HiZDesc;
        HiZDesc.Name = "Hi-Z Texture";
        HiZDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        HiZDesc.Width = m_windowWidth;
        HiZDesc.Height = m_windowHeight;
        HiZDesc.Format = Diligent::TEX_FORMAT_R32_FLOAT;
        HiZDesc.Usage = Diligent::USAGE_DEFAULT;
        HiZDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_RENDER_TARGET;
        HiZDesc.MipLevels = m_maxMipLevel;
        m_diligent->pHiZTextures[i].Release();
        m_diligent->pDevice->CreateTexture(HiZDesc, nullptr, &m_diligent->pHiZTextures[i]);

        Diligent::TextureDesc DepthDesc;
        DepthDesc.Name = "Depth Pre-pass Renderbuffer";
        DepthDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        DepthDesc.Width = m_windowWidth;
        DepthDesc.Height = m_windowHeight;
        DepthDesc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
        DepthDesc.Usage = Diligent::USAGE_DEFAULT;
        DepthDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL;
        m_diligent->pDepthRenderbuffers[i].Release();
        m_diligent->pDevice->CreateTexture(DepthDesc, nullptr, &m_diligent->pDepthRenderbuffers[i]);

        if (!m_diligent->pHiZTextures[i]) continue;
        const float clearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        m_diligent->pImmediateContext->ClearRenderTarget(m_diligent->pHiZTextures[i]->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET), clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pHiZTextures[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(m_diligent->pHiZSampler);

        for (int mip = 1; mip < m_maxMipLevel && mip < 32; ++mip) {
            Diligent::TextureViewDesc ViewDesc;
            ViewDesc.ViewType = Diligent::TEXTURE_VIEW_UNORDERED_ACCESS;
            ViewDesc.NumMipLevels = 1;
            ViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_READ;
            ViewDesc.MostDetailedMip = mip - 1;
            m_diligent->pHiZTextures[i]->CreateView(ViewDesc, &m_diligent->pHiZMipViewsSource[i][mip]);
            ViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_WRITE;
            ViewDesc.MostDetailedMip = mip;
            m_diligent->pHiZTextures[i]->CreateView(ViewDesc, &m_diligent->pHiZMipViewsDest[i][mip]);
        }
    }
}

bool Renderer::resizeToWindow() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    if (width <= 0 || height <= 0 || (width == m_windowWidth && height == m_windowHeight)) return false;

    m_diligent->pImmediateContext->WaitForIdle();
    m_diligent->pSwapChain->Resize(static_cast<Diligent::Uint32>(width), static_cast<Diligent::Uint32>(height));
    const auto& swapChainDesc = m_diligent->pSwapChain->GetDesc();
    m_windowWidth = static_cast<int>(swapChainDesc.Width);
    m_windowHeight = static_cast<int>(swapChainDesc.Height);
    createScreenTargets();
    m_uiManager->resize(m_windowWidth, m_windowHeight);
    return true;
}

bool Renderer::animatedEntitiesAreLeaves(const SceneDatabase& sceneDatabase) {
    const std::array<uint64_t, 3> key = {sceneDatabase.m_hierarchyVersion, m_animEntityOffset, m_animMovingCount};
    if (key == m_animLeafCheckKey) return m_animatedEntitiesAreLeaves;
    m_animLeafCheckKey = key;
    m_animatedEntitiesAreLeaves = true;
    const size_t end = std::min<size_t>(static_cast<size_t>(m_animEntityOffset) + m_animMovingCount, sceneDatabase.hierarchies.size());
    for (size_t e = m_animEntityOffset; e < end && m_animatedEntitiesAreLeaves; ++e) m_animatedEntitiesAreLeaves = sceneDatabase.childrenOf(static_cast<Entity>(e)).empty();
    return m_animatedEntitiesAreLeaves;
}

bool Renderer::collectTransformWork(const SceneDatabase& sceneDatabase, std::span<const Entity> dirty) {
    const size_t entityCount = sceneDatabase.hierarchies.size();
    if (m_transformWorkStamp.size() < entityCount) m_transformWorkStamp.resize(entityCount, 0);
    if (++m_transformWorkEpoch == 0) {
        std::ranges::fill(m_transformWorkStamp, 0u);
        m_transformWorkEpoch = 1;
    }

    m_transformWorkScratch.clear();
    auto enqueue = [&](Entity e) {
        if (e >= entityCount || m_transformWorkStamp[e] == m_transformWorkEpoch) return;
        m_transformWorkStamp[e] = m_transformWorkEpoch;
        m_transformWorkScratch.push_back(e);
    };
    for (Entity e : dirty) enqueue(e);
    for (size_t next = 0; next < m_transformWorkScratch.size(); ++next) {
        if (m_transformWorkScratch.size() > MAX_TRANSFORM_WORK) return false;
        for (Entity child : sceneDatabase.childrenOf(m_transformWorkScratch[next])) enqueue(child);
    }
    if (m_transformWorkScratch.size() > MAX_TRANSFORM_WORK) return false;

    m_transformWorkLevelCounts.assign(sceneDatabase.m_maxHierarchyDepth + 1, 0);
    for (Entity e : m_transformWorkScratch) m_transformWorkLevelCounts[sceneDatabase.hierarchies[e].level]++;
    std::vector<uint32_t>& levelHeads = m_transformWorkLevelHeads;
    levelHeads.assign(m_transformWorkLevelCounts.size(), 0);
    for (size_t level = 1; level < levelHeads.size(); ++level) levelHeads[level] = levelHeads[level - 1] + m_transformWorkLevelCounts[level - 1];
    m_transformWork.resize(m_transformWorkScratch.size());
    for (Entity e : m_transformWorkScratch) m_transformWork[levelHeads[sceneDatabase.hierarchies[e].level]++] = e;
    return true;
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
        const auto& renderables = sceneDatabase.renderables;
        m_packedRenderables.resize(renderables.size());
        for (size_t e = 0; e < renderables.size(); ++e) m_packedRenderables[e] = PackRenderable(renderables[e]);
        if (m_transparentIdsStale) {
            m_transparentIds.clear();
            for (size_t e = 0; e < renderables.size(); ++e) {
                if (renderables[e].shaderId == 2) { m_transparentIds.push_back(static_cast<uint32_t>(e)); }
            }
            m_transparentIdsStale = false;
        }
        m_transparentIdCounts[0] = static_cast<uint32_t>(m_transparentIds.size());
        if (!m_transparentIds.empty()) {
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransparentIdBuffer[m_currentFrame], 0, m_transparentIds.size() * sizeof(uint32_t), m_transparentIds.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pRenderableBuffer[m_currentFrame], 0, m_packedRenderables.size() * sizeof(uint32_t), m_packedRenderables.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_renderableUpdateCounter--;
    }

    m_transformEpoch++;

    const bool forceAllLevels = hierarchyJustChanged || dirtyUsesBulkPath || fullTransformUpdateActive || (m_animMovingCount > 0 && !animatedEntitiesAreLeaves(sceneDatabase));
    std::vector<uint8_t>& levelNeedsProcessing = m_levelNeedsProcessing;
    levelNeedsProcessing.assign(sceneDatabase.m_maxHierarchyDepth + 1, forceAllLevels ? 1u : 0u);

    bool sparseTransforms = false;
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

        sparseTransforms = !forceAllLevels && collectTransformWork(sceneDatabase, m_dirtyIndexScratch);
        if (sparseTransforms) {
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransformWorkBuffer, 0, m_transformWork.size() * sizeof(Entity), m_transformWork.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        } else if (!hierarchyJustChanged) {
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
        unsigned int directEntityRange;
    } transformUniforms;

    const bool animActive = m_animMovingCount > 0 && m_diligent->pBasePositionBuffer;

    auto dispatchTransforms = [&](Diligent::IShaderResourceBinding* pSRB, uint32_t listOffset, uint32_t count, bool forceRecompute, bool directEntityRange) {
        transformUniforms.objectCount = count;
        transformUniforms.currentHierarchyLevel = 0;
        transformUniforms.sortedListOffset = listOffset;
        transformUniforms.frameOffset = 0;
        transformUniforms.epoch = m_transformEpoch;
        transformUniforms.forceFullRecompute = forceRecompute ? 1u : 0u;
        transformUniforms.animTime = m_animTime;
        transformUniforms.animMovingCount = animActive ? m_animMovingCount : 0u;
        transformUniforms.animEntityOffset = m_animEntityOffset;
        transformUniforms.directEntityRange = directEntityRange ? 1u : 0u;
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransformUniforms, 0, sizeof(transformUniforms), &transformUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_diligent->pImmediateContext->SetPipelineState(m_diligent->pTransformPSO);
        m_diligent->pImmediateContext->CommitShaderResources(pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pImmediateContext->DispatchCompute(Diligent::DispatchComputeAttribs((count + transformWorkgroupSize - 1) / transformWorkgroupSize, 1, 1));

        Diligent::StateTransitionDesc barriers[2];
        for (auto& [barrier, pBuffer] : {std::pair{&barriers[0], m_diligent->pWorldMatrixBuffer[m_currentFrame].RawPtr()}, std::pair{&barriers[1], m_diligent->pTouchedEpochBuffer[m_currentFrame].RawPtr()}}) {
            barrier->pResource = pBuffer;
            barrier->OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            barrier->NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
            barrier->TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
            barrier->Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
        }
        m_diligent->pImmediateContext->TransitionResourceStates(2, barriers);
    };

    const std::vector<uint32_t>& levelCounts = sparseTransforms ? m_transformWorkLevelCounts : sceneDatabase.m_levelCounts;
    auto* pLevelSRB = sparseTransforms ? m_diligent->pTransformWorkSRB[m_currentFrame].RawPtr() : m_diligent->pTransformSRB[m_currentFrame].RawPtr();
    const bool forceLevelRecompute = sparseTransforms || hierarchyJustChanged || dirtyUsesBulkPath || localMatricesUploading;
    uint32_t levelOffset = 0;
    for (uint32_t level = 0; level < levelCounts.size(); ++level) {
        const uint32_t levelCount = levelCounts[level];
        const bool processLevel = sparseTransforms || (level < levelNeedsProcessing.size() && levelNeedsProcessing[level]);
        if (levelCount > 0 && processLevel) dispatchTransforms(pLevelSRB, levelOffset, levelCount, forceLevelRecompute, false);
        levelOffset += levelCount;
    }

    if (animActive && !forceAllLevels) {
        const uint32_t animCount = static_cast<uint32_t>(std::min<size_t>(m_animMovingCount, sceneDatabase.transforms.size() - std::min<size_t>(m_animEntityOffset, sceneDatabase.transforms.size())));
        if (animCount > 0) dispatchTransforms(m_diligent->pTransformSRB[m_currentFrame], m_animEntityOffset, animCount, true, true);
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
    if (m_lightOverride) {
        sceneUniforms.dirLightDir = m_directional[0];
        sceneUniforms.dirLightColor = m_directional[1];
    }
    const std::vector<glm::vec4>& activeLights = m_lightOverride ? m_lightData : m_defaultLightData;
    const uint32_t activeLightCount = std::min<uint32_t>(static_cast<uint32_t>(activeLights.size() / 4), kMaxGpuLights);
    sceneUniforms.lightInfo = glm::vec4(static_cast<float>(activeLightCount), 0.0f, 0.0f, 0.0f);
    for (uint32_t k = 0; k < 8; ++k) sceneUniforms.lightHead[k] = k < activeLightCount * 4 && k < 8 ? activeLights[k] : glm::vec4(0.0f);
    if (m_lightDataDirty || m_uploadedLightSource != m_lightOverride) {
        std::vector<glm::vec4> padded(static_cast<size_t>(kMaxGpuLights) * 4, glm::vec4(0.0f));
        std::copy_n(activeLights.begin(), static_cast<size_t>(activeLightCount) * 4, padded.begin());
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pLightBuffer, 0, padded.size() * sizeof(glm::vec4), padded.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_lightDataDirty = false;
        m_uploadedLightSource = m_lightOverride;
    }
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

    if ((!m_debugLines.empty() || !m_debugOverlayLines.empty()) && m_diligent->pDebugLinePSO && m_diligent->pDebugLineSRB) {
        constexpr size_t kMaxDebugVertices = 1u << 17;
        constexpr size_t kFloatsPerVertex = sizeof(DebugLineVertex) / sizeof(float);
        const size_t depthVertices = std::min(m_debugLines.size() / kFloatsPerVertex, kMaxDebugVertices);
        const size_t overlayVertices = std::min(m_debugOverlayLines.size() / kFloatsPerVertex, kMaxDebugVertices - depthVertices);
        auto uploadVertices = [&](const std::vector<float>& source, size_t firstVertex, size_t vertexCount) {
            if (vertexCount == 0) return;
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pDebugLineVB, firstVertex * sizeof(DebugLineVertex), vertexCount * sizeof(DebugLineVertex), source.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        };
        uploadVertices(m_debugLines, 0, depthVertices);
        uploadVertices(m_debugOverlayLines, depthVertices, overlayVertices);
        {
            Diligent::MapHelper<glm::mat4> viewProjection(m_diligent->pImmediateContext, m_diligent->pDebugLineUBO, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            *viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
        }
        Diligent::IBuffer* pBuffers[] = {m_diligent->pDebugLineVB};
        const Diligent::Uint64 offsets[] = {0};
        m_diligent->pImmediateContext->SetVertexBuffers(0, 1, pBuffers, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        if (depthVertices > 0) {
            m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDebugLinePSO);
            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pDebugLineSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Diligent::DrawAttribs lineDraw;
            lineDraw.NumVertices = static_cast<Diligent::Uint32>(depthVertices);
            lineDraw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
            m_diligent->pImmediateContext->Draw(lineDraw);
        }
        if (overlayVertices > 0 && m_diligent->pDebugLineOverlayPSO && m_diligent->pDebugLineOverlaySRB) {
            m_diligent->pImmediateContext->SetPipelineState(m_diligent->pDebugLineOverlayPSO);
            m_diligent->pImmediateContext->CommitShaderResources(m_diligent->pDebugLineOverlaySRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Diligent::DrawAttribs overlayDraw;
            overlayDraw.NumVertices = static_cast<Diligent::Uint32>(overlayVertices);
            overlayDraw.StartVertexLocation = static_cast<Diligent::Uint32>(depthVertices);
            overlayDraw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
            m_diligent->pImmediateContext->Draw(overlayDraw);
        }
    }
    m_debugLines.clear();
    m_debugOverlayLines.clear();

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
    m_diligent->pTransformPSO = CreateComputePipeline(m_diligent->pDevice, "Transform PSO", "resources/shaders/transform.comp");
    m_diligent->pTransformUniforms = CreateUniformBuffer(m_diligent->pDevice, "Transform Uniforms", 256, Diligent::USAGE_DEFAULT);
}

void Renderer::createTransparentCullPSO() {
    m_diligent->pTransparentCullPSO = CreateComputePipeline(m_diligent->pDevice, "Transparent Cull PSO", "resources/shaders/transparent_cull.comp", {"u_hizTexture"});
    CreateBindings(m_diligent->pTransparentCullPSO, m_diligent->pTransparentCullSRB);
}

void Renderer::createTransparentCommandGenPSO() {
    m_diligent->pTransparentCommandGenPSO = CreateComputePipeline(m_diligent->pDevice, "Transparent Command Gen PSO", "resources/shaders/transparent_command_gen.comp");
    CreateBindings(m_diligent->pTransparentCommandGenPSO, m_diligent->pTransparentCommandGenSRB);
}

void Renderer::createDepthPrepassPSO() {
    m_diligent->pDepthPrepassPSO.Release();
    auto pVS = CompileShader(m_diligent->pDevice, "resources/shaders/depth_prepass.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pPS = CompileShader(m_diligent->pDevice, "resources/shaders/depth_prepass.frag", Diligent::SHADER_TYPE_PIXEL);
    if (!pVS || !pPS) return;

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
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    Diligent::LayoutElement LayoutElems[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false, 0, 6 * sizeof(float)}
    };
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    const auto variables = DescribeVariables({pVS, pPS});
    UseVariables(PSOCreateInfo.PSODesc.ResourceLayout, variables);

    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pDepthPrepassPSO);
    if (!m_diligent->pDepthPrepassPSO) Lit::Log::Error("Failed to create Depth Prepass PSO");
    CreateBindings(m_diligent->pDepthPrepassPSO, m_diligent->pDepthPrepassSRB);
}

void Renderer::createOpaquePSOs() {
    struct ShaderInfo {
        const char* frag;
        const char* name;
    };
    const ShaderInfo shaderInfos[] = {
        {"resources/shaders/cube.frag",        "Cube PSO"             },
        {"resources/shaders/red.frag",         "Red PSO"              },
        {"resources/shaders/transparent.frag", "Transparent Proxy PSO"}
    };
    constexpr size_t shaderCount = std::size(shaderInfos);

    for (auto* psos : {&m_diligent->pOpaquePSOs, &m_diligent->pPointPSOs}) {
        psos->clear();
        psos->resize(shaderCount);
    }
    for (auto* srbs : {&m_diligent->pOpaqueSRBs, &m_diligent->pPointSRBs}) {
        srbs->assign(DiligentData::NumFrames, {});
        for (auto& frame : *srbs) frame.resize(shaderCount);
    }

    auto pMeshVS = CompileShader(m_diligent->pDevice, "resources/shaders/cube.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pPointVS = CompileShader(m_diligent->pDevice, "resources/shaders/point.vert", Diligent::SHADER_TYPE_VERTEX);
    const Diligent::ShaderMacro pointMacros[] = {{"POINT_SPRITE", "1"}};

    for (size_t i = 0; i < shaderCount; ++i) {
        for (const bool point : {false, true}) {
            auto& pso = point ? m_diligent->pPointPSOs[i] : m_diligent->pOpaquePSOs[i];
            auto& srbs = point ? m_diligent->pPointSRBs : m_diligent->pOpaqueSRBs;
            const std::string psoName = std::string(shaderInfos[i].name) + (point ? " (points)" : "");

            Diligent::IShader* pVS = point ? pPointVS.RawPtr() : pMeshVS.RawPtr();
            auto pPS = CompileShader(m_diligent->pDevice, shaderInfos[i].frag, Diligent::SHADER_TYPE_PIXEL, point ? Diligent::ShaderMacroArray{pointMacros, 1} : Diligent::ShaderMacroArray{});
            if (!pVS || !pPS) continue;

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
            PSOCreateInfo.pVS = pVS;
            PSOCreateInfo.pPS = pPS;

            Diligent::LayoutElement LayoutElems[] = {
                Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
                Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false}
            };
            PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
            PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = point ? 0 : _countof(LayoutElems);

            const auto variables = DescribeVariables({pVS, pPS});
            UseVariables(PSOCreateInfo.PSODesc.ResourceLayout, variables);

            m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pso);
            if (!pso) {
                Lit::Log::Error("Failed to create Opaque PSO: {}", psoName);
                continue;
            }
            for (int f = 0; f < DiligentData::NumFrames; ++f) { pso->CreateShaderResourceBinding(&srbs[f][i], true); }
        }
    }
}

void Renderer::createTransparentPSO() {
    m_diligent->pTransparentPSO.Release();
    CreateBindings(nullptr, m_diligent->pTransparentSRB);

    auto pVS = CompileShader(m_diligent->pDevice, "resources/shaders/transparent.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pPS = CompileShader(m_diligent->pDevice, "resources/shaders/transparent.frag", Diligent::SHADER_TYPE_PIXEL);
    if (!pVS || !pPS) return;

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
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    auto& RT0 = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    RT0.BlendEnable = true;
    RT0.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    RT0.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOp = Diligent::BLEND_OPERATION_ADD;
    RT0.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    RT0.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;

    Diligent::LayoutElement LayoutElems[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
        Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false}
    };
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    const auto variables = DescribeVariables({pVS, pPS});
    UseVariables(PSOCreateInfo.PSODesc.ResourceLayout, variables);

    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pTransparentPSO);
    if (!m_diligent->pTransparentPSO) Lit::Log::Error("Failed to create Transparent PSO");
    CreateBindings(m_diligent->pTransparentPSO, m_diligent->pTransparentSRB);
}

void Renderer::createHiZPSO() {
    m_diligent->pHiZMipmapPSO = CreateComputePipeline(m_diligent->pDevice, "Hi-Z Mipmap PSO", "resources/shaders/hiz_mipmap.comp", {"u_sourceMip", "u_destMip"});
    CreateBindings(m_diligent->pHiZMipmapPSO, std::span(std::addressof(m_diligent->pHiZMipmapSRB), 1));
}

void Renderer::createCullingPSO() {
    m_diligent->pCullingPSO = CreateComputePipeline(m_diligent->pDevice, "Culling PSO", "resources/shaders/cull.comp", {"u_hizTexture"});
    m_diligent->pCullingUniforms = CreateUniformBuffer(m_diligent->pDevice, "Culling Uniforms", 256, Diligent::USAGE_DEFAULT);
}

void Renderer::createCommandGenPSO() {
    m_diligent->pCommandGenPSO = CreateComputePipeline(m_diligent->pDevice, "Command Gen PSO", "resources/shaders/command_gen.comp");
    m_diligent->pCommandGenConstants = CreateUniformBuffer(m_diligent->pDevice, "Command Gen Constants", sizeof(CommandGenUniforms));
}

void Renderer::createPrefixSumPSO() {
    m_diligent->pPrefixSumPSO = CreateComputePipeline(m_diligent->pDevice, "Prefix Sum PSO", "resources/shaders/prefix_sum.comp");
    m_diligent->pPrefixSumConstants = CreateUniformBuffer(m_diligent->pDevice, "Prefix Sum Constants", sizeof(PrefixSumConstants));
}

void Renderer::createScatterPSO() {
    m_diligent->pScatterPSO = CreateComputePipeline(m_diligent->pDevice, "Scatter PSO", "resources/shaders/scatter.comp");
    m_diligent->pScatterConstants = CreateUniformBuffer(m_diligent->pDevice, "Scatter Constants", sizeof(ScatterConstants));
    m_diligent->pLargeScatterConstants = CreateUniformBuffer(m_diligent->pDevice, "Large Object Scatter Constants", sizeof(ScatterConstants));
}

void Renderer::createDispatchArgsPSO() {
    m_diligent->pDispatchArgsPSO = CreateComputePipeline(m_diligent->pDevice, "Dispatch Args PSO", "resources/shaders/dispatch_args.comp");
    m_diligent->pDispatchArgsConstants = CreateUniformBuffer(m_diligent->pDevice, "Dispatch Args Constants", sizeof(DispatchArgsConstants), Diligent::USAGE_DEFAULT);
    m_diligent->pScatterDispatchArgsConstants = CreateUniformBuffer(m_diligent->pDevice, "Scatter Dispatch Args Constants", sizeof(DispatchArgsConstants), Diligent::USAGE_DEFAULT);
}

void Renderer::createApplyDirtyPSO() {
    m_diligent->pApplyDirtyPSO = CreateComputePipeline(m_diligent->pDevice, "Apply Dirty PSO", "resources/shaders/apply_dirty.comp");
    m_diligent->pApplyDirtyConstants = CreateUniformBuffer(m_diligent->pDevice, "Apply Dirty Constants", sizeof(ApplyDirtyConstants));
}

void Renderer::createMarkTouchedPSO() {
    m_diligent->pMarkTouchedPSO = CreateComputePipeline(m_diligent->pDevice, "Mark Touched PSO", "resources/shaders/mark_touched.comp");
    m_diligent->pMarkTouchedConstants = CreateUniformBuffer(m_diligent->pDevice, "Mark Touched Constants", sizeof(MarkTouchedConstants));
}

void Renderer::createLargeObjectCullPSO() {
    m_diligent->pLargeObjectCullPSO = CreateComputePipeline(m_diligent->pDevice, "Large Object Cull PSO", "resources/shaders/large_object_cull.comp");
    m_diligent->pLargeObjectCullConstants = CreateUniformBuffer(m_diligent->pDevice, "Large Object Cull Constants", sizeof(LargeObjectCullUniforms));
}

void Renderer::createDebugDepthPSO() {
    auto pVS = CompileShader(m_diligent->pDevice, "resources/shaders/debug_depth.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pPS = CompileShader(m_diligent->pDevice, "resources/shaders/debug_depth.frag", Diligent::SHADER_TYPE_PIXEL);
    if (!pVS || !pPS) return;

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

    const auto variables = DescribeVariables({pVS, pPS});
    UseVariables(PSOCreateInfo.PSODesc.ResourceLayout, variables);

    const Diligent::SamplerDesc pointClamp{Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP};
    Diligent::ImmutableSamplerDesc Samplers[] = {{Diligent::SHADER_TYPE_PIXEL, "u_depthTexture", pointClamp}};
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = Samplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(Samplers);

    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pDebugDepthPSO);
    if (!m_diligent->pDebugDepthPSO) {
        Lit::Log::Error("Failed to create Debug Depth PSO");
        return;
    }

    m_diligent->pDebugDepthUniforms = CreateUniformBuffer(m_diligent->pDevice, "Debug Depth Uniforms", 4 * sizeof(float));
    m_diligent->pDebugDepthPSO->CreateShaderResourceBinding(&m_diligent->pDebugDepthSRB, true);
}

void Renderer::setLights(bool enabled, const std::array<glm::vec4, 2>& directional, const std::vector<glm::vec4>& packed) {
    const bool sameLights = m_lightData.size() == packed.size() && (packed.empty() || std::memcmp(m_lightData.data(), packed.data(), packed.size() * sizeof(glm::vec4)) == 0);
    const bool sameDirectional = std::memcmp(m_directional.data(), directional.data(), sizeof(glm::vec4) * 2) == 0;
    if (m_lightOverride != enabled || !sameDirectional || !sameLights) m_lightDataDirty = true;
    m_lightOverride = enabled;
    m_directional = directional;
    m_lightData = packed;
}

void Renderer::releasePipelines() {
    m_diligent->pTransformPSO.Release();
    m_diligent->pAnimPSO.Release();
    m_diligent->pHiZMipmapSRB.Release();
    m_diligent->pHiZMipmapPSO.Release();
    m_diligent->pCullingPSO.Release();
    m_diligent->pCommandGenPSO.Release();
    m_diligent->pPrefixSumPSO.Release();
    m_diligent->pScatterPSO.Release();
    m_diligent->pApplyDirtyPSO.Release();
    m_diligent->pMarkTouchedPSO.Release();
    m_diligent->pLargeObjectCullPSO.Release();
    m_diligent->pTransparentCullPSO.Release();
    m_diligent->pTransparentCommandGenPSO.Release();
    m_diligent->pDepthPrepassPSO.Release();
    m_diligent->pTransparentPSO.Release();
    m_diligent->pDebugDepthPSO.Release();
    m_diligent->pDebugLinePSO.Release();
    m_diligent->pDebugLineSRB.Release();
    m_diligent->pDebugDepthSRB.Release();
    m_diligent->pDispatchArgsPSO.Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pTransformSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pAnimSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pCullingSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pCommandGenSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pPrefixSumSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pScatterSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pApplyDirtySRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pMarkTouchedSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pLargeObjectCullSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pTransparentCullSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pTransparentCommandGenSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pDepthPrepassSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pTransparentSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pTransparentDispatchArgsSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pLargeObjectDispatchArgsSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pScatterDispatchArgsSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pLargePrefixSumSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pLargeScatterSRB[f].Release();
    for (int f = 0; f < DiligentData::NumFrames; ++f) m_diligent->pLargeCommandGenSRB[f].Release();
    m_diligent->pOpaquePSOs.clear();
    m_diligent->pPointPSOs.clear();
    m_diligent->pOpaqueSRBs.clear();
    m_diligent->pPointSRBs.clear();
    m_diligent->pTransformUniforms.Release();
    m_diligent->pCullingUniforms.Release();
    m_diligent->pCommandGenConstants.Release();
    m_diligent->pPrefixSumConstants.Release();
    m_diligent->pScatterConstants.Release();
    m_diligent->pLargeScatterConstants.Release();
    m_diligent->pDispatchArgsConstants.Release();
    m_diligent->pScatterDispatchArgsConstants.Release();
    m_diligent->pApplyDirtyConstants.Release();
    m_diligent->pMarkTouchedConstants.Release();
    m_diligent->pLargeObjectCullConstants.Release();
    m_diligent->pDebugDepthUniforms.Release();
    m_diligent->pDebugLineUBO.Release();
    m_diligent->pDebugLineVB.Release();
    m_diligent->pDebugLineOverlayPSO.Release();
    m_diligent->pDebugLineOverlaySRB.Release();
    m_diligent->pAnimConstants.Release();
}

void Renderer::reloadShaders() {
    if (!m_initialized || !m_diligent) return;
    if (!AllShadersCompile(m_diligent->pDevice)) {
        Lit::Log::Error("Shader reload skipped: fix the compile errors above and save again.");
        return;
    }
    m_diligent->pImmediateContext->WaitForIdle();
    releasePipelines();
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
    createTransparentCullPSO();
    createTransparentCommandGenPSO();
    createDebugDepthPSO();
    createDebugLinePSO();
    reallocateBuffers(m_maxObjects);
    m_lightDataDirty = true;
    Lit::Log::Info("Shaders reloaded");
}

void Renderer::setMaterial(uint32_t index, const glm::vec4& colorAndStrength) {
    if (index >= kMaxMaterials || !m_diligent || !m_diligent->pMaterialBuffer) return;
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pMaterialBuffer, static_cast<Diligent::Uint64>(index) * sizeof(glm::vec4), sizeof(glm::vec4), &colorAndStrength, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::addDebugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, bool overlay) {
    const DebugLineVertex vertices[2] = {{from, color}, {to, color}};
    const float* raw = reinterpret_cast<const float*>(vertices);
    std::vector<float>& target = overlay ? m_debugOverlayLines : m_debugLines;
    target.insert(target.end(), raw, raw + 2 * sizeof(DebugLineVertex) / sizeof(float));
}

void Renderer::createDebugLinePSO() {
    auto pVS = CompileShader(m_diligent->pDevice, "resources/shaders/debug_line.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pPS = CompileShader(m_diligent->pDevice, "resources/shaders/debug_line.frag", Diligent::SHADER_TYPE_PIXEL);
    if (!pVS || !pPS) return;

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

    const auto variables = DescribeVariables({pVS, pPS});
    UseVariables(PSOCreateInfo.PSODesc.ResourceLayout, variables);

    m_diligent->pDebugLineUBO = CreateUniformBuffer(m_diligent->pDevice, "Debug Line Uniforms", sizeof(glm::mat4));

    Diligent::BufferDesc VBDesc;
    VBDesc.Name = "Debug Line Vertices";
    VBDesc.Usage = Diligent::USAGE_DEFAULT;
    VBDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    VBDesc.Size = sizeof(DebugLineVertex) * (1u << 17);
    m_diligent->pDevice->CreateBuffer(VBDesc, nullptr, &m_diligent->pDebugLineVB);

    auto createLinePipeline = [&](const char* name, bool depthTest, Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
        PSOCreateInfo.PSODesc.Name = name;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = depthTest;
        m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pso);
        if (!pso) {
            Lit::Log::Error("Failed to create {}", name);
            return;
        }
        pso->CreateShaderResourceBinding(&srb, true);
        if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "DebugLineUniforms")) var->Set(m_diligent->pDebugLineUBO);
    };
    createLinePipeline("Debug Line PSO", true, m_diligent->pDebugLinePSO, m_diligent->pDebugLineSRB);
    createLinePipeline("Debug Line Overlay PSO", false, m_diligent->pDebugLineOverlayPSO, m_diligent->pDebugLineOverlaySRB);
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
            if (var) var->Set(m_diligent->pBasePositionBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
        if (m_diligent->pAnimSRB[i] && m_diligent->pBasePositionBuffer) {
            auto* var = m_diligent->pAnimSRB[i]->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BasePositionBuffer");
            if (var) var->Set(m_diligent->pBasePositionBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
    }
}

void Renderer::createAnimPSO() {
    m_diligent->pAnimPSO = CreateComputePipeline(m_diligent->pDevice, "Anim PSO", "resources/shaders/anim.comp");
    m_diligent->pAnimConstants = CreateUniformBuffer(m_diligent->pDevice, "Anim Constants", 4 * sizeof(float));
}
