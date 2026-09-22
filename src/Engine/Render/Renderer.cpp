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

struct CullingUniforms {
    uint32_t objectCount;
    uint32_t maxDraws;
    float smallObjectThreshold;
    float lodBias;
    int32_t forcedLod;
    uint32_t opaqueShaderCount;
};

constexpr uint32_t MAX_MESHES = 2048;
constexpr Diligent::TEXTURE_FORMAT kSceneDepthFormat = Diligent::TEX_FORMAT_D32_FLOAT;
constexpr Diligent::TEXTURE_FORMAT kAccumulationFormat = Diligent::TEX_FORMAT_RGBA16_FLOAT;
constexpr Diligent::TEXTURE_FORMAT kRevealageFormat = Diligent::TEX_FORMAT_R16_FLOAT;

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

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateDispatchArgsBuffer(Diligent::IRenderDevice* pDevice, const char* name) {
    Diligent::BufferDesc Desc;
    Desc.Name = name;
    Desc.Usage = Diligent::USAGE_DEFAULT;
    Desc.BindFlags = Diligent::BIND_INDIRECT_DRAW_ARGS | Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_SHADER_RESOURCE;
    Desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(unsigned int);
    Desc.Size = sizeof(unsigned int) * 4;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    pDevice->CreateBuffer(Desc, nullptr, &pBuffer);
    if (!pBuffer) { Lit::Log::Error("Failed to allocate dispatch argument buffer '{}'.", name); }
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

uint8_t PackAlpha(float alpha) { return static_cast<uint8_t>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)); }

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

void SetDynamicVariable(Diligent::IShaderResourceBinding* pSRB, const char* name, Diligent::IDeviceObject* pObject, Diligent::SHADER_TYPE stage = Diligent::SHADER_TYPE_COMPUTE) {
    if (!pSRB) return;
    if (auto* pVariable = pSRB->GetVariableByName(stage, name)) pVariable->Set(pObject);
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
    const Diligent::ShaderMacro retestMacros[] = {{"RETEST", "1"}};
    const Diligent::ShaderMacro transparentMacros[] = {{"TRANSPARENT", "1"}};
    bool allCompile = true;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("resources/shaders", ec)) {
        const std::string extension = entry.path().extension().string();
        for (const auto& [stageExtension, type] : stages) {
            if (extension != stageExtension) continue;
            const std::string path = entry.path().generic_string();
            allCompile &= CompileShader(pDevice, path, type) != nullptr;
            if (type == Diligent::SHADER_TYPE_PIXEL) allCompile &= CompileShader(pDevice, path, type, Diligent::ShaderMacroArray{pointMacros, 1}) != nullptr;
            if (type == Diligent::SHADER_TYPE_COMPUTE) allCompile &= CompileShader(pDevice, path, type, Diligent::ShaderMacroArray{retestMacros, 1}) != nullptr;
            if (type != Diligent::SHADER_TYPE_PIXEL) allCompile &= CompileShader(pDevice, path, type, Diligent::ShaderMacroArray{transparentMacros, 1}) != nullptr;
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

Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateComputePipeline(Diligent::IRenderDevice* pDevice, const char* name, const std::string& path, std::initializer_list<std::string_view> dynamicVariables = {},
                                                                        const Diligent::ShaderMacroArray& macros = {}) {
    Diligent::RefCntAutoPtr<Diligent::IShader> pCS = CompileShader(pDevice, path, Diligent::SHADER_TYPE_COMPUTE, macros);
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

enum class GpuPass : uint8_t {
    Upload,
    Transform,
    Cull,
    CommandGen,
    OpaqueDraw,
    HiZ,
    RetestCull,
    RetestCommandGen,
    RetestDraw,
    TransparentCull,
    TransparentCommandGen,
    TransparentDraw,
    Ui,
    Count
};

constexpr size_t kGpuPassCount = static_cast<size_t>(GpuPass::Count);
constexpr std::array<const char*, kGpuPassCount> kGpuPassNames = {"Upload", "Transform", "Cull", "CmdGen", "Opaque Draw", "Hi-Z", "Retest Cull", "Retest CmdGen",
                                                                  "Retest Draw", "Trans Cull", "Trans CmdGen", "Trans Draw", "UI"};

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
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSceneUBO[NumFrames];


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
    Diligent::RefCntAutoPtr<Diligent::ISampler> pHiZSampler;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pHiZMipViewsSource[NumFrames][32];
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pHiZMipViewsDest[NumFrames][32];

    bool QueryReady[NumFrames] = {false};

    struct VisibilityPhase {
        Diligent::RefCntAutoPtr<Diligent::IBuffer> cullingUniforms;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> visibleCounter;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> visibleEntries;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> sortedEntries;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> bucketCounts;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> bucketOffsets;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> bucketWriteHeads;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> scatterDispatchArgs;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> drawCommands;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> drawCounters;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> pointCommands;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> pointCounters;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> cullSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> prefixSumSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> scatterDispatchArgsSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> scatterSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> commandGenSRB[NumFrames];
        std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> opaqueSRBs[NumFrames];
        std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> pointSRBs[NumFrames];
    };
    VisibilityPhase visibility[3];
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentPointPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCandidateCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCandidateIds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentDispatchArgs;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pAlphaBuffer;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pAccumulation;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pRevealage;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCompositePSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pCompositeSRB;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pRetestCullingPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pRetestCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pRetestIds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pRetestDispatchArgs;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pObjectDispatchArgsConstants;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pRetestDispatchArgsSRB[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::ITexture> pSceneDepth;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pHiZFromDepthPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pHiZFromDepthSRB;

    Diligent::RefCntAutoPtr<Diligent::IQuery> passQueries[kGpuPassCount][NumFrames][2];
    bool passRecorded[NumFrames][kGpuPassCount] = {};
    double passMilliseconds[kGpuPassCount] = {};
    double gpuTotalMilliseconds = 0.0;
    double fenceWaitMilliseconds = 0.0;
    float profileLogTimer = 0.0f;

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pHiZMipmapSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pApplyDirtySRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pMarkTouchedSRB[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pHiZMipmapPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCullingPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCommandGenPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPrefixSumPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pScatterPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pApplyDirtyPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pMarkTouchedPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentCullPSO;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCommandGenConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pPrefixSumConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pScatterConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pApplyDirtyConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pMarkTouchedConstants;





    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentPSO;
    std::vector<Diligent::RefCntAutoPtr<Diligent::IPipelineState>> pOpaquePSOs;
    std::vector<Diligent::RefCntAutoPtr<Diligent::IPipelineState>> pPointPSOs;


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



};

std::vector<Diligent::IPipelineState*> SurfacePipelines(const DiligentData& d, size_t phaseIndex, bool points) {
    if (phaseIndex == 2) return {points ? d.pTransparentPointPSO.RawPtr() : d.pTransparentPSO.RawPtr()};
    std::vector<Diligent::IPipelineState*> pipelines;
    for (const auto& pso : points ? d.pPointPSOs : d.pOpaquePSOs) pipelines.push_back(pso);
    return pipelines;
}

void ReadGpuTimings(DiligentData& d, int frame) {
    d.gpuTotalMilliseconds = 0.0;
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) {
        d.passMilliseconds[pass] = 0.0;
        if (!d.passRecorded[frame][pass]) continue;
        Diligent::QueryDataTimestamp start{};
        Diligent::QueryDataTimestamp end{};
        if (!d.passQueries[pass][frame][0]->GetData(&start, sizeof(start), false) || !d.passQueries[pass][frame][1]->GetData(&end, sizeof(end), false)) continue;
        if (end.Counter > start.Counter && end.Frequency > 0) d.passMilliseconds[pass] = static_cast<double>(end.Counter - start.Counter) / static_cast<double>(end.Frequency) * 1000.0;
        d.gpuTotalMilliseconds += d.passMilliseconds[pass];
    }
}

void LogGpuTimings(const DiligentData& d) {
    Lit::Log::Info("GPU Total: {:.2f} ms | CPU Fence Wait: {:.2f} ms", d.gpuTotalMilliseconds, d.fenceWaitMilliseconds);
    std::string line;
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) {
        line += std::format("{}{}: {:.2f} ms", line.empty() ? "  " : " | ", kGpuPassNames[pass], d.passMilliseconds[pass]);
        if (pass % 4 == 3 || pass + 1 == kGpuPassCount) {
            Lit::Log::Info("{}", line);
            line.clear();
        }
    }
}

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
    EngineCI.Features.IndependentBlend = Diligent::DEVICE_FEATURE_STATE_ENABLED;
    m_diligent->pFactoryVk->CreateDeviceAndContextsVk(EngineCI, &m_diligent->pDevice, &m_diligent->pImmediateContext);

    Diligent::SwapChainDesc SCDesc;
    SCDesc.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
    SCDesc.DepthBufferFormat = Diligent::TEX_FORMAT_UNKNOWN;
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
    m_uiManager->init(m_diligent->pDevice, m_diligent->pImmediateContext, m_diligent->pSwapChain, kSceneDepthFormat, windowWidth, windowHeight);

    createPipelines();
    m_numDrawingShaders = m_diligent->pOpaquePSOs.size();

    Diligent::QueryDesc queryDesc;
    queryDesc.Type = Diligent::QUERY_TYPE_TIMESTAMP;
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) {
        queryDesc.Name = kGpuPassNames[pass];
        for (auto& frameQueries : m_diligent->passQueries[pass]) {
            for (auto& query : frameQueries) m_diligent->pDevice->CreateQuery(queryDesc, &query);
        }
    }

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
    if (!pipelinesReady()) Lit::Log::Error("Renderer pipelines failed to build; nothing will be drawn until the shader errors above are fixed.");

    m_maxObjects = 500000;
    reallocateBuffers(m_maxObjects);

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

    const auto objectCapacity = static_cast<Diligent::Uint32>(m_maxObjects);
    const size_t commandBytes = static_cast<size_t>(MAX_MESHES) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand);
    const unsigned int zero = 0;
    for (auto& phase : m_diligent->visibility) {
        phase.cullingUniforms = CreateUniformBuffer(m_diligent->pDevice, "Culling Uniforms", sizeof(CullingUniforms), Diligent::USAGE_DEFAULT);
        phase.visibleCounter = CreateStructuredBuffer(m_diligent->pDevice, "Visible Counter", sizeof(unsigned int), 1, (void*)&zero);
        phase.visibleEntries = CreateStructuredBuffer(m_diligent->pDevice, "Visible Entries", sizeof(unsigned int) * 2, objectCapacity);
        phase.sortedEntries = CreateStructuredBuffer(m_diligent->pDevice, "Sorted Visible Entries", sizeof(unsigned int) * 2, objectCapacity);
        phase.bucketCounts = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Counts", sizeof(unsigned int), totalBuckets);
        phase.bucketOffsets = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Offsets", sizeof(unsigned int), totalBuckets);
        phase.bucketWriteHeads = CreateStructuredBuffer(m_diligent->pDevice, "Bucket Write Heads", sizeof(unsigned int), totalBuckets);
        phase.scatterDispatchArgs = CreateDispatchArgsBuffer(m_diligent->pDevice, "Scatter Dispatch Args");
        phase.drawCommands = CreateIndirectBuffer(m_diligent->pDevice, "Draw Commands", commandBytes);
        phase.drawCounters = CreateStructuredBuffer(m_diligent->pDevice, "Draw Counters", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_numDrawingShaders), m_bucketZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
        phase.pointCommands = CreateIndirectBuffer(m_diligent->pDevice, "Point Commands", commandBytes);
        phase.pointCounters = CreateStructuredBuffer(m_diligent->pDevice, "Point Counters", sizeof(unsigned int), static_cast<Diligent::Uint32>(m_numDrawingShaders), m_bucketZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
    }
    m_diligent->pRetestCounter = CreateStructuredBuffer(m_diligent->pDevice, "Retest Counter", sizeof(unsigned int), 1, (void*)&zero);
    m_diligent->pRetestIds = CreateStructuredBuffer(m_diligent->pDevice, "Retest Ids", sizeof(unsigned int), objectCapacity);
    m_diligent->pRetestDispatchArgs = CreateDispatchArgsBuffer(m_diligent->pDevice, "Retest Dispatch Args");
    m_diligent->pTransparentCandidateCounter = CreateStructuredBuffer(m_diligent->pDevice, "Transparent Candidate Counter", sizeof(unsigned int), 1, (void*)&zero);
    m_diligent->pTransparentCandidateIds = CreateStructuredBuffer(m_diligent->pDevice, "Transparent Candidate Ids", sizeof(unsigned int), objectCapacity);
    m_diligent->pTransparentDispatchArgs = CreateDispatchArgsBuffer(m_diligent->pDevice, "Transparent Dispatch Args");
    m_diligent->pAlphaBuffer = CreateStructuredBuffer(m_diligent->pDevice, "Alpha Buffer", sizeof(uint32_t), (objectCapacity + 3) / 4);

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




        Diligent::BufferDesc SceneUBODesc;
        SceneUBODesc.Name = "Scene UBO";
        SceneUBODesc.Usage = Diligent::USAGE_DEFAULT;
        SceneUBODesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        SceneUBODesc.Size = (sizeof(SceneUniforms) + 255) & ~255;
        m_diligent->pSceneUBO[i].Release();
        m_diligent->pDevice->CreateBuffer(SceneUBODesc, nullptr, &m_diligent->pSceneUBO[i]);
    }

    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        const auto& main = m_diligent->visibility[0];
        const auto& retest = m_diligent->visibility[1];
        const bool allocationsOk = m_diligent->pObjectBuffer[i] && m_diligent->pWorldMatrixBuffer[i] && m_diligent->pCullSphereBuffer[i] && m_diligent->pCullOrientationBuffer[i] &&
                                   m_diligent->pHierarchyBuffer[i] && m_diligent->pRenderableBuffer[i] && m_diligent->pSortedHierarchyBuffer[i] && m_diligent->pTouchedEpochBuffer[i] &&
                                   m_diligent->pDirtyIndexBuffer[i] && m_diligent->pDirtyPayloadBuffer[i] && m_diligent->pSceneUBO[i] && m_diligent->pTransparentCandidateIds && m_diligent->pAlphaBuffer && main.visibleEntries && main.sortedEntries && retest.visibleEntries &&
                                   retest.sortedEntries && m_diligent->pRetestIds;
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
        CreateBoundSRB(d.pApplyDirtyPSO, d.pApplyDirtySRB[i],
                       {{"ApplyDirtyConstants", d.pApplyDirtyConstants}, {"DirtyIndexBuffer", d.pDirtyIndexBuffer[i]}, {"DirtyPayloadBuffer", d.pDirtyPayloadBuffer[i]}, {"TransformBuffer", d.pObjectBuffer[i]},
                        {"TouchedEpochBuffer", d.pTouchedEpochBuffer[i]}});
        CreateBoundSRB(d.pMarkTouchedPSO, d.pMarkTouchedSRB[i], {{"MarkTouchedConstants", d.pMarkTouchedConstants}, {"DirtyIndexBuffer", d.pDirtyIndexBuffer[i]}, {"TouchedEpochBuffer", d.pTouchedEpochBuffer[i]}});
        CreateBoundSRB(d.pDispatchArgsPSO, d.pTransparentDispatchArgsSRB[i],
                       {{"DispatchArgsConstants", d.pObjectDispatchArgsConstants}, {"SourceCounterBuffer", d.pTransparentCandidateCounter}, {"DispatchArgsBuffer", d.pTransparentDispatchArgs}});
        CreateBoundSRB(d.pDispatchArgsPSO, d.pRetestDispatchArgsSRB[i],
                       {{"DispatchArgsConstants", d.pObjectDispatchArgsConstants}, {"SourceCounterBuffer", d.pRetestCounter}, {"DispatchArgsBuffer", d.pRetestDispatchArgs}});

        for (size_t phaseIndex = 0; phaseIndex < std::size(d.visibility); ++phaseIndex) {
            auto& phase = d.visibility[phaseIndex];
            Diligent::IBuffer* pCandidateCounter = phaseIndex == 2 ? d.pTransparentCandidateCounter : d.pRetestCounter;
            Diligent::IBuffer* pCandidateIds = phaseIndex == 2 ? d.pTransparentCandidateIds : d.pRetestIds;
            CreateBoundSRB(phaseIndex == 0 ? d.pCullingPSO : phaseIndex == 1 ? d.pRetestCullingPSO : d.pTransparentCullPSO, phase.cullSRB[i],
                           {{"SceneData", d.pSceneUBO[i]}, {"CullingUniforms", phase.cullingUniforms}, {"AtomicCounterBuffer", phase.visibleCounter}, {"VisibleObjectBuffer", phase.visibleEntries},
                            {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"RenderableBuffer", d.pRenderableBuffer[i]}, {"BucketCountBuffer", phase.bucketCounts},
                            {"RetestCounterBuffer", d.pRetestCounter}, {"RetestBuffer", d.pRetestIds}, {"TransparentCounterBuffer", d.pTransparentCandidateCounter},
                            {"TransparentBuffer", d.pTransparentCandidateIds}, {"CandidateCounterBuffer", pCandidateCounter}, {"CandidateBuffer", pCandidateIds}});
            CreateBoundSRB(d.pPrefixSumPSO, phase.prefixSumSRB[i],
                           {{"PrefixSumConstants", d.pPrefixSumConstants}, {"BucketCountBuffer", phase.bucketCounts}, {"BucketOffsetBuffer", phase.bucketOffsets}, {"BucketWriteHeadBuffer", phase.bucketWriteHeads}});
            CreateBoundSRB(d.pDispatchArgsPSO, phase.scatterDispatchArgsSRB[i],
                           {{"DispatchArgsConstants", d.pObjectDispatchArgsConstants}, {"SourceCounterBuffer", phase.visibleCounter}, {"DispatchArgsBuffer", phase.scatterDispatchArgs}});
            CreateBoundSRB(d.pScatterPSO, phase.scatterSRB[i],
                           {{"ScatterConstants", d.pScatterConstants}, {"AtomicCounterBuffer", phase.visibleCounter}, {"VisibleObjectBuffer", phase.visibleEntries},
                            {"BucketWriteHeadBuffer", phase.bucketWriteHeads}, {"SortedVisibleObjectBuffer", phase.sortedEntries}});
            CreateBoundSRB(d.pCommandGenPSO, phase.commandGenSRB[i],
                           {{"CommandGenConstants", d.pCommandGenConstants}, {"PointCounterBuffer", phase.pointCounters}, {"PointCommandBuffer", phase.pointCommands},
                            {"DrawAtomicCounterBuffer", phase.drawCounters}, {"DrawCommandBuffer", phase.drawCommands}, {"MeshInfoBuffer", d.pMeshInfoBuffer},
                            {"BucketCountBuffer", phase.bucketCounts}, {"BucketOffsetBuffer", phase.bucketOffsets}});

            const std::initializer_list<NamedBuffer> surfaceResources = {
                {"SceneData", d.pSceneUBO[i]}, {"LightBlock", d.pLightBuffer}, {"MaterialBuffer", d.pMaterialBuffer}, {"WorldMatrixBuffer", d.pWorldMatrixBuffer[i]},
                {"CullSphereBuffer", d.pCullSphereBuffer[i]}, {"CullOrientationBuffer", d.pCullOrientationBuffer[i]}, {"NormalSampleBuffer", d.pNormalSampleBuffer},
                {"VisibleObjectBuffer", phase.sortedEntries}, {"AlphaBuffer", d.pAlphaBuffer}};
            const auto meshPipelines = SurfacePipelines(d, phaseIndex, false);
            const auto pointPipelines = SurfacePipelines(d, phaseIndex, true);
            phase.opaqueSRBs[i].resize(meshPipelines.size());
            phase.pointSRBs[i].resize(pointPipelines.size());
            for (size_t s = 0; s < meshPipelines.size(); ++s) CreateBoundSRB(meshPipelines[s], phase.opaqueSRBs[i][s], surfaceResources);
            for (size_t s = 0; s < pointPipelines.size(); ++s) CreateBoundSRB(pointPipelines[s], phase.pointSRBs[i][s], surfaceResources);
        }

    }

    if (m_diligent->pObjectDispatchArgsConstants) {
        DispatchArgsConstants dispatchArgsConstants{};
        dispatchArgsConstants.maxCount = static_cast<uint32_t>(m_maxObjects);
        dispatchArgsConstants.workgroupSize = 256;
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pObjectDispatchArgsConstants, 0, sizeof(dispatchArgsConstants), &dispatchArgsConstants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    m_fullTransformUpdateCounter = 1;
    m_transformUpdateCounter = 0;
    m_renderableUpdateCounter = 1;
    m_hierarchyUpdateCounter = 1;
}

void Renderer::createScreenTargets() {
    m_maxMipLevel = static_cast<int>(std::floor(std::log2(std::max(m_windowWidth, m_windowHeight))));

    Diligent::TextureDesc DepthDesc;
    DepthDesc.Name = "Scene Depth";
    DepthDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    DepthDesc.Width = m_windowWidth;
    DepthDesc.Height = m_windowHeight;
    DepthDesc.Format = kSceneDepthFormat;
    DepthDesc.Usage = Diligent::USAGE_DEFAULT;
    DepthDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
    m_diligent->pSceneDepth.Release();
    m_diligent->pDevice->CreateTexture(DepthDesc, nullptr, &m_diligent->pSceneDepth);
    if (m_diligent->pSceneDepth) m_diligent->pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(m_diligent->pHiZSampler);

    auto createColorTarget = [&](const char* name, Diligent::TEXTURE_FORMAT format, Diligent::RefCntAutoPtr<Diligent::ITexture>& texture) {
        Diligent::TextureDesc desc;
        desc.Name = name;
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = m_windowWidth;
        desc.Height = m_windowHeight;
        desc.Format = format;
        desc.Usage = Diligent::USAGE_DEFAULT;
        desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
        texture.Release();
        m_diligent->pDevice->CreateTexture(desc, nullptr, &texture);
        if (texture) texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(m_diligent->pHiZSampler);
    };
    createColorTarget("Transparency Accumulation", kAccumulationFormat, m_diligent->pAccumulation);
    createColorTarget("Transparency Revealage", kRevealageFormat, m_diligent->pRevealage);

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
        if (!m_diligent->pHiZTextures[i]) continue;

        const float farDepth[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        m_diligent->pImmediateContext->ClearRenderTarget(m_diligent->pHiZTextures[i]->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET), farDepth, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_diligent->pHiZTextures[i]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(m_diligent->pHiZSampler);

        for (int mip = 0; mip < m_maxMipLevel && mip < 32; ++mip) {
            Diligent::TextureViewDesc ViewDesc;
            ViewDesc.ViewType = Diligent::TEXTURE_VIEW_UNORDERED_ACCESS;
            ViewDesc.NumMipLevels = 1;
            ViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_WRITE;
            ViewDesc.MostDetailedMip = mip;
            m_diligent->pHiZTextures[i]->CreateView(ViewDesc, &m_diligent->pHiZMipViewsDest[i][mip]);
            if (mip == 0) continue;
            ViewDesc.AccessFlags = Diligent::UAV_ACCESS_FLAG_READ;
            ViewDesc.MostDetailedMip = mip - 1;
            m_diligent->pHiZTextures[i]->CreateView(ViewDesc, &m_diligent->pHiZMipViewsSource[i][mip]);
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

void Renderer::uploadDirtyHierarchies(SceneDatabase& sceneDatabase) {
    auto& dirty = sceneDatabase.structureDirty.list;
    std::ranges::sort(dirty);
    const size_t limit = std::min(sceneDatabase.hierarchies.size(), m_maxObjects);
    for (size_t i = 0; i < dirty.size();) {
        const Entity first = dirty[i];
        size_t end = i + 1;
        while (end < dirty.size() && dirty[end] == dirty[end - 1] + 1) ++end;
        const size_t last = std::min<size_t>(static_cast<size_t>(dirty[end - 1]) + 1, limit);
        if (first < last) {
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pHierarchyBuffer[m_currentFrame], first * sizeof(HierarchyComponent), (last - first) * sizeof(HierarchyComponent), sceneDatabase.hierarchies.data() + first,
                                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        i = end;
    }
}

void Renderer::uploadAllRenderables(const SceneDatabase& sceneDatabase) {
    const auto& renderables = sceneDatabase.renderables;
    m_packedRenderables.resize(renderables.size());
    for (size_t e = 0; e < renderables.size(); ++e) m_packedRenderables[e] = PackRenderable(renderables[e]);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pRenderableBuffer[m_currentFrame], 0, m_packedRenderables.size() * sizeof(uint32_t), m_packedRenderables.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_packedAlphas.assign((renderables.size() + 3) & ~size_t{3}, 0);
    for (size_t e = 0; e < renderables.size(); ++e) m_packedAlphas[e] = PackAlpha(renderables[e].alpha);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pAlphaBuffer, 0, m_packedAlphas.size(), m_packedAlphas.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::uploadDirtyRenderables(SceneDatabase& sceneDatabase) {
    auto& dirty = sceneDatabase.renderableDirty.list;
    std::ranges::sort(dirty);
    size_t rangeStart = 0;
    size_t rangeEnd = 0;
    auto flushRange = [&] {
        if (rangeEnd > rangeStart) {
            m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pRenderableBuffer[m_currentFrame], rangeStart * sizeof(uint32_t), (rangeEnd - rangeStart) * sizeof(uint32_t), m_packedRenderables.data() + rangeStart,
                                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        rangeStart = rangeEnd = 0;
    };
    for (Entity e : dirty) {
        if (e >= m_packedRenderables.size()) continue;
        const uint32_t packed = PackRenderable(sceneDatabase.renderables[e]);
        if (packed == m_packedRenderables[e]) continue;
        m_packedRenderables[e] = packed;
        if (e != rangeEnd) flushRange();
        if (rangeEnd == rangeStart) rangeStart = e;
        rangeEnd = static_cast<size_t>(e) + 1;
    }
    flushRange();

    for (Entity e : dirty) {
        if (e >= sceneDatabase.renderables.size() || e >= m_packedAlphas.size()) continue;
        const uint8_t alpha = PackAlpha(sceneDatabase.renderables[e].alpha);
        if (alpha == m_packedAlphas[e]) continue;
        m_packedAlphas[e] = alpha;
        const size_t word = e & ~Entity{3};
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pAlphaBuffer, word, 4, m_packedAlphas.data() + word, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

bool Renderer::animatedEntitiesAreLeaves(const SceneDatabase& sceneDatabase) {
    const std::array<uint64_t, 3> key = {sceneDatabase.m_structureVersion, m_animEntityOffset, m_animMovingCount};
    if (key == m_animLeafCheckKey) return m_animatedEntitiesAreLeaves;
    m_animLeafCheckKey = key;
    m_animatedEntitiesAreLeaves = true;
    const size_t end = std::min<size_t>(static_cast<size_t>(m_animEntityOffset) + m_animMovingCount, sceneDatabase.hierarchies.size());
    for (size_t e = m_animEntityOffset; e < end && m_animatedEntitiesAreLeaves; ++e) m_animatedEntitiesAreLeaves = !sceneDatabase.hasChildren(static_cast<Entity>(e));
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
        sceneDatabase.forEachChild(m_transformWorkScratch[next], enqueue);
    }
    if (m_transformWorkScratch.size() > MAX_TRANSFORM_WORK) return false;

    m_transformWorkLevelCounts.clear();
    for (Entity e : m_transformWorkScratch) {
        const uint32_t level = sceneDatabase.hierarchies[e].level;
        if (m_transformWorkLevelCounts.size() <= level) m_transformWorkLevelCounts.resize(level + 1, 0);
        m_transformWorkLevelCounts[level]++;
    }
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

    m_diligent->pImmediateContext->WaitForIdle();
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
void Renderer::setDebugDepthMode(bool enabled) { m_debugDepthMode = enabled; }
bool Renderer::isDebugDepthMode() const { return m_debugDepthMode; }
void Renderer::setFullProfiling(bool enabled) { fullProfiling = enabled; }

void Renderer::drawScene(SceneDatabase& sceneDatabase, const Camera& camera) {
    const float currentFrameTime = static_cast<float>(glfwGetTime());
    const float deltaTime = currentFrameTime - m_lastFrameTime;
    m_lastFrameTime = currentFrameTime;

    const unsigned int numObjects = sceneDatabase.renderables.size();
    if (numObjects > m_maxObjects) {
        m_diligent->pImmediateContext->WaitForIdle();
        reallocateBuffers(static_cast<size_t>(numObjects) + numObjects / 100 + 1024);
    }

    m_currentFrame = (m_currentFrame + 1) % NUM_FRAMES_IN_FLIGHT;

    const double fenceWaitStart = glfwGetTime();
    m_diligent->pFences[m_currentFrame]->Wait(m_diligent->FenceValues[m_currentFrame]);
    m_diligent->fenceWaitMilliseconds = (glfwGetTime() - fenceWaitStart) * 1000.0;

    if (m_diligent->QueryReady[m_currentFrame]) {
        ReadGpuTimings(*m_diligent, static_cast<int>(m_currentFrame));
        m_diligent->profileLogTimer += deltaTime;
        if (fullProfiling && m_diligent->profileLogTimer >= 0.5f) {
            LogGpuTimings(*m_diligent);
            m_diligent->profileLogTimer = 0.0f;
        }
    }

    const bool profileThisFrame = fullProfiling;
    bool (&passRecorded)[kGpuPassCount] = m_diligent->passRecorded[m_currentFrame];
    std::ranges::fill(passRecorded, false);
    auto beginPass = [&](GpuPass pass) {
        if (!profileThisFrame) return;
        passRecorded[static_cast<size_t>(pass)] = true;
        m_diligent->pImmediateContext->EndQuery(m_diligent->passQueries[static_cast<size_t>(pass)][m_currentFrame][0]);
    };
    auto endPass = [&](GpuPass pass) {
        if (profileThisFrame) m_diligent->pImmediateContext->EndQuery(m_diligent->passQueries[static_cast<size_t>(pass)][m_currentFrame][1]);
    };

    if (m_processedHierarchyVersion < sceneDatabase.m_hierarchyVersion) {
        m_hierarchyUpdateCounter = 1;
        m_processedHierarchyVersion = sceneDatabase.m_hierarchyVersion;
    }

    if (!m_initialized || s_meshInfos.empty() || !pipelinesReady()) {
        m_diligent->QueryReady[m_currentFrame] = false;
        return;
    }

    if (numObjects == 0) {
        m_diligent->QueryReady[m_currentFrame] = false;
        m_diligent->pImmediateContext->ClearRenderTarget(m_diligent->pSwapChain->GetCurrentBackBufferRTV(), glm::value_ptr(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return;
    }

    beginPass(GpuPass::Upload);

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
    }

    const std::vector<Entity>& newlyDirty = sceneDatabase.transformDirty.list;
    if (!newlyDirty.empty()) {
        if (newlyDirty.size() > MAX_DIRTY_PER_FRAME) {
            dirtyUsesBulkPath = true;
            m_transformUpdateCounter = 1;
        } else if (!dirtyUsesBulkPath) {
            m_pendingDirty[0].insert(m_pendingDirty[0].end(), newlyDirty.begin(), newlyDirty.end());
        }
        sceneDatabase.transformDirty.clear();
    }

    if (dirtyUsesBulkPath) {
        m_pendingDirty[0].clear();
    }

    auto ensureLevelOrder = [&] {
        if (m_processedStructureVersion == sceneDatabase.m_structureVersion) return;
        sceneDatabase.updateHierarchy();
        m_processedStructureVersion = sceneDatabase.m_structureVersion;
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pSortedHierarchyBuffer[m_currentFrame], 0, sceneDatabase.sortedHierarchyList.size() * sizeof(Entity), sceneDatabase.sortedHierarchyList.data(),
                                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    };

    if (m_hierarchyUpdateCounter > 0) {
        m_processedStructureVersion = 0;
        ensureLevelOrder();
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pHierarchyBuffer[m_currentFrame], 0, sceneDatabase.hierarchies.size() * sizeof(HierarchyComponent), sceneDatabase.hierarchies.data(),
                                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_hierarchyUpdateCounter--;
    } else if (!sceneDatabase.structureDirty.list.empty()) {
        uploadDirtyHierarchies(sceneDatabase);
    }
    sceneDatabase.structureDirty.clear();

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
        uploadAllRenderables(sceneDatabase);
        m_renderableUpdateCounter--;
    } else if (!sceneDatabase.renderableDirty.list.empty()) {
        uploadDirtyRenderables(sceneDatabase);
    }
    sceneDatabase.renderableDirty.clear();

    m_transformEpoch++;

    const bool forceAllLevels = hierarchyJustChanged || dirtyUsesBulkPath || fullTransformUpdateActive || (m_animMovingCount > 0 && !animatedEntitiesAreLeaves(sceneDatabase));
    if (forceAllLevels) ensureLevelOrder();
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
            ensureLevelOrder();
            levelNeedsProcessing.resize(sceneDatabase.m_maxHierarchyDepth + 1, 0);
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

    endPass(GpuPass::Upload);
    beginPass(GpuPass::Transform);

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

    endPass(GpuPass::Transform);

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
    const size_t subPassMaxObjects = std::max<size_t>(m_maxObjects / 4, 1024);
    const int previousFrame = static_cast<int>((m_currentFrame + NUM_FRAMES_IN_FLIGHT - 1) % NUM_FRAMES_IN_FLIGHT);

    beginPass(GpuPass::Cull);
    cullVisibilityPhase(0, numObjects, previousFrame);
    endPass(GpuPass::Cull);
    beginPass(GpuPass::CommandGen);
    buildVisibilityDraws(0);
    endPass(GpuPass::CommandGen);

    bindSceneTargets(true);
    beginPass(GpuPass::OpaqueDraw);
    drawVisibilityPhase(0);
    endPass(GpuPass::OpaqueDraw);

    m_diligent->pImmediateContext->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    beginPass(GpuPass::HiZ);
    buildHiZ();
    endPass(GpuPass::HiZ);

    beginPass(GpuPass::RetestCull);
    cullVisibilityPhase(1, numObjects, static_cast<int>(m_currentFrame));
    endPass(GpuPass::RetestCull);
    beginPass(GpuPass::RetestCommandGen);
    buildVisibilityDraws(1);
    endPass(GpuPass::RetestCommandGen);

    bindSceneTargets(false);
    beginPass(GpuPass::RetestDraw);
    drawVisibilityPhase(1);
    endPass(GpuPass::RetestDraw);

    beginPass(GpuPass::TransparentCull);
    cullVisibilityPhase(2, numObjects, static_cast<int>(m_currentFrame));
    endPass(GpuPass::TransparentCull);
    beginPass(GpuPass::TransparentCommandGen);
    buildVisibilityDraws(2);
    endPass(GpuPass::TransparentCommandGen);

    beginPass(GpuPass::TransparentDraw);
    drawTransparency();
    endPass(GpuPass::TransparentDraw);

    beginPass(GpuPass::Ui);

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

    if (fullProfiling) drawProfilingHud();

    m_uiManager->render();

    endPass(GpuPass::Ui);
    m_diligent->QueryReady[m_currentFrame] = profileThisFrame;

    m_diligent->CurrentFenceValue++;
    m_diligent->pImmediateContext->EnqueueSignal(m_diligent->pFences[m_currentFrame], m_diligent->CurrentFenceValue);
    m_diligent->FenceValues[m_currentFrame] = m_diligent->CurrentFenceValue;
}

bool Renderer::pipelinesReady() const {
    const auto& d = *m_diligent;
    const bool computeReady = d.pTransformPSO && d.pCullingPSO && d.pRetestCullingPSO && d.pPrefixSumPSO && d.pScatterPSO && d.pCommandGenPSO && d.pDispatchArgsPSO &&
                              d.pHiZMipmapPSO && d.pHiZFromDepthPSO && d.pApplyDirtyPSO && d.pMarkTouchedPSO && d.pTransparentCullPSO;
    const bool graphicsReady = d.pTransparentPSO && d.pTransparentPointPSO && d.pCompositePSO && !d.pOpaquePSOs.empty() && std::ranges::all_of(d.pOpaquePSOs, [](const auto& pso) { return pso != nullptr; }) &&
                               std::ranges::all_of(d.pPointPSOs, [](const auto& pso) { return pso != nullptr; });
    return computeReady && graphicsReady && d.pSceneDepth && d.pAccumulation && d.pRevealage;
}

void Renderer::cullVisibilityPhase(int phaseIndex, uint32_t objectCount, int hizFrame) {
    auto& d = *m_diligent;
    auto& phase = d.visibility[phaseIndex];
    auto* pContext = d.pImmediateContext.RawPtr();
    const bool candidateInput = phaseIndex != 0;
    const unsigned int zero = 0;

    pContext->UpdateBuffer(phase.visibleCounter, 0, sizeof(zero), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->UpdateBuffer(phase.bucketCounts, 0, sizeof(unsigned int) * m_bucketZeros.size(), m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (!candidateInput) {
        pContext->UpdateBuffer(d.pRetestCounter, 0, sizeof(zero), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pContext->UpdateBuffer(d.pTransparentCandidateCounter, 0, sizeof(zero), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    const CullingUniforms uniforms{objectCount, static_cast<uint32_t>(m_maxObjects), m_smallObjectThreshold, m_lodBias, m_forcedLod, static_cast<uint32_t>(m_numDrawingShaders)};
    pContext->UpdateBuffer(phase.cullingUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::IBuffer* pCandidateDispatchArgs = phaseIndex == 2 ? d.pTransparentDispatchArgs : d.pRetestDispatchArgs;
    if (candidateInput) {
        pContext->SetPipelineState(d.pDispatchArgsPSO);
        pContext->CommitShaderResources(phaseIndex == 2 ? d.pTransparentDispatchArgsSRB[m_currentFrame] : d.pRetestDispatchArgsSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));
    }

    auto& srb = phase.cullSRB[m_currentFrame];
    SetDynamicVariable(srb, "u_hizTexture", d.pHiZTextures[hizFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    pContext->SetPipelineState(phaseIndex == 0 ? d.pCullingPSO : phaseIndex == 1 ? d.pRetestCullingPSO : d.pTransparentCullPSO);
    pContext->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (candidateInput) {
        pContext->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{pCandidateDispatchArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    } else {
        pContext->DispatchCompute(Diligent::DispatchComputeAttribs((objectCount + 255) / 256, 1, 1));
    }
}

void Renderer::buildVisibilityDraws(int phaseIndex) {
    auto& d = *m_diligent;
    auto& phase = d.visibility[phaseIndex];
    auto* pContext = d.pImmediateContext.RawPtr();
    const uint32_t totalBuckets = static_cast<uint32_t>(m_numDrawingShaders) * MAX_MESHES;

    pContext->SetPipelineState(d.pPrefixSumPSO);
    {
        Diligent::MapHelper<PrefixSumConstants> constants(pContext, d.pPrefixSumConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        constants->bucketCount = totalBuckets;
    }
    pContext->CommitShaderResources(phase.prefixSumSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));

    Diligent::StateTransitionDesc writeHeadBarrier{phase.bucketWriteHeads, Diligent::RESOURCE_STATE_UNORDERED_ACCESS, Diligent::RESOURCE_STATE_UNORDERED_ACCESS, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE};
    pContext->TransitionResourceStates(1, &writeHeadBarrier);

    pContext->SetPipelineState(d.pDispatchArgsPSO);
    pContext->CommitShaderResources(phase.scatterDispatchArgsSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));

    pContext->SetPipelineState(d.pScatterPSO);
    {
        Diligent::MapHelper<ScatterConstants> constants(pContext, d.pScatterConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        constants->maxDraws = static_cast<uint32_t>(m_maxObjects);
    }
    pContext->CommitShaderResources(phase.scatterSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{phase.scatterDispatchArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});

    const size_t counterBytes = sizeof(unsigned int) * m_numDrawingShaders;
    pContext->UpdateBuffer(phase.drawCounters, 0, counterBytes, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->UpdateBuffer(phase.pointCounters, 0, counterBytes, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    pContext->SetPipelineState(d.pCommandGenPSO);
    {
        Diligent::MapHelper<CommandGenUniforms> constants(pContext, d.pCommandGenConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        constants->maxBucketsPerShader = MAX_MESHES;
        constants->totalBuckets = totalBuckets;
    }
    pContext->CommitShaderResources(phase.commandGenSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->DispatchCompute(Diligent::DispatchComputeAttribs((totalBuckets + 255) / 256, 1, 1));
}

void Renderer::bindSceneTargets(bool clear) {
    auto* pContext = m_diligent->pImmediateContext.RawPtr();
    auto* pRTV = m_diligent->pSwapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_diligent->pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);

    const auto& swapChainDesc = m_diligent->pSwapChain->GetDesc();
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(swapChainDesc.Width);
    viewport.Height = static_cast<float>(swapChainDesc.Height);
    pContext->SetViewports(1, &viewport, swapChainDesc.Width, swapChainDesc.Height);
    pContext->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (!clear) return;
    pContext->ClearRenderTarget(pRTV, glm::value_ptr(glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::drawVisibilityPhase(int phaseIndex) {
    auto& d = *m_diligent;
    auto& phase = d.visibility[phaseIndex];
    auto* pContext = d.pImmediateContext.RawPtr();

    Diligent::IBuffer* pVBs[] = {d.pVBO};
    pContext->SetVertexBuffers(0, 1, pVBs, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    pContext->SetIndexBuffer(d.pEBO, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const auto meshPipelines = SurfacePipelines(d, static_cast<size_t>(phaseIndex), false);
    const auto pointPipelines = SurfacePipelines(d, static_cast<size_t>(phaseIndex), true);
    for (uint32_t shaderId = 0; shaderId < meshPipelines.size(); ++shaderId) {
        const auto commandOffset = static_cast<Diligent::Uint64>(shaderId) * MAX_MESHES * sizeof(DrawElementsIndirectCommand);
        if (meshPipelines[shaderId]) {
            pContext->SetPipelineState(meshPipelines[shaderId]);
            pContext->CommitShaderResources(phase.opaqueSRBs[m_currentFrame][shaderId], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Diligent::DrawIndexedIndirectAttribs drawAttrs;
            drawAttrs.IndexType = Diligent::VT_UINT32;
            drawAttrs.pAttribsBuffer = phase.drawCommands;
            drawAttrs.DrawArgsOffset = commandOffset;
            drawAttrs.DrawCount = MAX_MESHES;
            drawAttrs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
            drawAttrs.pCounterBuffer = phase.drawCounters;
            drawAttrs.CounterOffset = shaderId * sizeof(unsigned int);
            drawAttrs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            drawAttrs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            pContext->DrawIndexedIndirect(drawAttrs);
        }
        if (shaderId < pointPipelines.size() && pointPipelines[shaderId]) {
            pContext->SetPipelineState(pointPipelines[shaderId]);
            pContext->CommitShaderResources(phase.pointSRBs[m_currentFrame][shaderId], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Diligent::DrawIndirectAttribs pointAttrs;
            pointAttrs.pAttribsBuffer = phase.pointCommands;
            pointAttrs.DrawArgsOffset = commandOffset;
            pointAttrs.DrawCount = MAX_MESHES;
            pointAttrs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
            pointAttrs.pCounterBuffer = phase.pointCounters;
            pointAttrs.CounterOffset = shaderId * sizeof(unsigned int);
            pointAttrs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            pointAttrs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            pContext->DrawIndirect(pointAttrs);
        }
    }
}

void Renderer::drawTransparency() {
    auto& d = *m_diligent;
    auto* pContext = d.pImmediateContext.RawPtr();
    Diligent::ITextureView* pTargets[] = {d.pAccumulation->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET), d.pRevealage->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
    pContext->SetRenderTargets(2, pTargets, d.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float noCoverage[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float fullRevealage[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    pContext->ClearRenderTarget(pTargets[0], noCoverage, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->ClearRenderTarget(pTargets[1], fullRevealage, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    drawVisibilityPhase(2);

    bindSceneTargets(false);
    SetDynamicVariable(d.pCompositeSRB, "u_accumulation", d.pAccumulation->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
    SetDynamicVariable(d.pCompositeSRB, "u_revealage", d.pRevealage->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
    pContext->SetPipelineState(d.pCompositePSO);
    pContext->CommitShaderResources(d.pCompositeSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_NONE});
}

void Renderer::buildHiZ() {
    auto& d = *m_diligent;
    auto* pContext = d.pImmediateContext.RawPtr();
    auto* pHiZ = d.pHiZTextures[m_currentFrame].RawPtr();
    if (!d.pHiZFromDepthPSO || !d.pHiZMipmapPSO || !pHiZ) return;

    auto hizBarrier = [&](Diligent::RESOURCE_STATE newState) {
        Diligent::StateTransitionDesc barrier{pHiZ, Diligent::RESOURCE_STATE_UNKNOWN, newState, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE};
        pContext->TransitionResourceStates(1, &barrier);
    };

    SetDynamicVariable(d.pHiZFromDepthSRB, "u_depth", d.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    SetDynamicVariable(d.pHiZFromDepthSRB, "u_destMip", d.pHiZMipViewsDest[m_currentFrame][0]);
    pContext->SetPipelineState(d.pHiZFromDepthPSO);
    pContext->CommitShaderResources(d.pHiZFromDepthSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->DispatchCompute(Diligent::DispatchComputeAttribs((m_windowWidth + 7) / 8, (m_windowHeight + 7) / 8, 1));
    hizBarrier(Diligent::RESOURCE_STATE_UNORDERED_ACCESS);

    pContext->SetPipelineState(d.pHiZMipmapPSO);
    for (int mip = 1; mip < m_maxMipLevel && mip < 32; ++mip) {
        SetDynamicVariable(d.pHiZMipmapSRB, "u_sourceMip", d.pHiZMipViewsSource[m_currentFrame][mip]);
        SetDynamicVariable(d.pHiZMipmapSRB, "u_destMip", d.pHiZMipViewsDest[m_currentFrame][mip]);
        pContext->CommitShaderResources(d.pHiZMipmapSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const int mipWidth = std::max(1, m_windowWidth >> mip);
        const int mipHeight = std::max(1, m_windowHeight >> mip);
        pContext->DispatchCompute(Diligent::DispatchComputeAttribs((mipWidth + 7) / 8, (mipHeight + 7) / 8, 1));
        hizBarrier(Diligent::RESOURCE_STATE_UNORDERED_ACCESS);
    }
    hizBarrier(Diligent::RESOURCE_STATE_SHADER_RESOURCE);
}

void Renderer::drawProfilingHud() {
    const auto& d = *m_diligent;
    float y = 120.0f;
    const float step = 18.0f;
    const float scale = 0.4f;
    const glm::vec3 headerColor(0.3f, 1.0f, 0.5f);
    const glm::vec3 textColor(0.9f, 0.9f, 0.9f);
    const glm::vec3 waitColor(1.0f, 0.4f, 0.4f);

    auto line = [&](const glm::vec3& color, std::string_view label, double milliseconds) {
        m_uiManager->addText(std::format("{}{:.3f} ms", label, milliseconds), 10.0f, y, scale, color);
        y += step;
    };
    m_uiManager->addText("--- Profiling HUD ---", 10.0f, y, scale, headerColor);
    y += step;
    line(waitColor, "CPU Fence Wait: ", d.fenceWaitMilliseconds);
    line(headerColor, "GPU Total: ", d.gpuTotalMilliseconds);
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) line(textColor, std::format("  {}: ", kGpuPassNames[pass]), d.passMilliseconds[pass]);
}

std::vector<GpuEntityState> Renderer::readGpuEntityStates(size_t count) {
    count = std::min(count, m_maxObjects);
    std::vector<GpuEntityState> states(count);
    if (!m_initialized || count == 0) return states;

    auto* pContext = m_diligent->pImmediateContext.RawPtr();
    auto readBack = [&](Diligent::IBuffer* pSource, size_t bytes, auto&& consume) {
        Diligent::BufferDesc desc;
        desc.Name = "Readback";
        desc.Usage = Diligent::USAGE_STAGING;
        desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
        desc.Size = bytes;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> pStaging;
        m_diligent->pDevice->CreateBuffer(desc, nullptr, &pStaging);
        pContext->CopyBuffer(pSource, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, pStaging, 0, bytes, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pContext->WaitForIdle();
        Diligent::MapHelper<float> mapped(pContext, pStaging, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
        consume(static_cast<const float*>(mapped));
    };

    readBack(m_diligent->pWorldMatrixBuffer[0], count * sizeof(glm::vec4) * 3, [&](const float* rows) {
        for (size_t e = 0; e < count; ++e) {
            glm::mat4 world(1.0f);
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 4; ++column) world[column][row] = rows[e * 12 + row * 4 + column];
            }
            states[e].world = world;
        }
    });
    readBack(m_diligent->pCullSphereBuffer[0], count * sizeof(glm::vec4), [&](const float* spheres) {
        for (size_t e = 0; e < count; ++e) states[e].cullSphere = glm::vec4(spheres[e * 4], spheres[e * 4 + 1], spheres[e * 4 + 2], spheres[e * 4 + 3]);
    });
    return states;
}

void Renderer::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) { m_uiManager->addText(text, x, y, scale, color); }

void Renderer::createTransformPSO() {
    m_diligent->pTransformPSO = CreateComputePipeline(m_diligent->pDevice, "Transform PSO", "resources/shaders/transform.comp");
    m_diligent->pTransformUniforms = CreateUniformBuffer(m_diligent->pDevice, "Transform Uniforms", 256, Diligent::USAGE_DEFAULT);
}

void Renderer::createTransparentCullPSO() {
    const Diligent::ShaderMacro transparentMacros[] = {{"TRANSPARENT", "1"}};
    m_diligent->pTransparentCullPSO = CreateComputePipeline(m_diligent->pDevice, "Transparent Cull PSO", "resources/shaders/cull.comp", {"u_hizTexture"}, Diligent::ShaderMacroArray{transparentMacros, 1});
}

Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateSurfacePipeline(DiligentData& d, const std::string& name, const std::string& fragmentPath, bool point, bool transparent) {
    std::vector<Diligent::ShaderMacro> vertexMacros;
    std::vector<Diligent::ShaderMacro> pixelMacros;
    if (transparent) vertexMacros.push_back({"TRANSPARENT", "1"});
    if (point) pixelMacros.push_back({"POINT_SPRITE", "1"});
    auto pVS = CompileShader(d.pDevice, point ? "resources/shaders/point.vert" : "resources/shaders/cube.vert", Diligent::SHADER_TYPE_VERTEX,
                             Diligent::ShaderMacroArray{vertexMacros.data(), static_cast<Diligent::Uint32>(vertexMacros.size())});
    auto pPS = CompileShader(d.pDevice, fragmentPath, Diligent::SHADER_TYPE_PIXEL, Diligent::ShaderMacroArray{pixelMacros.data(), static_cast<Diligent::Uint32>(pixelMacros.size())});
    if (!pVS || !pPS) return {};

    const std::string psoName = name + (point ? " (points)" : "");
    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = psoName.c_str();
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = point ? Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST : Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_BACK;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = true;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = !transparent;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    if (transparent) {
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 2;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = kAccumulationFormat;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[1] = kRevealageFormat;
        PSOCreateInfo.GraphicsPipeline.BlendDesc.IndependentBlendEnable = true;
        auto& accumulation = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
        accumulation.BlendEnable = true;
        accumulation.SrcBlend = accumulation.DestBlend = accumulation.SrcBlendAlpha = accumulation.DestBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        auto& revealage = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[1];
        revealage.BlendEnable = true;
        revealage.SrcBlend = revealage.SrcBlendAlpha = Diligent::BLEND_FACTOR_ZERO;
        revealage.DestBlend = revealage.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_COLOR;
    } else {
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = d.pSwapChain->GetDesc().ColorBufferFormat;
    }
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

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    d.pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pso);
    if (!pso) Lit::Log::Error("Failed to create surface pipeline '{}'", psoName);
    return pso;
}

void Renderer::createOpaquePSOs() {
    struct ShaderInfo {
        const char* frag;
        const char* name;
    };
    const ShaderInfo shaderInfos[] = {
        {"resources/shaders/cube.frag", "Cube PSO"},
        {"resources/shaders/red.frag",  "Red PSO" }
    };
    m_diligent->pOpaquePSOs.clear();
    m_diligent->pPointPSOs.clear();
    for (const ShaderInfo& info : shaderInfos) {
        m_diligent->pOpaquePSOs.push_back(CreateSurfacePipeline(*m_diligent, info.name, info.frag, false, false));
        m_diligent->pPointPSOs.push_back(CreateSurfacePipeline(*m_diligent, info.name, info.frag, true, false));
    }
}

void Renderer::createTransparentPSO() {
    m_diligent->pTransparentPSO = CreateSurfacePipeline(*m_diligent, "Transparent PSO", "resources/shaders/transparent.frag", false, true);
    m_diligent->pTransparentPointPSO = CreateSurfacePipeline(*m_diligent, "Transparent PSO", "resources/shaders/transparent.frag", true, true);

    auto pVS = CompileShader(m_diligent->pDevice, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pPS = CompileShader(m_diligent->pDevice, "resources/shaders/transparent_composite.frag", Diligent::SHADER_TYPE_PIXEL);
    m_diligent->pCompositePSO.Release();
    m_diligent->pCompositeSRB.Release();
    if (!pVS || !pPS) return;

    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Transparent Composite PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = m_diligent->pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    auto& blend = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;
    const auto variables = DescribeVariables({pVS, pPS}, {"u_accumulation", "u_revealage"});
    UseVariables(PSOCreateInfo.PSODesc.ResourceLayout, variables);
    m_diligent->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_diligent->pCompositePSO);
    if (!m_diligent->pCompositePSO) {
        Lit::Log::Error("Failed to create Transparent Composite PSO");
        return;
    }
    m_diligent->pCompositePSO->CreateShaderResourceBinding(&m_diligent->pCompositeSRB, true);
}


void Renderer::createHiZPSO() {
    m_diligent->pHiZMipmapPSO = CreateComputePipeline(m_diligent->pDevice, "Hi-Z Mipmap PSO", "resources/shaders/hiz_mipmap.comp", {"u_sourceMip", "u_destMip"});
    CreateBindings(m_diligent->pHiZMipmapPSO, std::span(std::addressof(m_diligent->pHiZMipmapSRB), 1));
    m_diligent->pHiZFromDepthPSO = CreateComputePipeline(m_diligent->pDevice, "Hi-Z From Depth PSO", "resources/shaders/hiz_from_depth.comp", {"u_depth", "u_destMip"});
    CreateBindings(m_diligent->pHiZFromDepthPSO, std::span(std::addressof(m_diligent->pHiZFromDepthSRB), 1));
}

void Renderer::createCullingPSO() {
    m_diligent->pCullingPSO = CreateComputePipeline(m_diligent->pDevice, "Culling PSO", "resources/shaders/cull.comp", {"u_hizTexture"});
    const Diligent::ShaderMacro retestMacros[] = {{"RETEST", "1"}};
    m_diligent->pRetestCullingPSO = CreateComputePipeline(m_diligent->pDevice, "Retest Culling PSO", "resources/shaders/cull.comp", {"u_hizTexture"}, Diligent::ShaderMacroArray{retestMacros, 1});
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
}

void Renderer::createDispatchArgsPSO() {
    m_diligent->pDispatchArgsPSO = CreateComputePipeline(m_diligent->pDevice, "Dispatch Args PSO", "resources/shaders/dispatch_args.comp");
    m_diligent->pDispatchArgsConstants = CreateUniformBuffer(m_diligent->pDevice, "Dispatch Args Constants", sizeof(DispatchArgsConstants), Diligent::USAGE_DEFAULT);
    m_diligent->pObjectDispatchArgsConstants = CreateUniformBuffer(m_diligent->pDevice, "Object Dispatch Args Constants", sizeof(DispatchArgsConstants), Diligent::USAGE_DEFAULT);
}

void Renderer::createApplyDirtyPSO() {
    m_diligent->pApplyDirtyPSO = CreateComputePipeline(m_diligent->pDevice, "Apply Dirty PSO", "resources/shaders/apply_dirty.comp");
    m_diligent->pApplyDirtyConstants = CreateUniformBuffer(m_diligent->pDevice, "Apply Dirty Constants", sizeof(ApplyDirtyConstants));
}

void Renderer::createMarkTouchedPSO() {
    m_diligent->pMarkTouchedPSO = CreateComputePipeline(m_diligent->pDevice, "Mark Touched PSO", "resources/shaders/mark_touched.comp");
    m_diligent->pMarkTouchedConstants = CreateUniformBuffer(m_diligent->pDevice, "Mark Touched Constants", sizeof(MarkTouchedConstants));
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
    PSOCreateInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
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

void Renderer::createPipelines() {
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
    createTransparentCullPSO();
    createDebugDepthPSO();
    createDebugLinePSO();
}

void Renderer::reloadShaders() {
    if (!m_initialized || !m_diligent) return;
    if (!AllShadersCompile(m_diligent->pDevice)) {
        Lit::Log::Error("Shader reload skipped: fix the compile errors above and save again.");
        return;
    }
    m_diligent->pImmediateContext->WaitForIdle();
    createPipelines();
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
    PSOCreateInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
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
