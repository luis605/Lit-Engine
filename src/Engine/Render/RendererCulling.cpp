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

constexpr int kFrustumPlaneCount = 6;
constexpr uint32_t kCascadePlaneCount = 24;
constexpr uint32_t kVisibilityGroupObjects = 32 * kWorkgroupSize;
constexpr float kBlackColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

void DrawMeshCommands(Diligent::IDeviceContext* context, const DiligentData::VisibilityPhase& phase, Diligent::IPipelineState* pipeline, Diligent::IShaderResourceBinding* binding, Diligent::Uint64 commandSlot) {
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DrawIndexedIndirectAttribs attribs;
    attribs.IndexType = Diligent::VT_UINT32;
    attribs.pAttribsBuffer = phase.drawCommands;
    attribs.DrawArgsOffset = commandSlot * kMaxMeshes * sizeof(DrawElementsIndirectCommand);
    attribs.DrawCount = kMaxMeshes;
    attribs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
    attribs.pCounterBuffer = phase.drawCounters;
    attribs.CounterOffset = commandSlot * sizeof(unsigned int);
    attribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->DrawIndexedIndirect(attribs);
}

void DrawPointCommands(Diligent::IDeviceContext* context, const DiligentData::VisibilityPhase& phase, Diligent::IPipelineState* pipeline, Diligent::IShaderResourceBinding* binding, Diligent::Uint64 commandSlot) {
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DrawIndirectAttribs attribs;
    attribs.pAttribsBuffer = phase.pointCommands;
    attribs.DrawArgsOffset = commandSlot * kMaxMeshes * sizeof(DrawElementsIndirectCommand);
    attribs.DrawCount = kMaxMeshes;
    attribs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
    attribs.pCounterBuffer = phase.pointCounters;
    attribs.CounterOffset = commandSlot * sizeof(unsigned int);
    attribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->DrawIndirect(attribs);
}

}

void Renderer::resetVisibilityCounters(int phaseIndex) {
    auto& gpu = *m_diligent;
    auto& phase = gpu.visibility[phaseIndex];
    auto* context = gpu.pImmediateContext.RawPtr();
    ClearCounter(context, phase.visibleCounter);
    context->UpdateBuffer(phase.bucketCounts, 0, sizeof(unsigned int) * ActiveBucketCount(m_numDrawingShaders), m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (phaseIndex == kPhaseOpaque) {
        ClearCounter(context, gpu.pCasterCounter);
        ClearCounter(context, gpu.pLocalCasterCounter);
    }
    if (phaseIndex == kPhaseRetest) ClearCounter(context, gpu.pTransparentCandidateCounter);
}

void Renderer::prepareCandidateDispatch(int phaseIndex, uint32_t objectCount) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    if (phaseIndex == kPhaseTransparent) {
        DispatchSingleGroup(context, gpu.pDispatchArgsPSO, gpu.pTransparentDispatchArgsSRB[m_currentFrame]);
        return;
    }
    if (phaseIndex != kPhaseOpaque) return;
    ClearCounter(context, gpu.pEarlyCandidateCounter);
    context->SetPipelineState(gpu.pVisibilityCompactPSO);
    context->CommitShaderResources(gpu.pEarlyCompactSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs((objectCount + kVisibilityGroupObjects - 1) / kVisibilityGroupObjects, 1, 1));
    DispatchSingleGroup(context, gpu.pDispatchArgsPSO, gpu.pEarlyDispatchArgsSRB[m_currentFrame]);
}

void Renderer::cullVisibilityPhase(int phaseIndex, uint32_t objectCount, int hizFrame) {
    cullVisibilityPhase(phaseIndex, objectCount, m_diligent->pHiZTextures[hizFrame].RawPtr());
}

void Renderer::cullVisibilityPhase(int phaseIndex, uint32_t objectCount, Diligent::ITexture* hiz) {
    auto& gpu = *m_diligent;
    auto& phase = gpu.visibility[phaseIndex];
    auto* context = gpu.pImmediateContext.RawPtr();
    const bool candidateInput = phaseIndex == kPhaseTransparent;

    resetVisibilityCounters(phaseIndex);
    const CullingUniforms uniforms = MakeCullingUniforms(objectCount, m_maxObjects, m_smallObjectThreshold, m_lodBias, m_forcedLod, m_numDrawingShaders);
    context->UpdateBuffer(phase.cullingUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    prepareCandidateDispatch(phaseIndex, objectCount);

    auto& binding = phase.cullSRB[m_currentFrame];
    SetDynamicVariable(binding, "u_hizTexture", hiz->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    context->SetPipelineState(phaseIndex == kPhaseOpaque ? gpu.pCullingPSO.RawPtr() : phaseIndex == kPhaseRetest ? gpu.pRetestCullingPSO.RawPtr() : gpu.pTransparentCullPSO.RawPtr());
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (candidateInput || phaseIndex == kPhaseOpaque) {
        context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{candidateInput ? gpu.pTransparentDispatchArgs.RawPtr() : gpu.pEarlyCandidateDispatchArgs.RawPtr(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    } else {
        context->DispatchCompute(Diligent::DispatchComputeAttribs((objectCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1));
    }
}

void Renderer::buildVisibilityDraws(int phaseIndex, uint32_t views) {
    auto& gpu = *m_diligent;
    auto& phase = gpu.visibility[phaseIndex];
    auto* context = gpu.pImmediateContext.RawPtr();
    const uint32_t viewBuckets = ActiveBucketCount(m_numDrawingShaders);
    const uint32_t totalBuckets = viewBuckets * views;

    context->SetPipelineState(gpu.pPrefixSumPSO);
    {
        Diligent::MapHelper<PrefixSumConstants> constants(context, gpu.pPrefixSumConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        constants->bucketCount = totalBuckets;
    }
    context->CommitShaderResources(phase.prefixSumSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs(1, 1, 1));

    Diligent::StateTransitionDesc writeHeadBarrier{phase.bucketWriteHeads, Diligent::RESOURCE_STATE_UNORDERED_ACCESS, Diligent::RESOURCE_STATE_UNORDERED_ACCESS, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE};
    context->TransitionResourceStates(1, &writeHeadBarrier);

    DispatchSingleGroup(context, gpu.pDispatchArgsPSO, phase.scatterDispatchArgsSRB[m_currentFrame]);

    context->SetPipelineState(gpu.pScatterPSO);
    {
        Diligent::MapHelper<ScatterConstants> constants(context, gpu.pScatterConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        constants->maxDraws = static_cast<uint32_t>(m_maxObjects);
    }
    context->CommitShaderResources(phase.scatterSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{phase.scatterDispatchArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});

    const size_t counterBytes = sizeof(unsigned int) * m_numDrawingShaders * views;
    context->UpdateBuffer(phase.drawCounters, 0, counterBytes, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->UpdateBuffer(phase.pointCounters, 0, counterBytes, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    context->SetPipelineState(gpu.pCommandGenPSO);
    {
        Diligent::MapHelper<CommandGenUniforms> constants(context, gpu.pCommandGenConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        constants->maxBucketsPerShader = kMaxMeshes;
        constants->totalBuckets = totalBuckets;
        constants->bucketSlots = BucketSlotCount();
        constants->viewBuckets = viewBuckets;
    }
    context->CommitShaderResources(phase.commandGenSRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs((totalBuckets + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1));
}

void Renderer::bindSceneTargets(bool clear) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    Diligent::ITextureView* targets[4] = {litgpu::rtv(m_lighting.gbuffer(0)), litgpu::rtv(m_lighting.gbuffer(1)), litgpu::rtv(m_lighting.gbuffer(2)), litgpu::rtv(m_lighting.hdr())};
    auto* depthView = gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    SetSceneViewport(gpu);
    context->SetRenderTargets(4, targets, depthView, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (!clear) return;
    context->ClearRenderTarget(targets[3], kBlackColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(depthView, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::bindBackBuffer(bool sceneDepthValid) {
    auto& gpu = *m_diligent;
    auto* backBufferView = gpu.pSwapChain->GetCurrentBackBufferRTV();
    const bool scaled = gpu.pOverlayDepth && gpu.renderWidth != 0 && (gpu.renderWidth != gpu.pSwapChain->GetDesc().Width || gpu.renderHeight != gpu.pSwapChain->GetDesc().Height);
    auto* depthView = scaled ? gpu.pOverlayDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    SetSwapChainViewport(gpu);
    const bool resolve = scaled && sceneDepthValid && gpu.pDepthUpscalePSO && gpu.pDepthUpscaleSRB;
    if (resolve) resolveOverlayDepth();
    gpu.pImmediateContext->SetRenderTargets(1, &backBufferView, depthView, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (scaled && !resolve) gpu.pImmediateContext->ClearDepthStencil(depthView, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::uploadCascadeCull(bool empty) {
    struct CascadeCull {
        glm::vec4 planes[kCascadePlaneCount];
        uint32_t info[4];
        glm::vec4 localSpheres[kMaxLocalShadowLights];
        glm::vec4 localCones[kMaxLocalShadowLights];
        uint32_t localSlots[kMaxLocalShadowLights][4];
    } cull{};
    const int localCount = !empty && m_diligent->pLocalShadowCullPSO ? std::min<int>(m_lighting.localShadowCasterCount(), kMaxLocalShadowLights) : 0;
    for (int casterIndex = 0; casterIndex < localCount; ++casterIndex) {
        const LocalShadowCaster& caster = m_lighting.localShadowCasters()[static_cast<size_t>(casterIndex)];
        cull.localSpheres[casterIndex] = caster.sphere;
        cull.localCones[casterIndex] = caster.cone;
        cull.localSlots[casterIndex][0] = caster.faceBase;
        cull.localSlots[casterIndex][1] = caster.faceMask;
        cull.localSlots[casterIndex][2] = caster.spot;
    }
    cull.info[1] = static_cast<uint32_t>(localCount);
    const auto& cascades = m_lighting.cascades();
    const int cascadeCount = !empty && m_diligent->pShadowCullPSO ? std::min<int>(m_lighting.cascadeCount(), kCascadeShadowViews) : 0;
    for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex) {
        const Cascade& cascade = cascades[static_cast<size_t>(cascadeIndex)];
        if (!cascade.due) continue;
        cull.info[0] |= 1u << cascadeIndex;
        for (int planeIndex = 0; planeIndex < kFrustumPlaneCount; ++planeIndex) cull.planes[cascadeIndex * kFrustumPlaneCount + planeIndex] = cascade.planes[planeIndex];
    }
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pCascadeCullUBO, 0, sizeof(cull), &cull, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::drawVisibilityPhase(int phaseIndex) { drawSurfaces(phaseIndex, -1); }

void Renderer::drawSurfaces(int phaseIndex, int cascade, uint32_t view) {
    auto& gpu = *m_diligent;
    auto& phase = gpu.visibility[phaseIndex];
    const auto& meshBindings = cascade >= 0 ? gpu.shadowSRBs[m_currentFrame][cascade] : phase.opaqueSRBs[m_currentFrame];
    const auto& pointBindings = cascade >= 0 ? gpu.shadowPointSRBs[m_currentFrame][cascade] : phase.pointSRBs[m_currentFrame];
    auto* context = gpu.pImmediateContext.RawPtr();
    Diligent::IBuffer* vertexBuffers[] = {gpu.pVBO};
    context->SetVertexBuffers(0, 1, vertexBuffers, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    context->SetIndexBuffer(gpu.pEBO, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    for (uint32_t shaderId = 0; shaderId < gpu.surfaces.size(); ++shaderId) {
        const auto commandSlot = static_cast<Diligent::Uint64>(view) * m_numDrawingShaders + shaderId;
        Diligent::IPipelineState* meshPipeline = SurfacePipeline(gpu.surfaces[shaderId], phaseIndex, false);
        if (meshPipeline && shaderId < meshBindings.size() && meshBindings[shaderId]) DrawMeshCommands(context, phase, meshPipeline, meshBindings[shaderId], commandSlot);
        Diligent::IPipelineState* pointPipeline = SurfacePipeline(gpu.surfaces[shaderId], phaseIndex, true);
        if (pointPipeline && shaderId < pointBindings.size() && pointBindings[shaderId]) DrawPointCommands(context, phase, pointPipeline, pointBindings[shaderId], commandSlot);
    }
}
