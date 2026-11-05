#pragma once

#include "DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Sampler.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "GpuShader.hpp"

namespace litgpu {

inline constexpr Diligent::Uint8 kAnisotropicSamplerLevel = 8;
inline constexpr Diligent::Uint64 kUniformBufferAlignment = 16;

struct Named {
    std::string_view name;
    Diligent::IDeviceObject* object;
};

struct NamedSampler {
    const char* name;
    Diligent::SamplerDesc desc;
};

inline Diligent::SamplerDesc makeSampler(Diligent::FILTER_TYPE filter, Diligent::TEXTURE_ADDRESS_MODE address) {
    Diligent::SamplerDesc desc;
    desc.MinFilter = desc.MagFilter = desc.MipFilter = filter;
    desc.AddressU = desc.AddressV = desc.AddressW = address;
    return desc;
}

inline Diligent::SamplerDesc linearClamp() { return makeSampler(Diligent::FILTER_TYPE_LINEAR, Diligent::TEXTURE_ADDRESS_CLAMP); }

inline Diligent::SamplerDesc linearWrap() { return makeSampler(Diligent::FILTER_TYPE_LINEAR, Diligent::TEXTURE_ADDRESS_WRAP); }

inline Diligent::SamplerDesc anisotropicWrap() {
    Diligent::SamplerDesc desc = makeSampler(Diligent::FILTER_TYPE_ANISOTROPIC, Diligent::TEXTURE_ADDRESS_WRAP);
    desc.MaxAnisotropy = kAnisotropicSamplerLevel;
    return desc;
}

inline Diligent::SamplerDesc pointClamp() { return makeSampler(Diligent::FILTER_TYPE_POINT, Diligent::TEXTURE_ADDRESS_CLAMP); }

inline Diligent::SamplerDesc shadowCompare() {
    Diligent::SamplerDesc desc = makeSampler(Diligent::FILTER_TYPE_COMPARISON_LINEAR, Diligent::TEXTURE_ADDRESS_CLAMP);
    desc.MipFilter = Diligent::FILTER_TYPE_POINT;
    desc.ComparisonFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    return desc;
}

inline std::vector<Diligent::ShaderResourceVariableDesc> describeVariables(std::initializer_list<Diligent::IShader*> shaders, std::initializer_list<std::string_view> dynamicVariables = {}) {
    std::vector<Diligent::ShaderResourceVariableDesc> variables;
    for (Diligent::IShader* shader : shaders) {
        if (!shader) continue;
        const Diligent::SHADER_TYPE stage = shader->GetDesc().ShaderType;
        for (Diligent::Uint32 resourceIndex = 0; resourceIndex < shader->GetResourceCount(); ++resourceIndex) {
            Diligent::ShaderResourceDesc resource;
            shader->GetResourceDesc(resourceIndex, resource);
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
    for (const NamedSampler& sampler : samplers) layout.samplers.push_back({Diligent::SHADER_TYPE_ALL_GRAPHICS | Diligent::SHADER_TYPE_COMPUTE, sampler.name, sampler.desc});
    return layout;
}

inline RefCntAutoPtr<Diligent::IPipelineState> computePipeline(Diligent::IRenderDevice* device, const std::string& path, std::initializer_list<NamedSampler> samplers = {}, const ShaderMacros& macros = {},
                                                              std::initializer_list<std::string_view> dynamicVariables = {}) {
    RefCntAutoPtr<Diligent::IShader> computeShader = compileFile(device, path, Diligent::SHADER_TYPE_COMPUTE, macros);
    if (!computeShader) return {};
    const PipelineLayout layout = makeLayout({computeShader}, samplers, dynamicVariables);
    Diligent::ComputePipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = path.c_str();
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    createInfo.pCS = computeShader;
    layout.apply(createInfo.PSODesc.ResourceLayout);
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateComputePipelineState(createInfo, &pso);
    if (!pso) Lit::Log::Error("Failed to create compute pipeline '{}'", path);
    return pso;
}

inline RefCntAutoPtr<Diligent::IPipelineState> fullscreenPipeline(Diligent::IRenderDevice* device, const std::string& name, const std::string& pixelPath, Diligent::TEXTURE_FORMAT colorFormat, std::initializer_list<NamedSampler> samplers = {},
                                                                 const ShaderMacros& macros = {}, bool alphaBlend = false) {
    RefCntAutoPtr<Diligent::IShader> vertexShader = compileFile(device, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
    RefCntAutoPtr<Diligent::IShader> pixelShader = compileFile(device, pixelPath, Diligent::SHADER_TYPE_PIXEL, macros);
    if (!vertexShader || !pixelShader) return {};
    Diligent::GraphicsPipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = name.c_str();
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    createInfo.GraphicsPipeline.NumRenderTargets = 1;
    createInfo.GraphicsPipeline.RTVFormats[0] = colorFormat;
    createInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    createInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    if (alphaBlend) {
        auto& blend = createInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
        blend.BlendEnable = true;
        blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
        blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    }
    createInfo.pVS = vertexShader;
    createInfo.pPS = pixelShader;
    const PipelineLayout layout = makeLayout({vertexShader, pixelShader}, samplers);
    layout.apply(createInfo.PSODesc.ResourceLayout);
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateGraphicsPipelineState(createInfo, &pso);
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
    for (Diligent::Uint32 resourceIndex = 0; resourceIndex < signature.NumResources; ++resourceIndex) {
        const Diligent::PipelineResourceDesc& resource = signature.Resources[resourceIndex];
        if (resource.VarType == Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC) continue;
        const auto match = std::find_if(resources.begin(), resources.end(), [&](const Named& named) { return named.name == resource.Name; });
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
    litgpu::bind(srb.RawPtr(), resources, warn);
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
    desc.Size = (size + kUniformBufferAlignment - 1) & ~(kUniformBufferAlignment - 1);
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
    RefCntAutoPtr<Diligent::ITexture> textureObject;
    device->CreateTexture(desc, nullptr, &textureObject);
    if (!textureObject) Lit::Log::Error("Failed to create texture '{}'", spec.name);
    return textureObject;
}

inline RefCntAutoPtr<Diligent::ITextureView> mipView(Diligent::ITexture* textureObject, Diligent::TEXTURE_VIEW_TYPE type, Diligent::Uint32 mip, Diligent::RESOURCE_DIMENSION dim = Diligent::RESOURCE_DIM_UNDEFINED, Diligent::Uint32 firstSlice = 0, Diligent::Uint32 slices = 0, Diligent::Uint32 mipCount = 1) {
    RefCntAutoPtr<Diligent::ITextureView> view;
    if (!textureObject) return view;
    Diligent::TextureViewDesc desc;
    desc.ViewType = type;
    desc.TextureDim = dim;
    desc.MostDetailedMip = mip;
    desc.NumMipLevels = mipCount;
    desc.FirstArraySlice = firstSlice;
    desc.NumArraySlices = slices;
    if (type == Diligent::TEXTURE_VIEW_UNORDERED_ACCESS) desc.AccessFlags = Diligent::UAV_ACCESS_FLAG_READ_WRITE;
    textureObject->CreateView(desc, &view);
    return view;
}

inline Diligent::ITextureView* srv(Diligent::ITexture* textureObject) { return textureObject ? textureObject->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr; }
inline Diligent::ITextureView* uav(Diligent::ITexture* textureObject) { return textureObject ? textureObject->GetDefaultView(Diligent::TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr; }
inline Diligent::ITextureView* rtv(Diligent::ITexture* textureObject) { return textureObject ? textureObject->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET) : nullptr; }
inline Diligent::ITextureView* dsv(Diligent::ITexture* textureObject) { return textureObject ? textureObject->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : nullptr; }

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

}
