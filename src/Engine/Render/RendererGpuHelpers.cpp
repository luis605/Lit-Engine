module;

#include "RendererPrelude.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

std::string LoadSourceFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Lit::Log::Error("Failed to open shader file: {}", filepath);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Diligent::IDeviceObject* BufferResourceFor(Diligent::IBuffer* buffer, Diligent::SHADER_RESOURCE_TYPE type) {
    if (!buffer) return nullptr;
    switch (type) {
        case Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER:
            return buffer;
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV:
            return buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV:
            return buffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
        default:
            return nullptr;
    }
}

void BindBuffers(Diligent::IShaderResourceBinding* binding, std::initializer_list<NamedBuffer> buffers) {
    if (!binding) return;
    const auto& signature = binding->GetPipelineResourceSignature()->GetDesc();
    for (Diligent::Uint32 resourceIndex = 0; resourceIndex < signature.NumResources; ++resourceIndex) {
        const Diligent::PipelineResourceDesc& resource = signature.Resources[resourceIndex];
        if (resource.VarType == Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC) continue;
        const auto match = std::ranges::find(buffers, std::string_view(resource.Name), &NamedBuffer::name);
        Diligent::IDeviceObject* object = match != buffers.end() ? BufferResourceFor(match->buffer, resource.ResourceType) : nullptr;
        if (!object) {
            Lit::Log::Error("No buffer bound to '{}' in '{}'.", resource.Name, signature.Name);
            continue;
        }
        for (Diligent::Uint32 stages = resource.ShaderStages; stages != 0; stages &= stages - 1) {
            const auto stage = static_cast<Diligent::SHADER_TYPE>(stages & ~(stages - 1));
            if (auto* variable = binding->GetVariableByName(stage, resource.Name)) variable->Set(object, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
    }
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateBuffer(Diligent::IRenderDevice* device, const Diligent::BufferDesc& description, const Diligent::BufferData* initialData) {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    device->CreateBuffer(description, initialData, &buffer);
    return buffer;
}

}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateStructuredBuffer(Diligent::IRenderDevice* device, const char* name, Diligent::Uint32 elementSize, Diligent::Uint32 elementCount, void* initialData, Diligent::BIND_FLAGS extraFlags) {
    Diligent::BufferDesc description;
    description.Name = name;
    description.Usage = Diligent::USAGE_DEFAULT;
    description.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS | extraFlags;
    description.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    description.ElementByteStride = elementSize;
    description.Size = static_cast<Diligent::Uint64>(elementSize) * static_cast<Diligent::Uint64>(elementCount);

    Diligent::BufferData data;
    if (initialData) {
        data.pData = initialData;
        data.DataSize = description.Size;
    }

    auto buffer = CreateBuffer(device, description, initialData ? &data : nullptr);
    if (!buffer) Lit::Log::Error("Failed to allocate structured buffer '{}' ({} bytes).", name, description.Size);
    return buffer;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateDispatchArgsBuffer(Diligent::IRenderDevice* device, const char* name, uint32_t count) {
    constexpr uint32_t kArgumentWordsPerDispatch = 4;
    Diligent::BufferDesc description;
    description.Name = name;
    description.Usage = Diligent::USAGE_DEFAULT;
    description.BindFlags = Diligent::BIND_INDIRECT_DRAW_ARGS | Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_SHADER_RESOURCE;
    description.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    description.ElementByteStride = sizeof(unsigned int);
    description.Size = sizeof(unsigned int) * kArgumentWordsPerDispatch * count;

    auto buffer = CreateBuffer(device, description, nullptr);
    if (!buffer) Lit::Log::Error("Failed to allocate dispatch argument buffer '{}'.", name);
    return buffer;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateIndirectBuffer(Diligent::IRenderDevice* device, const char* name, size_t size) {
    Diligent::BufferDesc description;
    description.Name = name;
    description.Usage = Diligent::USAGE_DEFAULT;
    description.BindFlags = Diligent::BIND_INDIRECT_DRAW_ARGS | Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    description.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    description.ElementByteStride = sizeof(DrawElementsIndirectCommand);
    description.Size = size;

    auto buffer = CreateBuffer(device, description, nullptr);
    if (!buffer) Lit::Log::Error("Failed to allocate indirect buffer '{}' ({} bytes).", name, size);
    return buffer;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateUniformBuffer(Diligent::IRenderDevice* device, const char* name, Diligent::Uint64 size, Diligent::USAGE usage) {
    Diligent::BufferDesc description;
    description.Name = name;
    description.Usage = usage;
    description.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    description.CPUAccessFlags = usage == Diligent::USAGE_DYNAMIC ? Diligent::CPU_ACCESS_WRITE : Diligent::CPU_ACCESS_NONE;
    description.Size = size;

    auto buffer = CreateBuffer(device, description, nullptr);
    if (!buffer) Lit::Log::Error("Failed to allocate uniform buffer '{}'.", name);
    return buffer;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateVertexBuffer(Diligent::IRenderDevice* device, size_t size) {
    Diligent::BufferDesc description;
    description.Name = "VBO";
    description.Usage = Diligent::USAGE_DEFAULT;
    description.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    description.Size = size;
    return CreateBuffer(device, description, nullptr);
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateIndexBuffer(Diligent::IRenderDevice* device, size_t size) {
    Diligent::BufferDesc description;
    description.Name = "EBO";
    description.Usage = Diligent::USAGE_DEFAULT;
    description.BindFlags = Diligent::BIND_INDEX_BUFFER;
    description.Size = size;
    return CreateBuffer(device, description, nullptr);
}

uint8_t PackAlpha(float alpha) { return static_cast<uint8_t>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)); }

uint32_t PackRenderable(const RenderableComponent& renderable) {
    constexpr uint32_t translucentFlag = 4u;
    constexpr uint32_t meshMask = 0xFFFu;
    constexpr uint32_t materialMask = 0x3FFu;
    constexpr uint32_t shaderMask = 0xFu;
    constexpr uint32_t materialShift = 12u;
    constexpr uint32_t shaderShift = 22u;
    constexpr uint32_t flagsShift = 26u;
    const uint32_t mesh = std::min(renderable.mesh_uuid, meshMask);
    const uint32_t material = std::min(renderable.material_uuid, materialMask);
    const uint32_t shader = std::min(renderable.shaderId, shaderMask);
    const uint32_t flags = (renderable.flags & (RENDER_HIDDEN | RENDER_NO_OCCLUDER | RENDER_MASKED)) | (renderable.alpha < 1.0f ? translucentFlag : 0u);
    return mesh | material << materialShift | shader << shaderShift | flags << flagsShift;
}

Diligent::Uint32 SelectFastestAdapter(Diligent::IEngineFactory* factory, Diligent::Version minVersion) {
    Diligent::Uint32 adapterCount = 0;
    factory->EnumerateAdapters(minVersion, adapterCount, nullptr);
    std::vector<Diligent::GraphicsAdapterInfo> adapters(adapterCount);
    factory->EnumerateAdapters(minVersion, adapterCount, adapters.data());

    auto rank = [](const Diligent::GraphicsAdapterInfo& adapter) {
        switch (adapter.Type) {
            case Diligent::ADAPTER_TYPE_DISCRETE:
                return 3;
            case Diligent::ADAPTER_TYPE_INTEGRATED:
                return 2;
            case Diligent::ADAPTER_TYPE_SOFTWARE:
                return 0;
            default:
                return 1;
        }
    };
    const auto best = std::ranges::max_element(adapters, {}, rank);
    if (best == adapters.end()) return Diligent::DEFAULT_ADAPTER_ID;
    Lit::Log::Info("Selected GPU: {}", best->Description);
    return static_cast<Diligent::Uint32>(best - adapters.begin());
}

void SetDynamicVariable(Diligent::IShaderResourceBinding* binding, const char* name, Diligent::IDeviceObject* object, Diligent::SHADER_TYPE stage) {
    if (!binding) return;
    if (auto* variable = binding->GetVariableByName(stage, name)) variable->Set(object);
}

void CreateBoundSRB(Diligent::IPipelineState* pipeline, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& binding, std::initializer_list<NamedBuffer> buffers) {
    binding.Release();
    if (!pipeline) return;
    pipeline->CreateShaderResourceBinding(&binding, true);
    BindBuffers(binding, buffers);
}

void CreateBindings(Diligent::IPipelineState* pipeline, std::span<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> bindings) {
    for (auto& binding : bindings) {
        binding.Release();
        if (pipeline) pipeline->CreateShaderResourceBinding(&binding, true);
    }
}

Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::IRenderDevice* device, const std::string& path, Diligent::SHADER_TYPE type, const Diligent::ShaderMacroArray& macros) {
    std::string source = LoadSourceFromFile(path);
    if (source.empty()) return {};

    const size_t versionPosition = source.find("#version");
    if (versionPosition != std::string::npos) {
        const size_t nextLine = source.find('\n', versionPosition);
        if (nextLine != std::string::npos) source.erase(0, nextLine + 1);
    }

    static Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> includeFactory = [] {
        Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> factory;
        Diligent::GetEngineFactoryVk()->CreateDefaultShaderSourceStreamFactory("resources/shaders/include", &factory);
        return factory;
    }();

    Diligent::ShaderCreateInfo createInfo;
    createInfo.Source = source.c_str();
    createInfo.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    createInfo.pShaderSourceStreamFactory = includeFactory;
    createInfo.Desc.UseCombinedTextureSamplers = true;
    createInfo.Desc.ShaderType = type;
    createInfo.Desc.Name = path.c_str();
    createInfo.Macros = macros;

    Diligent::RefCntAutoPtr<Diligent::IShader> shader = litgpu::createShader(device, createInfo);
    if (!shader) Lit::Log::Error("Failed to compile shader '{}'.", path);
    return shader;
}

std::vector<litgpu::ShaderJob> DefaultShaderJobs() {
    const char* computeShaders[] = {"transform.comp", "apply_dirty.comp", "prefix_sum.comp", "scatter.comp", "command_gen.comp", "dispatch_args.comp", "hiz_from_depth.comp", "hiz_mipmap.comp"};
    const std::pair<const char*, Diligent::SHADER_TYPE> graphicsShaders[] = {
        {"fullscreen.vert",            Diligent::SHADER_TYPE_VERTEX},
        {"transparent_composite.frag", Diligent::SHADER_TYPE_PIXEL },
        {"debug_depth.vert",           Diligent::SHADER_TYPE_VERTEX},
        {"debug_depth.frag",           Diligent::SHADER_TYPE_PIXEL },
        {"debug_line.vert",            Diligent::SHADER_TYPE_VERTEX},
        {"debug_line.frag",            Diligent::SHADER_TYPE_PIXEL }
    };
    std::vector<litgpu::ShaderJob> jobs;
    for (const char* name : computeShaders) jobs.push_back({std::string("resources/shaders/") + name, Diligent::SHADER_TYPE_COMPUTE, {}});
    for (const auto& [name, type] : graphicsShaders) jobs.push_back({std::string("resources/shaders/") + name, type, {}});
    for (const char* macro : {"", "RETEST", "TRANSPARENT", "SHADOW"}) {
        litgpu::ShaderMacros macros;
        if (*macro) macros.push_back({macro, "1"});
        jobs.push_back({"resources/shaders/cull.comp", Diligent::SHADER_TYPE_COMPUTE, macros});
    }
    std::error_code errorCode;
    for (const char* folder : {"resources/shaders/lighting", "resources/shaders/post"}) {
        for (const auto& entry : std::filesystem::directory_iterator(folder, errorCode)) {
            const std::string extension = entry.path().extension().string();
            if (extension == ".comp") jobs.push_back({entry.path().generic_string(), Diligent::SHADER_TYPE_COMPUTE, {}});
            if (extension == ".frag") jobs.push_back({entry.path().generic_string(), Diligent::SHADER_TYPE_PIXEL, {}});
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const litgpu::ShaderJob& left, const litgpu::ShaderJob& right) { return left.path < right.path; });
    return jobs;
}

bool AllShadersCompile(Diligent::IRenderDevice* device) { return litgpu::compileJobs(device, DefaultShaderJobs(), false); }

void PrefetchShaders(Diligent::IRenderDevice* device) { litgpu::compileJobs(device, DefaultShaderJobs(), true); }

std::vector<Diligent::ShaderResourceVariableDesc> DescribeVariables(std::initializer_list<Diligent::IShader*> shaders, std::initializer_list<std::string_view> dynamicVariables) {
    std::vector<Diligent::ShaderResourceVariableDesc> variables;
    for (Diligent::IShader* shader : shaders) {
        const Diligent::SHADER_TYPE stage = shader->GetDesc().ShaderType;
        for (Diligent::Uint32 resourceIndex = 0; resourceIndex < shader->GetResourceCount(); ++resourceIndex) {
            Diligent::ShaderResourceDesc resource;
            shader->GetResourceDesc(resourceIndex, resource);
            const bool isDynamic = std::ranges::find(dynamicVariables, std::string_view(resource.Name)) != dynamicVariables.end();
            const bool isStorageBuffer = resource.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV || resource.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV;
            variables.push_back({stage, resource.Name, isDynamic ? Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC : Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE, isStorageBuffer ? Diligent::SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS : Diligent::SHADER_VARIABLE_FLAG_NONE});
        }
    }
    return variables;
}

void UseVariables(Diligent::PipelineResourceLayoutDesc& layout, const std::vector<Diligent::ShaderResourceVariableDesc>& variables) {
    layout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    layout.Variables = variables.data();
    layout.NumVariables = static_cast<Diligent::Uint32>(variables.size());
}

Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateComputePipeline(Diligent::IRenderDevice* device, const char* name, const std::string& path, std::initializer_list<std::string_view> dynamicVariables, const Diligent::ShaderMacroArray& macros) {
    Diligent::RefCntAutoPtr<Diligent::IShader> shader = CompileShader(device, path, Diligent::SHADER_TYPE_COMPUTE, macros);
    if (!shader) return {};

    const auto variables = DescribeVariables({shader}, dynamicVariables);
    Diligent::ComputePipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = name;
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    createInfo.pCS = shader;
    UseVariables(createInfo.PSODesc.ResourceLayout, variables);

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
    device->CreateComputePipelineState(createInfo, &pipeline);
    if (!pipeline) Lit::Log::Error("Failed to create pipeline '{}'.", name);
    return pipeline;
}

void ClearCounter(Diligent::IDeviceContext* context, Diligent::IBuffer* buffer) {
    const unsigned int zero = 0;
    context->UpdateBuffer(buffer, 0, sizeof(zero), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void UnbindRenderTargets(Diligent::IDeviceContext* context) { context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE); }

void SetSwapChainViewport(DiligentData& gpu) {
    const auto& swapChainDesc = gpu.pSwapChain->GetDesc();
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(swapChainDesc.Width);
    viewport.Height = static_cast<float>(swapChainDesc.Height);
    gpu.pImmediateContext->SetViewports(1, &viewport, swapChainDesc.Width, swapChainDesc.Height);
}

void SetSceneViewport(DiligentData& gpu) {
    const auto& swapChainDesc = gpu.pSwapChain->GetDesc();
    const Diligent::Uint32 width = gpu.renderWidth > 0 ? gpu.renderWidth : swapChainDesc.Width;
    const Diligent::Uint32 height = gpu.renderHeight > 0 ? gpu.renderHeight : swapChainDesc.Height;
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    gpu.pImmediateContext->SetViewports(1, &viewport, width, height);
}

void DispatchSingleGroup(Diligent::IDeviceContext* context, Diligent::IPipelineState* pipeline, Diligent::IShaderResourceBinding* binding) {
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));
}

void DrawFullscreenTriangle(Diligent::IDeviceContext* context) { context->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_NONE}); }

Diligent::IPipelineState* SurfacePipeline(const DiligentData::SurfaceProgram& surface, int phaseIndex, bool points) {
    switch (phaseIndex) {
        case kPhaseTransparent:
            return points ? surface.forwardPoint.RawPtr() : surface.forward.RawPtr();
        case kPhaseShadow:
            return points ? surface.shadowPoint.RawPtr() : surface.shadow.RawPtr();
        default:
            return points ? surface.gbufferPoint.RawPtr() : surface.gbuffer.RawPtr();
    }
}

std::vector<Diligent::IPipelineState*> SurfacePipelines(const DiligentData& gpu, int phaseIndex, bool points) {
    std::vector<Diligent::IPipelineState*> pipelines;
    pipelines.reserve(gpu.surfaces.size());
    for (const auto& surface : gpu.surfaces) pipelines.push_back(SurfacePipeline(surface, phaseIndex, points));
    return pipelines;
}
