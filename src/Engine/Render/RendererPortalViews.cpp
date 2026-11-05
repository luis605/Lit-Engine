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

constexpr int kShadeStageCount = 3;

void CopyWholeTexture(Diligent::IDeviceContext* context, Diligent::ITexture* source, Diligent::ITexture* destination) {
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = source;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = destination;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
}

void CopyBufferFully(Diligent::IDeviceContext* context, Diligent::IBuffer* source, Diligent::IBuffer* destination, Diligent::Uint64 bytes) {
    context->CopyBuffer(source, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, destination, 0, bytes, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

PortalHistory& AcquirePortalHistory(DiligentData& gpu, uint64_t key, uint64_t frame) {
    for (PortalHistory& history : gpu.portalHistory) {
        if (history.used && history.key == key) {
            history.lastFrame = frame;
            return history;
        }
    }
    PortalHistory* slot = nullptr;
    if (gpu.portalHistory.size() < static_cast<size_t>(kMaxPortalViews)) {
        gpu.portalHistory.reserve(kMaxPortalViews);
        slot = &gpu.portalHistory.emplace_back();
    } else {
        slot = &*std::ranges::min_element(gpu.portalHistory, {}, &PortalHistory::lastFrame);
    }
    slot->used = true;
    slot->key = key;
    slot->lastFrame = frame;
    slot->previous = 0;
    slot->bits.Release();
    for (HiZSet& set : slot->hiz) set.texture.Release();
    return *slot;
}

void CreateSampledTexture(DiligentData& gpu, Diligent::TextureDesc description, const char* name, Diligent::RefCntAutoPtr<Diligent::ITexture>& texture) {
    description.Name = name;
    description.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    description.Usage = Diligent::USAGE_DEFAULT;
    gpu.pDevice->CreateTexture(description, nullptr, &texture);
}

bool PreparePortalResources(DiligentData& gpu, PortalHistory& history, int index, Diligent::Uint64 bitsSize, Diligent::ITexture* hdr, int width, int height, int mips) {
    if (!history.bits || history.bits->GetDesc().Size != bitsSize) {
        std::vector<uint32_t> zeros(static_cast<size_t>(bitsSize / sizeof(uint32_t)), 0u);
        history.bits = CreateStructuredBuffer(gpu.pDevice, "Portal Visibility Bits", sizeof(uint32_t), static_cast<Diligent::Uint32>(zeros.size()), zeros.data());
    }
    for (HiZSet& set : history.hiz) {
        if (!set.texture) CreateHiZSet(gpu, set, width, height, mips);
    }
    if (!history.bits || !history.hiz[0].texture || !history.hiz[1].texture || !hdr) return false;
    if (!gpu.portalColor[index]) {
        Diligent::TextureDesc description = hdr->GetDesc();
        description.MipLevels = 1;
        CreateSampledTexture(gpu, description, "Portal View Color", gpu.portalColor[index]);
        if (gpu.portalColor[index]) gpu.portalColor[index]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
    }
    return gpu.portalColor[index] != nullptr;
}

SceneUniforms MakePortalSceneUniforms(const SceneUniforms& base, const PortalViewNode& node) {
    SceneUniforms uniforms = base;
    const glm::mat4 viewProjection = node.projection * node.view;
    uniforms.projection = node.projection;
    uniforms.view = node.view;
    uniforms.viewProjection = viewProjection;
    uniforms.invView = glm::inverse(node.view);
    uniforms.invProjection = glm::inverse(node.projection);
    uniforms.invViewProjection = glm::inverse(viewProjection);
    uniforms.prevViewProjection = viewProjection;
    for (size_t planeIndex = 0; planeIndex < 6; ++planeIndex) uniforms.frustumPlanes[planeIndex] = node.planes[planeIndex];
    uniforms.viewPos = node.position;
    uniforms.sunIlluminance = glm::vec4(glm::vec3(uniforms.sunIlluminance) * node.sunScale, uniforms.sunIlluminance.w);
    uniforms.skyGradient[1].w *= node.ambientScale;
    uniforms.skyParams.w = 0.0f;
    return uniforms;
}

void CapturePortalDepth(DiligentData& gpu, int index) {
    const Diligent::TextureDesc sceneDesc = gpu.pSceneDepth->GetDesc();
    auto& portalDepth = gpu.portalDepth[index];
    if (portalDepth && (portalDepth->GetDesc().Width != sceneDesc.Width || portalDepth->GetDesc().Height != sceneDesc.Height)) portalDepth.Release();
    if (!portalDepth) {
        Diligent::TextureDesc description = sceneDesc;
        description.Name = "Portal View Depth";
        gpu.pDevice->CreateTexture(description, nullptr, &portalDepth);
        if (portalDepth) portalDepth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
    }
    if (!portalDepth) return;
    UnbindRenderTargets(gpu.pImmediateContext);
    CopyWholeTexture(gpu.pImmediateContext, gpu.pSceneDepth, portalDepth);
}

}

void Renderer::setPortalFrame(PortalFrame frame) {
    if (frame.nodes.size() > static_cast<size_t>(kMaxPortalViews)) frame.nodes.resize(static_cast<size_t>(kMaxPortalViews));
    const int count = static_cast<int>(frame.nodes.size());
    for (PortalViewNode& node : frame.nodes) {
        for (PortalSurface& surface : node.surfaces) {
            if (surface.child >= count) surface.child = -1;
        }
    }
    m_portalFrame = std::move(frame);
}

void Renderer::renderPortalViews(const SceneUniforms& base, SceneUniforms& restore, uint32_t objectCount) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const PortalViewNode& root = m_portalFrame.nodes[0];
    const Diligent::Uint64 bitsSize = gpu.pVisibilityBits->GetDesc().Size;
    if (!gpu.portalMainBits || gpu.portalMainBits->GetDesc().Size != bitsSize) gpu.portalMainBits = CreateStructuredBuffer(gpu.pDevice, "Portal Main Visibility Bits", sizeof(uint32_t), static_cast<Diligent::Uint32>(bitsSize / sizeof(uint32_t)));
    if (!gpu.portalMainBits) return;
    CopyBufferFully(context, gpu.pVisibilityBits, gpu.portalMainBits, bitsSize);
    for (const PortalSurface& surface : root.surfaces) {
        if (surface.child > 0) renderPortalNode(surface.child, base, objectCount);
    }
    for (bool& valid : gpu.shadowHiZValid) valid = false;
    m_lighting.fitShadows(gpu.mainInput, restore);
    CopyBufferFully(context, gpu.portalMainBits, gpu.pVisibilityBits, bitsSize);
    context->UpdateBuffer(gpu.pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &restore, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_lighting.cullLights(gpu.pSceneUBO[m_currentFrame]);
}

void Renderer::renderPortalGeometry(int index, uint32_t objectCount, PortalHistory& history) {
    auto& gpu = *m_diligent;
    HiZSet& previousHiZ = history.hiz[history.previous];
    HiZSet& currentHiZ = history.hiz[1 - history.previous];
    cullVisibilityPhase(kPhaseOpaque, objectCount, previousHiZ.texture.RawPtr());
    buildVisibilityDraws(kPhaseOpaque);
    bindSceneTargets(true);
    drawVisibilityPhase(kPhaseOpaque);
    UnbindRenderTargets(gpu.pImmediateContext);
    BuildHiZ(gpu, currentHiZ.texture.RawPtr(), currentHiZ.source, currentHiZ.dest, m_renderWidth, m_renderHeight, m_maxMipLevel);
    cullVisibilityPhase(kPhaseRetest, objectCount, currentHiZ.texture.RawPtr());
    buildVisibilityDraws(kPhaseRetest);
    bindSceneTargets(false);
    drawVisibilityPhase(kPhaseRetest);
    recordPortalStats(index);
    for (bool& valid : gpu.shadowHiZValid) valid = false;
    drawShadows(objectCount);
    cullVisibilityPhase(kPhaseTransparent, objectCount, currentHiZ.texture.RawPtr());
    buildVisibilityDraws(kPhaseTransparent);
    UnbindRenderTargets(gpu.pImmediateContext);
}

void Renderer::shadePortalView(int index) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    maskPortalView(index);
    resolvePortalDepth(index);
    CapturePortalDepth(gpu, index);

    for (int stage = 0; stage < kShadeStageCount; ++stage) m_lighting.shade(gpu.pSceneDepth, gpu.pSceneUBO[m_currentFrame], stage);

    compositePortals(index);
    m_lighting.captureSceneColor();
    drawTransparency();
    CopyWholeTexture(context, m_lighting.hdr(), gpu.portalColor[index]);
}

void Renderer::renderPortalNode(int index, const SceneUniforms& base, uint32_t objectCount) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(index)];
    for (const PortalSurface& surface : node.surfaces) {
        if (surface.child > index) renderPortalNode(surface.child, base, objectCount);
    }

    const bool profile = m_fullProfiling;
    auto& snapshot = gpu.portalSnapshot[m_currentFrame][index];
    snapshot.label = node.label;
    snapshot.depth = node.depth;
    snapshot.timed = profile;
    auto& timingQueries = gpu.portalQueries[m_currentFrame][index];
    if (profile) context->EndQuery(timingQueries[0]);
    auto endTiming = [&] {
        if (profile) context->EndQuery(timingQueries[1]);
    };

    PortalHistory& history = AcquirePortalHistory(gpu, node.key, m_frameCounter);
    const Diligent::Uint64 bitsSize = gpu.pVisibilityBits->GetDesc().Size;
    if (!PreparePortalResources(gpu, history, index, bitsSize, m_lighting.hdr(), m_renderWidth, m_renderHeight, m_maxMipLevel)) {
        endTiming();
        return;
    }

    CopyBufferFully(context, history.bits, gpu.pVisibilityBits, bitsSize);

    SceneUniforms uniforms = MakePortalSceneUniforms(base, node);
    const glm::mat4 viewProjection = node.projection * node.view;
    FrameInput input = gpu.mainInput;
    input.view = node.view;
    input.projection = node.projection;
    input.cameraPosition = node.position;
    m_lighting.fitShadows(input, uniforms);
    uniforms.prevViewProjection = viewProjection;
    uniforms.giParams.z = 0.0f;
    uploadCascadeCull();
    context->UpdateBuffer(gpu.pSceneUBO[m_currentFrame], 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_lighting.cullLights(gpu.pSceneUBO[m_currentFrame]);

    renderPortalGeometry(index, objectCount, history);
    shadePortalView(index);

    CopyBufferFully(context, gpu.pVisibilityBits, history.bits, bitsSize);
    history.previous = 1 - history.previous;
    endTiming();
}
