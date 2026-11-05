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

constexpr int kShadowPlaneCount = 6;

struct ShadowCullConstants {
    glm::vec4 planes[kShadowPlaneCount];
    glm::vec4 lod;
    glm::mat4 hizViewProjection;
    glm::vec4 origin;
};

struct ShadowPassConstants {
    glm::mat4 viewProjection;
    glm::vec4 params;
};

struct LocalShadowViewConstants {
    glm::vec4 origins[kMaxLocalShadowViews];
    uint32_t info[4];
};

void SetShadowTarget(Diligent::IDeviceContext* context, Diligent::ITextureView* target, const Diligent::Viewport& viewport, int resolution, bool clear) {
    context->SetRenderTargets(0, nullptr, target, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (clear) context->ClearDepthStencil(target, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(resolution), static_cast<Diligent::Uint32>(resolution));
}

}

void Renderer::dispatchCascadeCull(int cascadeIndex, int hizLayer, Diligent::IPipelineState* pipeline, Diligent::IShaderResourceBinding* binding, Diligent::IBuffer* dispatchArgs, const glm::mat4& hizViewProjection, bool hizValid) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    auto& phase = gpu.visibility[kPhaseShadow];
    const Cascade& cascade = m_lighting.cascades()[static_cast<size_t>(cascadeIndex)];

    ShadowCullConstants constants{};
    for (int planeIndex = 0; planeIndex < kShadowPlaneCount; ++planeIndex) constants.planes[planeIndex] = cascade.planes[planeIndex];
    constants.lod = glm::vec4(cascade.texelsPerUnit, static_cast<float>(cascadeIndex), hizValid && gpu.shadowHiZ[hizLayer] ? 1.0f : 0.0f, 0.0f);
    constants.hizViewProjection = hizViewProjection;
    context->UpdateBuffer(gpu.pShadowCullUBO[cascadeIndex], 0, sizeof(constants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ClearCounter(context, phase.visibleCounter);
    context->UpdateBuffer(phase.bucketCounts, 0, sizeof(unsigned int) * ActiveBucketCount(m_numDrawingShaders), m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto* sceneHiZ = gpu.pHiZTextures[m_currentFrame].RawPtr();
    SetDynamicVariable(binding, "u_hizTexture", sceneHiZ->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    Diligent::ITexture* shadowHiZ = gpu.shadowHiZ[hizLayer] ? gpu.shadowHiZ[hizLayer].RawPtr() : sceneHiZ;
    SetDynamicVariable(binding, "u_shadowHiz", shadowHiZ->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{dispatchArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    buildVisibilityDraws(kPhaseShadow);
}

void Renderer::renderCascade(int cascadeIndex) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const Cascade& cascade = m_lighting.cascades()[static_cast<size_t>(cascadeIndex)];
    const int resolution = m_lighting.shadowResolution();
    const int layer = cascade.renderLayer;
    const int earlyLayer = cascade.layer;

    const ShadowPassConstants pass{cascade.renderViewProjection, glm::vec4(static_cast<float>(resolution), 0.0f, 0.0f, 0.0f)};
    context->UpdateBuffer(gpu.pShadowPassUBO[cascadeIndex], 0, sizeof(pass), &pass, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ClearCounter(context, gpu.pShadowRetestCounter);
    dispatchCascadeCull(cascadeIndex, earlyLayer, gpu.pShadowCullPSO, gpu.shadowCullSRB[m_currentFrame][cascadeIndex], gpu.pCasterDispatchArgs, gpu.shadowHiZViewProj[earlyLayer], gpu.shadowHiZValid[earlyLayer]);

    Diligent::Viewport viewport;
    viewport.TopLeftX = static_cast<float>(cascade.viewport[0]);
    viewport.TopLeftY = static_cast<float>(cascade.viewport[1]);
    viewport.Width = static_cast<float>(cascade.viewport[2]);
    viewport.Height = static_cast<float>(cascade.viewport[3]);
    Diligent::ITextureView* target = m_lighting.shadowTarget(layer);
    SetShadowTarget(context, target, viewport, resolution, cascade.clear);
    drawSurfaces(kPhaseShadow, cascadeIndex);
    UnbindRenderTargets(context);

    if (!gpu.shadowHiZ[layer]) {
        gpu.shadowHiZValid[layer] = false;
        return;
    }
    buildShadowHiZ(layer);
    DispatchSingleGroup(context, gpu.pDispatchArgsPSO, gpu.pShadowRetestDispatchArgsSRB[m_currentFrame]);
    dispatchCascadeCull(cascadeIndex, layer, gpu.pShadowLateCullPSO, gpu.shadowLateCullSRB[m_currentFrame][cascadeIndex], gpu.pShadowRetestDispatchArgs, cascade.viewProjection, true);
    SetShadowTarget(context, target, viewport, resolution, false);
    drawSurfaces(kPhaseShadow, cascadeIndex);
    UnbindRenderTargets(context);
    gpu.shadowHiZViewProj[layer] = cascade.viewProjection;
    gpu.shadowHiZValid[layer] = true;
}

void Renderer::drawShadows(uint32_t objectCount) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const int cascadeCount = gpu.pShadowCullPSO && gpu.pShadowLateCullPSO ? std::min<int>(m_lighting.cascadeCount(), kCascadeShadowViews) : 0;
    drawLocalShadows(objectCount);
    if (cascadeCount <= 0) return;
    ensureShadowHiZ();
    const CullingUniforms uniforms = MakeCullingUniforms(objectCount, m_maxObjects, 0.0f, m_lodBias, m_forcedLod, m_numDrawingShaders);
    context->UpdateBuffer(gpu.visibility[kPhaseShadow].cullingUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DispatchSingleGroup(context, gpu.pDispatchArgsPSO, gpu.pCasterDispatchArgsSRB[m_currentFrame]);
    for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex) {
        if (m_lighting.cascades()[static_cast<size_t>(cascadeIndex)].due) renderCascade(cascadeIndex);
    }
}

void Renderer::drawLocalShadows(uint32_t objectCount) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const int casters = gpu.pLocalShadowCullPSO ? m_lighting.localShadowCasterCount() : 0;
    if (casters <= 0) return;
    auto& phase = gpu.visibility[kPhaseShadow];
    const int resolution = m_lighting.localShadowResolution();
    const CullingUniforms uniforms = MakeCullingUniforms(objectCount, m_maxObjects, 0.0f, m_lodBias, m_forcedLod, m_numDrawingShaders);
    context->UpdateBuffer(phase.cullingUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DispatchSingleGroup(context, gpu.pDispatchArgsPSO, gpu.pLocalCasterDispatchArgsSRB[m_currentFrame]);
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(resolution);
    viewport.Height = static_cast<float>(resolution);
    const auto& views = m_lighting.localShadowViews();
    const uint32_t viewBuckets = ActiveBucketCount(m_numDrawingShaders);
    const int group = static_cast<int>(std::clamp<uint32_t>(kShadowBucketCapacity / std::max(viewBuckets, 1u), 1u, static_cast<uint32_t>(kMaxLocalShadowViews)));
    for (int first = 0; first < kMaxLocalShadowViews; first += group) {
        const int count = std::min(group, kMaxLocalShadowViews - first);
        LocalShadowViewConstants batch{};
        bool anyActive = false;
        for (int offset = 0; offset < count; ++offset) {
            const LocalShadowView& view = views[static_cast<size_t>(first + offset)];
            batch.origins[offset] = glm::vec4(view.origin, view.texelsAtUnitDistance);
            if (!view.active) continue;
            anyActive = true;
            const ShadowPassConstants pass{view.viewProjection, glm::vec4(static_cast<float>(resolution), view.projectionScale, 1.0f, 0.0f)};
            context->UpdateBuffer(gpu.pShadowPassUBO[kCascadeShadowViews + first + offset], 0, sizeof(pass), &pass, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        if (!anyActive) continue;
        batch.info[0] = static_cast<uint32_t>(first);
        batch.info[1] = static_cast<uint32_t>(count);
        batch.info[2] = viewBuckets;
        context->UpdateBuffer(gpu.pLocalShadowViewsUBO, 0, sizeof(batch), &batch, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ClearCounter(context, phase.visibleCounter);
        context->UpdateBuffer(phase.bucketCounts, 0, sizeof(unsigned int) * viewBuckets * static_cast<uint32_t>(count), m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        auto& binding = gpu.localCullSRB[m_currentFrame];
        auto* sceneHiZView = gpu.pHiZTextures[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        SetDynamicVariable(binding, "u_hizTexture", sceneHiZView);
        SetDynamicVariable(binding, "u_shadowHiz", sceneHiZView);
        context->SetPipelineState(gpu.pLocalShadowCullPSO);
        context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{gpu.pLocalCasterDispatchArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        buildVisibilityDraws(kPhaseShadow, static_cast<uint32_t>(count));
        for (int offset = 0; offset < count; ++offset) {
            if (!views[static_cast<size_t>(first + offset)].active) continue;
            SetShadowTarget(context, m_lighting.localShadowTarget(first + offset), viewport, resolution, true);
            drawSurfaces(kPhaseShadow, kCascadeShadowViews + first + offset, static_cast<uint32_t>(offset));
            UnbindRenderTargets(context);
        }
    }
}
