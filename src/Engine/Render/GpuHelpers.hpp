#pragma once

#include "DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Shader.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Sampler.h"
#include "DiligentCore/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "DiligentCore/Primitives/interface/DataBlob.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Log/Log.hpp"

namespace litgpu {

using Diligent::RefCntAutoPtr;

struct Named {
    std::string_view name;
    Diligent::IDeviceObject* object;
};

struct NamedSampler {
    const char* name;
    Diligent::SamplerDesc desc;
};

inline Diligent::SamplerDesc linearClamp() {
    Diligent::SamplerDesc d;
    d.MinFilter = d.MagFilter = d.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    d.AddressU = d.AddressV = d.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    return d;
}

inline Diligent::SamplerDesc linearWrap() {
    Diligent::SamplerDesc d;
    d.MinFilter = d.MagFilter = d.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    d.AddressU = d.AddressV = d.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
    return d;
}

inline Diligent::SamplerDesc anisotropicWrap() {
    Diligent::SamplerDesc d;
    d.MinFilter = d.MagFilter = d.MipFilter = Diligent::FILTER_TYPE_ANISOTROPIC;
    d.MaxAnisotropy = 8;
    d.AddressU = d.AddressV = d.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
    return d;
}

inline Diligent::SamplerDesc pointClamp() {
    Diligent::SamplerDesc d;
    d.MinFilter = d.MagFilter = d.MipFilter = Diligent::FILTER_TYPE_POINT;
    d.AddressU = d.AddressV = d.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    return d;
}

inline Diligent::SamplerDesc shadowCompare() {
    Diligent::SamplerDesc d;
    d.MinFilter = d.MagFilter = Diligent::FILTER_TYPE_COMPARISON_LINEAR;
    d.MipFilter = Diligent::FILTER_TYPE_POINT;
    d.AddressU = d.AddressV = d.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    d.ComparisonFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    return d;
}

inline std::string readText(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline Diligent::IShaderSourceInputStreamFactory* includeFactory() {
    static RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> factory = [] {
        RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> f;
        Diligent::GetEngineFactoryVk()->CreateDefaultShaderSourceStreamFactory("resources/shaders/include;resources/shaders", &f);
        return f;
    }();
    return factory;
}

inline uint64_t fnv1a(std::string_view data, uint64_t hash = 1469598103934665603ull) {
    for (unsigned char c : data) hash = (hash ^ c) * 1099511628211ull;
    return hash;
}

inline void hashWithIncludes(std::string_view source, uint64_t& hash, std::vector<std::string>& seen) {
    hash = fnv1a(source, hash);
    size_t pos = 0;
    while ((pos = source.find("#include", pos)) != std::string_view::npos) {
        pos += 8;
        const size_t open = source.find_first_of("\"<", pos);
        const size_t close = open == std::string_view::npos ? open : source.find_first_of("\">", open + 1);
        if (close == std::string_view::npos) break;
        std::string name(source.substr(open + 1, close - open - 1));
        if (std::find(seen.begin(), seen.end(), name) != seen.end()) continue;
        seen.push_back(name);
        for (const char* dir : {"resources/shaders/include/", "resources/shaders/"}) {
            const std::string text = readText(dir + name);
            if (text.empty()) continue;
            hashWithIncludes(text, hash, seen);
            break;
        }
    }
}

inline constexpr const char* kShaderCacheDir = "resources/cache/shaders";

struct ShaderDiagnostic {
    std::string source;
    std::string message;
};

inline std::mutex& diagnosticsMutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::vector<ShaderDiagnostic>& diagnosticsQueue() {
    static std::vector<ShaderDiagnostic> queue;
    return queue;
}

inline void reportShaderError(std::string source, std::string message) {
    std::lock_guard lock(diagnosticsMutex());
    auto& queue = diagnosticsQueue();
    for (const ShaderDiagnostic& existing : queue) {
        if (existing.source == source && existing.message == message) return;
    }
    if (queue.size() >= 32) queue.erase(queue.begin());
    queue.push_back({std::move(source), std::move(message)});
}

inline std::vector<ShaderDiagnostic> takeShaderDiagnostics() {
    std::lock_guard lock(diagnosticsMutex());
    std::vector<ShaderDiagnostic> out;
    out.swap(diagnosticsQueue());
    return out;
}

inline std::string& lastCompilerOutput() {
    thread_local std::string text;
    return text;
}

inline std::string& lastCompileErrors() {
    thread_local std::string text;
    return text;
}

inline constexpr int kUserSourceString = 1;

inline std::string userLine(int line) { return std::format("\n#line {} {}\n", line, kUserSourceString); }

inline std::string injectUserCode(const std::string& templ, const std::string& code) {
    const std::string marker = "#pragma lit_user_code";
    const size_t pos = templ.find(marker);
    if (pos == std::string::npos) return templ;
    const auto line = std::count(templ.begin(), templ.begin() + static_cast<std::ptrdiff_t>(pos), '\n') + 1;
    return templ.substr(0, pos) + code + std::format("\n#line {} 0\n", line) + templ.substr(pos + marker.size());
}

inline std::string compilerErrors(const std::string& output, const std::string& name, const std::string& userPath) {
    std::string errors;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("ERROR:", 0) != 0 || line.find("compilation errors") != std::string::npos || line.find("compilation terminated") != std::string::npos) continue;
        std::string text = line.substr(6);
        while (!text.empty() && text.front() == ' ') text.erase(text.begin());
        const std::string userTag = std::format("{}:", kUserSourceString);
        if (!userPath.empty() && text.rfind(userTag, 0) == 0) {
            text = userPath + ":" + text.substr(userTag.size());
        } else if (text.rfind("0:", 0) == 0) {
            text = name + ":" + text.substr(2);
        }
        errors += (errors.empty() ? "" : "\n") + text;
    }
    return errors;
}

inline RefCntAutoPtr<Diligent::IShader> createShader(Diligent::IRenderDevice* device, const Diligent::ShaderCreateInfo& ci) {
    RefCntAutoPtr<Diligent::IShader> shader;
    lastCompilerOutput().clear();
    auto compile = [&] {
        RefCntAutoPtr<Diligent::IDataBlob> output;
        device->CreateShader(ci, &shader, &output);
        if (!shader && output && output->GetSize() > 0) lastCompilerOutput() = static_cast<const char*>(output->GetConstDataPtr());
    };
    if (!ci.Source || ci.ByteCode || std::getenv("LIT_NO_SHADER_CACHE")) {
        compile();
        return shader;
    }
    uint64_t hash = fnv1a(std::format("lit-spirv-1|{}|{}|{}|", static_cast<int>(ci.Desc.ShaderType), static_cast<int>(ci.SourceLanguage), ci.Desc.UseCombinedTextureSamplers ? 1 : 0));
    for (Diligent::Uint32 i = 0; i < ci.Macros.Count; ++i) hash = fnv1a(std::format("{}={};", ci.Macros[i].Name, ci.Macros[i].Definition ? ci.Macros[i].Definition : ""), hash);
    std::vector<std::string> seen;
    hashWithIncludes(ci.Source, hash, seen);
    const std::filesystem::path path = std::filesystem::path(kShaderCacheDir) / std::format("{:016x}.spv", hash);

    std::ifstream in(path, std::ios::binary);
    if (in) {
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!bytes.empty() && bytes.size() % 4 == 0) {
            Diligent::ShaderCreateInfo cached;
            cached.Desc = ci.Desc;
            cached.ByteCode = bytes.data();
            cached.ByteCodeSize = bytes.size();
            device->CreateShader(cached, &shader);
            if (shader) return shader;
        }
    }

    compile();
    if (!shader) return shader;
    const void* bytecode = nullptr;
    Diligent::Uint64 size = 0;
    shader->GetBytecode(&bytecode, size);
    if (!bytecode || size == 0) return shader;
    std::error_code ec;
    std::filesystem::create_directories(kShaderCacheDir, ec);
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(bytecode), static_cast<std::streamsize>(size));
        if (!out) return shader;
    }
    std::filesystem::rename(temp, path, ec);
    return shader;
}

inline std::string stripVersion(std::string source) {
    const size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        const size_t nextLine = source.find('\n', versionPos);
        if (nextLine != std::string::npos) source.erase(versionPos, nextLine - versionPos);
    }
    return source;
}

inline RefCntAutoPtr<Diligent::IShader> compileSource(Diligent::IRenderDevice* device, const std::string& name, const std::string& source, Diligent::SHADER_TYPE type, const std::vector<std::pair<std::string, std::string>>& macros = {},
                                                     const std::string& userPath = {}) {
    const std::string body = "#line 1 0\n" + stripVersion(source);
    std::vector<Diligent::ShaderMacro> list;
    list.reserve(macros.size());
    for (const auto& [key, value] : macros) list.push_back({key.c_str(), value.c_str()});
    Diligent::ShaderCreateInfo ci;
    ci.Source = body.c_str();
    ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ci.pShaderSourceStreamFactory = includeFactory();
    ci.Desc.UseCombinedTextureSamplers = true;
    ci.Desc.ShaderType = type;
    ci.Desc.Name = name.c_str();
    ci.Macros = Diligent::ShaderMacroArray{list.data(), static_cast<Diligent::Uint32>(list.size())};
    RefCntAutoPtr<Diligent::IShader> shader = createShader(device, ci);
    if (!shader) {
        std::string errors = compilerErrors(lastCompilerOutput(), name, userPath);
        if (errors.empty()) errors = "failed to compile (see the log)";
        lastCompileErrors() = errors;
        Lit::Log::Error("Failed to compile shader '{}':\n{}", name, errors);
        reportShaderError(userPath.empty() ? name : userPath, std::move(errors));
    }
    return shader;
}

inline RefCntAutoPtr<Diligent::IShader> compileFile(Diligent::IRenderDevice* device, const std::string& path, Diligent::SHADER_TYPE type, const std::vector<std::pair<std::string, std::string>>& macros = {}) {
    const std::string source = readText(path);
    if (source.empty()) {
        Lit::Log::Error("Failed to open shader file: {}", path);
        return {};
    }
    return compileSource(device, path, source, type, macros);
}

inline std::vector<Diligent::ShaderResourceVariableDesc> describeVariables(std::initializer_list<Diligent::IShader*> shaders, std::initializer_list<std::string_view> dynamicVariables = {}) {
    std::vector<Diligent::ShaderResourceVariableDesc> variables;
    for (Diligent::IShader* shader : shaders) {
        if (!shader) continue;
        const Diligent::SHADER_TYPE stage = shader->GetDesc().ShaderType;
        for (Diligent::Uint32 i = 0; i < shader->GetResourceCount(); ++i) {
            Diligent::ShaderResourceDesc resource;
            shader->GetResourceDesc(i, resource);
            const bool isDynamic = std::find(dynamicVariables.begin(), dynamicVariables.end(), std::string_view(resource.Name)) != dynamicVariables.end();
            const bool isStorageBuffer = resource.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV || resource.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV;
            variables.push_back({stage, resource.Name, isDynamic ? Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC : Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
                                 isStorageBuffer ? Diligent::SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS : Diligent::SHADER_VARIABLE_FLAG_NONE});
        }
    }
    return variables;
}

struct PipelineLayout {
    std::vector<Diligent::ShaderResourceVariableDesc> variables;
    std::vector<Diligent::ImmutableSamplerDesc> samplers;

    void apply(Diligent::PipelineResourceLayoutDesc& layout) const {
        layout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        layout.Variables = variables.data();
        layout.NumVariables = static_cast<Diligent::Uint32>(variables.size());
        layout.ImmutableSamplers = samplers.data();
        layout.NumImmutableSamplers = static_cast<Diligent::Uint32>(samplers.size());
    }
};

inline PipelineLayout makeLayout(std::initializer_list<Diligent::IShader*> shaders, std::initializer_list<NamedSampler> samplers = {}, std::initializer_list<std::string_view> dynamicVariables = {}) {
    PipelineLayout layout;
    layout.variables = describeVariables(shaders, dynamicVariables);
    for (const NamedSampler& s : samplers) layout.samplers.push_back({Diligent::SHADER_TYPE_ALL_GRAPHICS | Diligent::SHADER_TYPE_COMPUTE, s.name, s.desc});
    return layout;
}

inline RefCntAutoPtr<Diligent::IPipelineState> computePipeline(Diligent::IRenderDevice* device, const std::string& path, std::initializer_list<NamedSampler> samplers = {}, const std::vector<std::pair<std::string, std::string>>& macros = {},
                                                              std::initializer_list<std::string_view> dynamicVariables = {}) {
    RefCntAutoPtr<Diligent::IShader> cs = compileFile(device, path, Diligent::SHADER_TYPE_COMPUTE, macros);
    if (!cs) return {};
    const PipelineLayout layout = makeLayout({cs}, samplers, dynamicVariables);
    Diligent::ComputePipelineStateCreateInfo ci;
    ci.PSODesc.Name = path.c_str();
    ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    ci.pCS = cs;
    layout.apply(ci.PSODesc.ResourceLayout);
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateComputePipelineState(ci, &pso);
    if (!pso) Lit::Log::Error("Failed to create compute pipeline '{}'", path);
    return pso;
}

inline RefCntAutoPtr<Diligent::IPipelineState> fullscreenPipeline(Diligent::IRenderDevice* device, const std::string& name, const std::string& pixelPath, Diligent::TEXTURE_FORMAT colorFormat, std::initializer_list<NamedSampler> samplers = {},
                                                                 const std::vector<std::pair<std::string, std::string>>& macros = {}, bool alphaBlend = false) {
    RefCntAutoPtr<Diligent::IShader> vs = compileFile(device, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
    RefCntAutoPtr<Diligent::IShader> ps = compileFile(device, pixelPath, Diligent::SHADER_TYPE_PIXEL, macros);
    if (!vs || !ps) return {};
    Diligent::GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = name.c_str();
    ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    ci.GraphicsPipeline.NumRenderTargets = 1;
    ci.GraphicsPipeline.RTVFormats[0] = colorFormat;
    ci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    if (alphaBlend) {
        auto& blend = ci.GraphicsPipeline.BlendDesc.RenderTargets[0];
        blend.BlendEnable = true;
        blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
        blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    }
    ci.pVS = vs;
    ci.pPS = ps;
    const PipelineLayout layout = makeLayout({vs, ps}, samplers);
    layout.apply(ci.PSODesc.ResourceLayout);
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateGraphicsPipelineState(ci, &pso);
    if (!pso) Lit::Log::Error("Failed to create pipeline '{}'", name);
    return pso;
}

inline Diligent::IDeviceObject* bufferView(Diligent::IBuffer* buffer, Diligent::SHADER_RESOURCE_TYPE type) {
    if (!buffer) return nullptr;
    switch (type) {
        case Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER: return buffer;
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV: return buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV: return buffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
        default: return nullptr;
    }
}

template <typename Range>
inline bool bindRange(Diligent::IShaderResourceBinding* srb, const Range& resources, bool warn) {
    if (!srb) return false;
    bool complete = true;
    const auto& signature = srb->GetPipelineResourceSignature()->GetDesc();
    for (Diligent::Uint32 r = 0; r < signature.NumResources; ++r) {
        const Diligent::PipelineResourceDesc& resource = signature.Resources[r];
        if (resource.VarType == Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC) continue;
        const auto match = std::find_if(resources.begin(), resources.end(), [&](const Named& n) { return n.name == resource.Name; });
        if (match == resources.end() || !match->object) {
            if (warn) Lit::Log::Error("No resource bound to '{}' in '{}'", resource.Name, signature.Name);
            complete = false;
            continue;
        }
        Diligent::IDeviceObject* object = match->object;
        if (auto* buffer = Diligent::RefCntAutoPtr<Diligent::IBuffer>(object, Diligent::IID_Buffer).RawPtr()) object = bufferView(buffer, resource.ResourceType);
        else if (auto* texture = Diligent::RefCntAutoPtr<Diligent::ITexture>(object, Diligent::IID_Texture).RawPtr()) object = texture->GetDefaultView(resource.ResourceType == Diligent::SHADER_RESOURCE_TYPE_TEXTURE_UAV ? Diligent::TEXTURE_VIEW_UNORDERED_ACCESS : Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        if (!object) {
            complete = false;
            continue;
        }
        for (Diligent::Uint32 stages = resource.ShaderStages; stages != 0; stages &= stages - 1) {
            const auto stage = static_cast<Diligent::SHADER_TYPE>(stages & ~(stages - 1));
            if (auto* variable = srb->GetVariableByName(stage, resource.Name)) variable->Set(object, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
    }
    return complete;
}

inline bool bind(Diligent::IShaderResourceBinding* srb, std::initializer_list<Named> resources, bool warn = true) { return bindRange(srb, resources, warn); }
inline bool bindList(Diligent::IShaderResourceBinding* srb, const std::vector<Named>& resources, bool warn = true) { return bindRange(srb, resources, warn); }

inline RefCntAutoPtr<Diligent::IShaderResourceBinding> makeBinding(Diligent::IPipelineState* pso, std::initializer_list<Named> resources, bool warn = true) {
    RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
    if (!pso) return srb;
    pso->CreateShaderResourceBinding(&srb, true);
    bind(srb, resources, warn);
    return srb;
}

inline RefCntAutoPtr<Diligent::IBuffer> structuredBuffer(Diligent::IRenderDevice* device, const char* name, Diligent::Uint32 stride, Diligent::Uint32 count, const void* init = nullptr, Diligent::BIND_FLAGS extraFlags = Diligent::BIND_NONE) {
    Diligent::BufferDesc desc;
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS | extraFlags;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = stride;
    desc.Size = static_cast<Diligent::Uint64>(stride) * std::max<Diligent::Uint32>(count, 1);
    Diligent::BufferData data;
    data.pData = init;
    data.DataSize = desc.Size;
    RefCntAutoPtr<Diligent::IBuffer> buffer;
    device->CreateBuffer(desc, init ? &data : nullptr, &buffer);
    if (!buffer) Lit::Log::Error("Failed to allocate buffer '{}'", name);
    return buffer;
}

inline RefCntAutoPtr<Diligent::IBuffer> uniformBuffer(Diligent::IRenderDevice* device, const char* name, Diligent::Uint64 size) {
    Diligent::BufferDesc desc;
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    desc.Size = (size + 15) & ~Diligent::Uint64{15};
    RefCntAutoPtr<Diligent::IBuffer> buffer;
    device->CreateBuffer(desc, nullptr, &buffer);
    return buffer;
}

struct TextureSpec {
    const char* name = "Texture";
    Diligent::RESOURCE_DIMENSION dim = Diligent::RESOURCE_DIM_TEX_2D;
    Diligent::Uint32 width = 1;
    Diligent::Uint32 height = 1;
    Diligent::Uint32 depthOrSlices = 1;
    Diligent::Uint32 mips = 1;
    Diligent::TEXTURE_FORMAT format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    Diligent::BIND_FLAGS bind = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
};

inline RefCntAutoPtr<Diligent::ITexture> texture(Diligent::IRenderDevice* device, const TextureSpec& spec) {
    Diligent::TextureDesc desc;
    desc.Name = spec.name;
    desc.Type = spec.dim;
    desc.Width = std::max<Diligent::Uint32>(spec.width, 1);
    desc.Height = std::max<Diligent::Uint32>(spec.height, 1);
    if (spec.dim == Diligent::RESOURCE_DIM_TEX_3D) {
        desc.Depth = spec.depthOrSlices;
    } else {
        desc.ArraySize = spec.depthOrSlices;
    }
    desc.MipLevels = spec.mips;
    desc.Format = spec.format;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = spec.bind;
    RefCntAutoPtr<Diligent::ITexture> tex;
    device->CreateTexture(desc, nullptr, &tex);
    if (!tex) Lit::Log::Error("Failed to create texture '{}'", spec.name);
    return tex;
}

inline RefCntAutoPtr<Diligent::ITextureView> mipView(Diligent::ITexture* tex, Diligent::TEXTURE_VIEW_TYPE type, Diligent::Uint32 mip, Diligent::RESOURCE_DIMENSION dim = Diligent::RESOURCE_DIM_UNDEFINED, Diligent::Uint32 firstSlice = 0, Diligent::Uint32 slices = 0, Diligent::Uint32 mipCount = 1) {
    RefCntAutoPtr<Diligent::ITextureView> view;
    if (!tex) return view;
    Diligent::TextureViewDesc desc;
    desc.ViewType = type;
    desc.TextureDim = dim;
    desc.MostDetailedMip = mip;
    desc.NumMipLevels = mipCount;
    desc.FirstArraySlice = firstSlice;
    desc.NumArraySlices = slices;
    if (type == Diligent::TEXTURE_VIEW_UNORDERED_ACCESS) desc.AccessFlags = Diligent::UAV_ACCESS_FLAG_READ_WRITE;
    tex->CreateView(desc, &view);
    return view;
}

inline Diligent::ITextureView* srv(Diligent::ITexture* tex) { return tex ? tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr; }
inline Diligent::ITextureView* uav(Diligent::ITexture* tex) { return tex ? tex->GetDefaultView(Diligent::TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr; }
inline Diligent::ITextureView* rtv(Diligent::ITexture* tex) { return tex ? tex->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET) : nullptr; }
inline Diligent::ITextureView* dsv(Diligent::ITexture* tex) { return tex ? tex->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : nullptr; }

inline void dispatch(Diligent::IDeviceContext* context, Diligent::IPipelineState* pso, Diligent::IShaderResourceBinding* srb, Diligent::Uint32 x, Diligent::Uint32 y = 1, Diligent::Uint32 z = 1) {
    if (!pso || !srb) return;
    context->SetPipelineState(pso);
    context->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs(std::max<Diligent::Uint32>(x, 1), std::max<Diligent::Uint32>(y, 1), std::max<Diligent::Uint32>(z, 1)));
}

inline void setVar(Diligent::IShaderResourceBinding* srb, const char* name, Diligent::IDeviceObject* object) {
    if (!srb || !object) return;
    for (Diligent::SHADER_TYPE stage : {Diligent::SHADER_TYPE_COMPUTE, Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL}) {
        if (auto* variable = srb->GetVariableByName(stage, name)) variable->Set(object, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
}

inline void uavBarrier(Diligent::IDeviceContext* context, Diligent::IDeviceObject* resource) {
    Diligent::StateTransitionDesc barrier;
    barrier.pResource = resource;
    barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    context->TransitionResourceStates(1, &barrier);
}

}  // namespace litgpu
