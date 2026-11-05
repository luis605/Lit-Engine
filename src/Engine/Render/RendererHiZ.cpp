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

constexpr float kFarDepth[4] = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr Diligent::Uint32 kHiZGroupSize = 8;

void CreateMipUavView(Diligent::ITexture* texture, Diligent::Uint32 mip, Diligent::UAV_ACCESS_FLAG access, Diligent::RefCntAutoPtr<Diligent::ITextureView>& view) {
    Diligent::TextureViewDesc description;
    description.ViewType = Diligent::TEXTURE_VIEW_UNORDERED_ACCESS;
    description.NumMipLevels = 1;
    description.AccessFlags = access;
    description.MostDetailedMip = mip;
    texture->CreateView(description, &view);
}

void CreateHiZMipViews(Diligent::ITexture* texture, int mips, Diligent::RefCntAutoPtr<Diligent::ITextureView>* source, Diligent::RefCntAutoPtr<Diligent::ITextureView>* dest) {
    for (int mip = 0; mip < mips && mip < kMaxHiZMips; ++mip) {
        CreateMipUavView(texture, static_cast<Diligent::Uint32>(mip), Diligent::UAV_ACCESS_FLAG_WRITE, dest[mip]);
        if (mip == 0) continue;
        CreateMipUavView(texture, static_cast<Diligent::Uint32>(mip - 1), Diligent::UAV_ACCESS_FLAG_READ, source[mip]);
    }
}

Diligent::TextureDesc DescribeHiZ(const char* name, int width, int height, int mips) {
    Diligent::TextureDesc description;
    description.Name = name;
    description.Type = Diligent::RESOURCE_DIM_TEX_2D;
    description.Width = static_cast<Diligent::Uint32>(width);
    description.Height = static_cast<Diligent::Uint32>(height);
    description.Format = Diligent::TEX_FORMAT_R32_FLOAT;
    description.Usage = Diligent::USAGE_DEFAULT;
    description.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_RENDER_TARGET;
    description.MipLevels = static_cast<Diligent::Uint32>(mips);
    return description;
}

void ClearToFarDepth(DiligentData& gpu, Diligent::ITexture* texture) {
    gpu.pImmediateContext->ClearRenderTarget(texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET), kFarDepth, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
}

void CreateColorTarget(DiligentData& gpu, const char* name, Diligent::TEXTURE_FORMAT format, int width, int height, Diligent::RefCntAutoPtr<Diligent::ITexture>& texture) {
    Diligent::TextureDesc description;
    description.Name = name;
    description.Type = Diligent::RESOURCE_DIM_TEX_2D;
    description.Width = width;
    description.Height = height;
    description.Format = format;
    description.Usage = Diligent::USAGE_DEFAULT;
    description.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    texture.Release();
    gpu.pDevice->CreateTexture(description, nullptr, &texture);
    if (texture) texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
}

}

bool CreateHiZSet(DiligentData& gpu, HiZSet& set, int width, int height, int mips) {
    for (auto& view : set.source) view.Release();
    for (auto& view : set.dest) view.Release();
    set.texture.Release();
    gpu.pDevice->CreateTexture(DescribeHiZ("Portal Hi-Z", width, height, mips), nullptr, &set.texture);
    if (!set.texture) return false;
    ClearToFarDepth(gpu, set.texture);
    CreateHiZMipViews(set.texture, mips, set.source, set.dest);
    return true;
}

void BuildHiZ(DiligentData& gpu, Diligent::ITexture* hiz, Diligent::RefCntAutoPtr<Diligent::ITextureView>* source, Diligent::RefCntAutoPtr<Diligent::ITextureView>* dest, int width, int height, int mips) {
    auto* context = gpu.pImmediateContext.RawPtr();
    if (!gpu.pHiZFromDepthPSO || !gpu.pHiZMipmapPSO || !hiz) return;
    auto hizBarrier = [&](Diligent::RESOURCE_STATE newState) {
        Diligent::StateTransitionDesc barrier{hiz, Diligent::RESOURCE_STATE_UNKNOWN, newState, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE};
        context->TransitionResourceStates(1, &barrier);
    };
    const auto groupCount = [](int extent) { return (static_cast<Diligent::Uint32>(std::max(1, extent)) + kHiZGroupSize - 1) / kHiZGroupSize; };
    SetDynamicVariable(gpu.pHiZFromDepthSRB, "u_depth", gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    SetDynamicVariable(gpu.pHiZFromDepthSRB, "u_destMip", dest[0]);
    context->SetPipelineState(gpu.pHiZFromDepthPSO);
    context->CommitShaderResources(gpu.pHiZFromDepthSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs(groupCount(width), groupCount(height), 1));
    hizBarrier(Diligent::RESOURCE_STATE_UNORDERED_ACCESS);
    context->SetPipelineState(gpu.pHiZMipmapPSO);
    for (int mip = 1; mip < mips && mip < kMaxHiZMips; ++mip) {
        SetDynamicVariable(gpu.pHiZMipmapSRB, "u_sourceMip", source[mip]);
        SetDynamicVariable(gpu.pHiZMipmapSRB, "u_destMip", dest[mip]);
        context->CommitShaderResources(gpu.pHiZMipmapSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute(Diligent::DispatchComputeAttribs(groupCount(width >> mip), groupCount(height >> mip), 1));
        hizBarrier(Diligent::RESOURCE_STATE_UNORDERED_ACCESS);
    }
    hizBarrier(Diligent::RESOURCE_STATE_SHADER_RESOURCE);
}

void Renderer::createScreenTargets() {
    auto& gpu = *m_diligent;
    m_maxMipLevel = static_cast<int>(std::floor(std::log2(std::max(m_renderWidth, m_renderHeight))));

    Diligent::TextureDesc depthDesc;
    depthDesc.Name = "Scene Depth";
    depthDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    depthDesc.Width = m_renderWidth;
    depthDesc.Height = m_renderHeight;
    depthDesc.Format = kSceneDepthFormat;
    depthDesc.Usage = Diligent::USAGE_DEFAULT;
    depthDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
    gpu.pSceneDepth.Release();
    gpu.pDevice->CreateTexture(depthDesc, nullptr, &gpu.pSceneDepth);
    if (gpu.pSceneDepth) gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);

    gpu.renderWidth = static_cast<Diligent::Uint32>(m_renderWidth);
    gpu.renderHeight = static_cast<Diligent::Uint32>(m_renderHeight);
    gpu.pOverlayDepth.Release();
    if (m_renderWidth != m_windowWidth || m_renderHeight != m_windowHeight) {
        Diligent::TextureDesc overlayDesc = depthDesc;
        overlayDesc.Name = "Overlay Depth";
        overlayDesc.Width = static_cast<Diligent::Uint32>(m_windowWidth);
        overlayDesc.Height = static_cast<Diligent::Uint32>(m_windowHeight);
        overlayDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL;
        gpu.pDevice->CreateTexture(overlayDesc, nullptr, &gpu.pOverlayDepth);
    }

    CreateColorTarget(gpu, "Transparency Accumulation", kAccumulationFormat, m_renderWidth, m_renderHeight, gpu.pAccumulation);
    CreateColorTarget(gpu, "Transparency Revealage", kRevealageFormat, m_renderWidth, m_renderHeight, gpu.pRevealage);

    gpu.portalHistory.clear();
    for (auto& color : gpu.portalColor) color.Release();
    for (auto& depth : gpu.portalDepth) depth.Release();
    gpu.portalDepthScratch.Release();
    for (int frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) {
        for (auto& view : gpu.pHiZMipViewsSource[frameIndex]) view.Release();
        for (auto& view : gpu.pHiZMipViewsDest[frameIndex]) view.Release();

        gpu.pHiZTextures[frameIndex].Release();
        gpu.pDevice->CreateTexture(DescribeHiZ("Hi-Z Texture", m_renderWidth, m_renderHeight, m_maxMipLevel), nullptr, &gpu.pHiZTextures[frameIndex]);
        if (!gpu.pHiZTextures[frameIndex]) continue;

        ClearToFarDepth(gpu, gpu.pHiZTextures[frameIndex]);
        CreateHiZMipViews(gpu.pHiZTextures[frameIndex], m_maxMipLevel, gpu.pHiZMipViewsSource[frameIndex], gpu.pHiZMipViewsDest[frameIndex]);
    }
}

void Renderer::buildHiZ() {
    auto& gpu = *m_diligent;
    BuildHiZ(gpu, gpu.pHiZTextures[m_currentFrame].RawPtr(), gpu.pHiZMipViewsSource[m_currentFrame], gpu.pHiZMipViewsDest[m_currentFrame], m_renderWidth, m_renderHeight, m_maxMipLevel);
}

void Renderer::createHiZPSO() {
    auto& gpu = *m_diligent;
    gpu.pHiZMipmapPSO = CreateComputePipeline(gpu.pDevice, "Hi-Z Mipmap PSO", "resources/shaders/hiz_mipmap.comp", {"u_sourceMip", "u_destMip"});
    CreateBindings(gpu.pHiZMipmapPSO, std::span(std::addressof(gpu.pHiZMipmapSRB), 1));
    gpu.pHiZFromDepthPSO = CreateComputePipeline(gpu.pDevice, "Hi-Z From Depth PSO", "resources/shaders/hiz_from_depth.comp", {"u_depth", "u_destMip"});
    CreateBindings(gpu.pHiZFromDepthPSO, std::span(std::addressof(gpu.pHiZFromDepthSRB), 1));
    gpu.pShadowHiZPSO = CreateComputePipeline(gpu.pDevice, "Shadow Hi-Z PSO", "resources/shaders/hiz_from_shadow.comp", {"u_shadowDepth", "u_destMip"});
    CreateBindings(gpu.pShadowHiZPSO, std::span(std::addressof(gpu.pShadowHiZSRB), 1));
    gpu.pShadowHiZArgsPSO = CreateComputePipeline(gpu.pDevice, "Shadow Hi-Z Args PSO", "resources/shaders/shadow_hiz_args.comp");
    gpu.shadowHiZSourceMap = nullptr;
}

void Renderer::releaseShadowHiZ() {
    auto& gpu = *m_diligent;
    for (int layer = 0; layer < kShadowLayers; ++layer) {
        gpu.shadowHiZ[layer].Release();
        gpu.shadowHiZDest[layer].clear();
        gpu.shadowHiZSource[layer].clear();
        gpu.shadowSlices[layer].Release();
        gpu.shadowHiZValid[layer] = false;
    }
}

void Renderer::createShadowHiZArgs(Diligent::Uint32 size, Diligent::Uint32 mips) {
    auto& gpu = *m_diligent;
    gpu.pShadowHiZArgs = CreateDispatchArgsBuffer(gpu.pDevice, "Shadow Hi-Z Args", mips);
    const uint32_t argsConstants[4] = {size, mips, 0u, 0u};
    gpu.pShadowHiZArgsConstants = CreateUniformBuffer(gpu.pDevice, "Shadow Hi-Z Args Constants", sizeof(argsConstants), Diligent::USAGE_DEFAULT);
    gpu.pImmediateContext->UpdateBuffer(gpu.pShadowHiZArgsConstants, 0, sizeof(argsConstants), argsConstants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    gpu.pShadowHiZArgsSRB.Release();
    CreateBoundSRB(gpu.pShadowHiZArgsPSO, gpu.pShadowHiZArgsSRB,
                   {
                       {"SourceCounterBuffer", gpu.pShadowRetestCounter   },
                       {"DispatchArgsBuffer",  gpu.pShadowHiZArgs         },
                       {"HiZArgsConstants",    gpu.pShadowHiZArgsConstants}
    });
}

void Renderer::createShadowHiZLayer(int layer, Diligent::ITexture* shadowMap, Diligent::Uint32 size, Diligent::Uint32 mips) {
    auto& gpu = *m_diligent;
    Diligent::TextureDesc description;
    description.Name = "Shadow Hi-Z";
    description.Type = Diligent::RESOURCE_DIM_TEX_2D;
    description.Width = description.Height = size;
    description.Format = Diligent::TEX_FORMAT_R32_FLOAT;
    description.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS;
    description.MipLevels = mips;
    gpu.pDevice->CreateTexture(description, nullptr, &gpu.shadowHiZ[layer]);
    if (!gpu.shadowHiZ[layer]) return;
    gpu.shadowHiZ[layer]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
    for (Diligent::Uint32 mip = 0; mip < mips; ++mip) {
        gpu.shadowHiZDest[layer].emplace_back();
        CreateMipUavView(gpu.shadowHiZ[layer], mip, Diligent::UAV_ACCESS_FLAG_WRITE, gpu.shadowHiZDest[layer].back());
        gpu.shadowHiZSource[layer].emplace_back();
        if (mip == 0) continue;
        CreateMipUavView(gpu.shadowHiZ[layer], mip - 1, Diligent::UAV_ACCESS_FLAG_READ, gpu.shadowHiZSource[layer].back());
    }
    gpu.shadowSlices[layer] = litgpu::mipView(shadowMap, Diligent::TEXTURE_VIEW_SHADER_RESOURCE, 0, Diligent::RESOURCE_DIM_TEX_2D_ARRAY, static_cast<Diligent::Uint32>(layer), 1);
    if (gpu.shadowSlices[layer]) gpu.shadowSlices[layer]->SetSampler(gpu.pHiZSampler);
}

void Renderer::ensureShadowHiZ() {
    auto& gpu = *m_diligent;
    Diligent::ITexture* shadowMap = m_lighting.shadowMap();
    if (shadowMap == gpu.shadowHiZSourceMap) return;
    gpu.shadowHiZSourceMap = shadowMap;
    releaseShadowHiZ();
    if (!shadowMap) return;
    const auto size = std::max<Diligent::Uint32>((shadowMap->GetDesc().Width + 1) / 2, 1);
    const auto mips = static_cast<Diligent::Uint32>(std::bit_width(size));
    createShadowHiZArgs(size, mips);
    for (int layer = 0; layer < kShadowLayers; ++layer) createShadowHiZLayer(layer, shadowMap, size, mips);
}

void Renderer::buildShadowHiZ(int cascade) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    Diligent::ITexture* hiz = gpu.shadowHiZ[cascade];
    if (!hiz || !gpu.pShadowHiZPSO || !gpu.pHiZMipmapPSO || !gpu.shadowSlices[cascade] || !gpu.pShadowHiZArgsSRB || gpu.shadowHiZDest[cascade].empty()) return;
    auto barrier = [&](Diligent::RESOURCE_STATE state) {
        Diligent::StateTransitionDesc transition{hiz, Diligent::RESOURCE_STATE_UNKNOWN, state, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE};
        context->TransitionResourceStates(1, &transition);
    };
    DispatchSingleGroup(context, gpu.pShadowHiZArgsPSO, gpu.pShadowHiZArgsSRB);
    auto dispatchMip = [&](size_t mip) {
        Diligent::DispatchComputeIndirectAttribs attribs{gpu.pShadowHiZArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
        attribs.DispatchArgsByteOffset = mip * sizeof(uint32_t) * 4;
        context->DispatchComputeIndirect(attribs);
    };
    SetDynamicVariable(gpu.pShadowHiZSRB, "u_shadowDepth", gpu.shadowSlices[cascade]);
    SetDynamicVariable(gpu.pShadowHiZSRB, "u_destMip", gpu.shadowHiZDest[cascade][0]);
    context->SetPipelineState(gpu.pShadowHiZPSO);
    context->CommitShaderResources(gpu.pShadowHiZSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    dispatchMip(0);
    barrier(Diligent::RESOURCE_STATE_UNORDERED_ACCESS);
    context->SetPipelineState(gpu.pHiZMipmapPSO);
    for (size_t mip = 1; mip < gpu.shadowHiZDest[cascade].size(); ++mip) {
        SetDynamicVariable(gpu.pHiZMipmapSRB, "u_sourceMip", gpu.shadowHiZSource[cascade][mip]);
        SetDynamicVariable(gpu.pHiZMipmapSRB, "u_destMip", gpu.shadowHiZDest[cascade][mip]);
        context->CommitShaderResources(gpu.pHiZMipmapSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        dispatchMip(mip);
        barrier(Diligent::RESOURCE_STATE_UNORDERED_ACCESS);
    }
    barrier(Diligent::RESOURCE_STATE_SHADER_RESOURCE);
}
