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
import Engine.Render.graph;

#include "RendererInternal.hpp"
#include "RendererGraph.hpp"

namespace {

constexpr float kPortalRectMinW = 0.01f;
constexpr int kPortalRectMargin = 48;
constexpr int kPortalRectAlign = 16;


constexpr int kDepthRegionAlign = 128;
constexpr int kShadeStageCount = 3;

struct ScreenBounds {
    glm::vec2 low;
    glm::vec2 high;
};

ScreenBounds ProjectPortalSurface(const PortalViewNode& owner, const PortalSurface& entry, const glm::vec2& size) {
    const glm::mat4 viewProjection = owner.projection * owner.view;
    std::array<glm::vec4, 4> corners;
    for (int corner = 0; corner < 4; ++corner) {
        const float u = corner == 1 || corner == 2 ? 1.0f : -1.0f;
        const float v = corner >= 2 ? 1.0f : -1.0f;
        corners[static_cast<size_t>(corner)] = viewProjection * glm::vec4(entry.center + entry.axisU * u + entry.axisV * v, 1.0f);
    }
    ScreenBounds bounds{size, glm::vec2(0.0f)};
    auto include = [&](const glm::vec4& clip) {
        const glm::vec2 pixel((clip.x / clip.w * 0.5f + 0.5f) * size.x, (0.5f - clip.y / clip.w * 0.5f) * size.y);
        bounds.low = glm::min(bounds.low, pixel);
        bounds.high = glm::max(bounds.high, pixel);
    };
    for (size_t corner = 0; corner < corners.size(); ++corner) {
        const glm::vec4& from = corners[corner];
        const glm::vec4& to = corners[(corner + 1) % corners.size()];
        if (from.w >= kPortalRectMinW) include(from);
        if ((from.w >= kPortalRectMinW) != (to.w >= kPortalRectMinW)) include(glm::mix(from, to, (kPortalRectMinW - from.w) / (to.w - from.w)));
    }
    return bounds;
}

glm::ivec4 PaddedPixelRect(const ScreenBounds& bounds, const glm::ivec2& limit) {
    glm::ivec2 begin = glm::clamp(glm::ivec2(glm::floor(bounds.low)) - kPortalRectMargin, glm::ivec2(0), limit - 1);
    begin = begin / kPortalRectAlign * kPortalRectAlign;
    const glm::ivec2 end = glm::clamp(glm::ivec2(glm::ceil(bounds.high)) + kPortalRectMargin, begin + 1, limit);
    return glm::ivec4(begin, end - begin);
}

std::pair<int, const PortalSurface*> FindPortalEntry(const PortalFrame& frame, int child) {
    for (int node = 0; node < static_cast<int>(frame.nodes.size()); ++node) {
        for (const PortalSurface& surface : frame.nodes[static_cast<size_t>(node)].surfaces) {
            if (surface.child == child) return {node, &surface};
        }
    }
    return {-1, nullptr};
}

void CopyWholeTexture(Diligent::IDeviceContext* context, Diligent::ITexture* source, Diligent::ITexture* destination) {
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = source;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = destination;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
}

void CopyTextureRect(Diligent::IDeviceContext* context, Diligent::ITexture* source, Diligent::ITexture* destination, const glm::ivec4& rect) {
    if (rect.z <= 0 || rect.w <= 0) {
        CopyWholeTexture(context, source, destination);
        return;
    }
    const Diligent::Box box(static_cast<Diligent::Uint32>(rect.x), static_cast<Diligent::Uint32>(rect.x + rect.z), static_cast<Diligent::Uint32>(rect.y), static_cast<Diligent::Uint32>(rect.y + rect.w));
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = source;
    copy.pSrcBox = &box;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = destination;
    copy.DstX = static_cast<Diligent::Uint32>(rect.x);
    copy.DstY = static_cast<Diligent::Uint32>(rect.y);
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

bool PreparePortalHistory(DiligentData& gpu, PortalHistory& history, Diligent::Uint64 bitsSize, int width, int height, int mips) {
    if (!history.bits || history.bits->GetDesc().Size != bitsSize) {
        std::vector<uint32_t> zeros(static_cast<size_t>(bitsSize / sizeof(uint32_t)), 0u);
        history.bits = CreateStructuredBuffer(gpu.pDevice, "Portal Visibility Bits", sizeof(uint32_t), static_cast<Diligent::Uint32>(zeros.size()), zeros.data());
    }
    for (HiZSet& set : history.hiz) {
        if (!set.texture) CreateHiZSet(gpu, set, width, height, mips);
    }
    return history.bits && history.hiz[0].texture && history.hiz[1].texture;
}

bool PreparePortalResources(DiligentData& gpu, PortalHistory& history, int index, Diligent::Uint64 bitsSize, Diligent::ITexture* hdr, int width, int height, int mips) {
    if (!PreparePortalHistory(gpu, history, bitsSize, width, height, mips) || !hdr) return false;
    if (!gpu.portalColor[index]) {
        Diligent::TextureDesc description = hdr->GetDesc();
        description.MipLevels = 1;
        CreateSampledTexture(gpu, description, "Portal View Color", gpu.portalColor[index]);
        if (gpu.portalColor[index]) gpu.portalColor[index]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
    }
    return gpu.portalColor[index] != nullptr;
}

void ApplyCellLighting(SceneUniforms& uniforms, const PortalViewNode& node) {
    uniforms.cellParams.x = static_cast<float>(node.cell);
    uniforms.sunIlluminance = glm::vec4(glm::vec3(uniforms.sunIlluminance) * node.sunScale * node.lightTint, uniforms.sunIlluminance.w);
    uniforms.skyGradient[1].w *= node.ambientScale;
    uniforms.cellTint = glm::vec4(node.lightTint, 0.0f);
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
    ApplyCellLighting(uniforms, node);
    uniforms.skyParams.w = 0.0f;
    return uniforms;
}

bool CascadesCover(const std::array<Cascade, 4>& cascades, int count, const PortalViewNode& node, float distance) {
    if (count <= 0 || distance <= 0.0f) return false;
    const Cascade& last = cascades[static_cast<size_t>(count - 1)];
    if (!last.valid) return false;
    const glm::mat4 world = glm::inverse(node.view);
    const glm::vec3 eye(world[3]);
    const float tanX = 1.0f / std::max(std::abs(node.projection[0][0]), 1e-4f);
    const float tanY = 1.0f / std::max(std::abs(node.projection[1][1]), 1e-4f);
    glm::vec3 lo = eye;
    glm::vec3 hi = eye;
    const glm::vec3 directions[5] = {{-tanX, -tanY, -1.0f}, {tanX, -tanY, -1.0f}, {-tanX, tanY, -1.0f}, {tanX, tanY, -1.0f}, {0.0f, 0.0f, -1.0f}};
    for (const glm::vec3& direction : directions) {
        const glm::vec3 point = glm::vec3(world * glm::vec4(glm::normalize(direction) * distance, 1.0f));
        lo = glm::min(lo, point);
        hi = glm::max(hi, point);
    }
    lo = glm::max(lo, node.cellMin);
    hi = glm::min(hi, node.cellMax);
    if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) return true;
    constexpr float kMargin = 0.96f;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y, (corner & 4) ? hi.z : lo.z);
        const glm::vec4 clip = last.viewProjection * glm::vec4(point, 1.0f);
        if (clip.w <= 0.0f) return false;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (std::abs(ndc.x) > kMargin || std::abs(ndc.y) > kMargin || ndc.z < 0.0f || ndc.z > 1.0f) return false;
    }
    return true;
}

void CapturePortalGBuffer(DiligentData& gpu, int index, Diligent::ITexture* const (&gbuffer)[3], const glm::ivec4& rect) {
    for (int target = 0; target < 3; ++target) {
        auto& copy = gpu.portalGBuffer[index][target];
        if (!gbuffer[target]) {
            copy.Release();
            continue;
        }
        const Diligent::TextureDesc sourceDesc = gbuffer[target]->GetDesc();
        if (copy && (copy->GetDesc().Width != sourceDesc.Width || copy->GetDesc().Height != sourceDesc.Height || copy->GetDesc().Format != sourceDesc.Format)) copy.Release();
        if (!copy) {
            Diligent::TextureDesc description = sourceDesc;
            description.Name = "Portal View GBuffer";
            description.MipLevels = 1;
            description.BindFlags = Diligent::BIND_SHADER_RESOURCE;
            gpu.pDevice->CreateTexture(description, nullptr, &copy);
            if (copy) copy->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
        }
        if (copy) CopyTextureRect(gpu.pImmediateContext, gbuffer[target], copy, rect);
    }
}

void CapturePortalDepth(DiligentData& gpu, int index, const glm::ivec4& rect) {
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
    CopyTextureRect(gpu.pImmediateContext, gpu.pSceneDepth, portalDepth, rect);
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

void Renderer::beginSharedCascades() {
    m_sharedCascades.cascades = m_lighting.cascades();
    m_sharedCascades.valid = m_lighting.cascadeCount() > 0;
    m_sharedCascades.resident = false;
    m_cascadeShared = false;
    m_cascadeReuse = false;
    for (auto& snapshot : m_diligent->portalSnapshot[m_currentFrame]) snapshot.sunCascades = snapshot.sharedCascades = false;
}

void Renderer::selectViewCascades(int index, const PortalViewNode& node, SceneUniforms& uniforms) {
    m_cascadeShared = false;
    m_cascadeReuse = false;
    uniforms.cellParams.z = 0.0f;
    const int count = m_lighting.cascadeCount();
    if (m_viewSunShadows && m_sharedCascades.valid && count > 0) {
        const float distance = uniforms.cascadeSplits[count - 1];
        if (CascadesCover(m_sharedCascades.cascades, count, node, distance)) {
            m_lighting.useCascades(m_sharedCascades.cascades, uniforms);
            uniforms.cellParams.z = distance;
            m_cascadeShared = true;
            m_cascadeReuse = m_sharedCascades.resident;
        }
    }
    auto& snapshot = m_diligent->portalSnapshot[m_currentFrame][index];
    snapshot.sunCascades = m_viewSunShadows && count > 0;
    snapshot.sharedCascades = m_cascadeShared;
}

void Renderer::selectMainCascades() {
    m_cascadeShared = m_sharedCascades.valid;
    m_cascadeReuse = m_cascadeShared && m_sharedCascades.resident;
    auto& snapshot = m_diligent->portalSnapshot[m_currentFrame][0];
    snapshot.sunCascades = m_viewSunShadows && m_lighting.cascadeCount() > 0;
    snapshot.sharedCascades = m_cascadeReuse;
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
    m_viewSunShadows = root.sunScale > 0.0f;
    if (!m_viewSunShadows) restore.shadowParams.x = 0.0f;
    selectMainCascades();
    CopyBufferFully(context, gpu.portalMainBits, gpu.pVisibilityBits, bitsSize);
    context->UpdateBuffer(gpu.pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &restore, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_lighting.cullLights(gpu.pSceneUBO[m_currentFrame]);
}

glm::ivec4 Renderer::portalViewRect(int index) const {
    if (index <= 0) return glm::ivec4(0);
    const glm::vec2 size(static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight));
    ScreenBounds visible{glm::vec2(0.0f), size};
    for (int child = index; child > 0;) {
        const auto [parent, entry] = FindPortalEntry(m_portalFrame, child);
        if (!entry) break;
        const ScreenBounds quad = ProjectPortalSurface(m_portalFrame.nodes[static_cast<size_t>(parent)], *entry, size);
        visible.low = glm::max(visible.low, quad.low);
        visible.high = glm::min(visible.high, quad.high);
        child = parent;
    }
    return PaddedPixelRect(visible, glm::ivec2(static_cast<int>(m_renderWidth), static_cast<int>(m_renderHeight)));
}

glm::ivec4 Renderer::portalDepthRegion(int index) const {
    if (index <= 0 || !m_portalFrame.nodes[static_cast<size_t>(index)].masked) return glm::ivec4(0);
    const auto [parent, entry] = FindPortalEntry(m_portalFrame, index);
    if (!entry) return glm::ivec4(0);
    const glm::ivec2 limit(static_cast<int>(m_renderWidth), static_cast<int>(m_renderHeight));
    const ScreenBounds quad = ProjectPortalSurface(m_portalFrame.nodes[static_cast<size_t>(parent)], *entry, glm::vec2(limit));
    const glm::ivec4 own = PaddedPixelRect(quad, limit);
    const glm::ivec4 visible = portalViewRect(index);
    const glm::ivec2 begin = glm::min(glm::ivec2(own), glm::ivec2(visible)) / kDepthRegionAlign * kDepthRegionAlign;
    glm::ivec2 end = glm::max(glm::ivec2(own) + glm::ivec2(own.z, own.w), glm::ivec2(visible) + glm::ivec2(visible.z, visible.w));
    end = (end + kDepthRegionAlign - 1) / kDepthRegionAlign * kDepthRegionAlign;
    if (end.x + kDepthRegionAlign > limit.x) end.x = limit.x;
    if (end.y + kDepthRegionAlign > limit.y) end.y = limit.y;
    return glm::ivec4(begin, end);
}

glm::ivec4 Renderer::portalSurfaceRect(const PortalSurface& surface) const {
    if (surface.child > 0) return portalViewRect(surface.child);
    return glm::ivec4(0, 0, static_cast<int>(m_renderWidth), static_cast<int>(m_renderHeight));
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
    BuildHiZ(gpu, currentHiZ.texture.RawPtr(), currentHiZ.source, currentHiZ.dest, m_renderWidth, m_renderHeight, m_maxMipLevel, portalDepthRegion(index));
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
    CapturePortalDepth(gpu, index, portalViewRect(index));
    Diligent::ITexture* const gbuffer[3] = {m_lighting.gbuffer(0), m_lighting.gbuffer(1), m_lighting.gbuffer(2)};
    CapturePortalGBuffer(gpu, index, gbuffer, portalViewRect(index));

    m_lighting.setViewRect(portalViewRect(index), portalDepthRegion(index));
    for (int stage = 0; stage < kShadeStageCount - 1; ++stage) m_lighting.shade(gpu.pSceneDepth, gpu.pSceneUBO[m_currentFrame], stage);
    gpu.portalCompositeWritesDepth = false;
    compositePortals(index);
    gpu.portalCompositeWritesDepth = true;
    UnbindRenderTargets(context);
    m_lighting.shade(gpu.pSceneDepth, gpu.pSceneUBO[m_currentFrame], kShadeStageCount - 1);
    m_lighting.setViewRect(glm::ivec4(0));
    compositePortals(index);
    if (gpu.sceneHasTranslucent) m_lighting.captureSceneColor();
    drawTransparency();
    CopyTextureRect(context, m_lighting.hdr(), gpu.portalColor[index], portalViewRect(index));
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
    m_viewSunShadows = node.sunScale > 0.0f;
    if (!m_viewSunShadows) uniforms.shadowParams.x = 0.0f;
    selectViewCascades(index, node, uniforms);
    uniforms.prevViewProjection = viewProjection;
    uniforms.giParams.z = 0.0f;
    uniforms.viewRect = portalViewRect(index);
    uniforms.depthRegion = portalDepthRegion(index);
    uploadCascadeCull();
    context->UpdateBuffer(gpu.pSceneUBO[m_currentFrame], 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_lighting.cullLights(gpu.pSceneUBO[m_currentFrame]);

    renderPortalGeometry(index, objectCount, history);
    shadePortalView(index);

    CopyBufferFully(context, gpu.pVisibilityBits, history.bits, bitsSize);
    history.previous = 1 - history.previous;
    endTiming();
}

void Renderer::addPortalPasses(rg::RenderGraph& graph, FrameGraphData& frame, uint32_t objectCount) {
    auto& gpu = *m_diligent;
    const PortalViewNode& root = m_portalFrame.nodes[0];
    const bool children = !root.surfaces.empty();
    graph.addPass("Portal Root Setup", [&](rg::PassBuilder& pass) {
        pass.write(frame.sceneUbo, rg::Usage::None);
        pass.sideEffect();
    }, [this, &frame, &root, children](rg::PassContext&) {
        auto& state = *m_diligent;
        frame.rootUniforms = frame.uniforms;
        SceneUniforms& viewUniforms = frame.rootUniforms;
        ApplyCellLighting(viewUniforms, root);
        m_viewSunShadows = root.sunScale > 0.0f;
        if (!m_viewSunShadows) viewUniforms.shadowParams.x = 0.0f;
        state.pImmediateContext->UpdateBuffer(state.pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &viewUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (!children) m_lighting.cullLights(state.pSceneUBO[m_currentFrame]);
    });
    if (!children) return;

    const Diligent::Uint64 bitsSize = gpu.pVisibilityBits->GetDesc().Size;
    if (!gpu.portalMainBits || gpu.portalMainBits->GetDesc().Size != bitsSize) gpu.portalMainBits = CreateStructuredBuffer(gpu.pDevice, "Portal Main Visibility Bits", sizeof(uint32_t), static_cast<Diligent::Uint32>(bitsSize / sizeof(uint32_t)));
    if (!gpu.portalMainBits) return;
    const rg::Handle mainBits = graph.importBuffer("portalMainBits", static_cast<void*>(gpu.portalMainBits.RawPtr()));
    graph.addPass("Portal Save Main Bits", [&](rg::PassBuilder& pass) {
        pass.read(frame.visibilityBits, rg::Usage::CopySource);
        pass.write(mainBits, rg::Usage::CopyDest);
    }, [this, bitsSize](rg::PassContext&) {
        auto& state = *m_diligent;
        CopyBufferFully(state.pImmediateContext, state.pVisibilityBits, state.portalMainBits, bitsSize);
    });
    for (const PortalSurface& surface : root.surfaces) {
        if (surface.child > 0) addPortalNodePasses(graph, frame, surface.child, objectCount);
    }
    graph.addPass("Portal Restore Main", [&](rg::PassBuilder& pass) {
        pass.read(mainBits, rg::Usage::CopySource);
        pass.write(frame.visibilityBits, rg::Usage::CopyDest);
        pass.write(frame.sceneUbo, rg::Usage::None);
    }, [this, &frame, &root, bitsSize](rg::PassContext&) {
        auto& state = *m_diligent;
        for (bool& valid : state.shadowHiZValid) valid = false;
        m_lighting.fitShadows(state.mainInput, frame.rootUniforms);
        m_viewSunShadows = root.sunScale > 0.0f;
        if (!m_viewSunShadows) frame.rootUniforms.shadowParams.x = 0.0f;
        selectMainCascades();
        CopyBufferFully(state.pImmediateContext, state.portalMainBits, state.pVisibilityBits, bitsSize);
        state.pImmediateContext->UpdateBuffer(state.pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &frame.rootUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_lighting.cullLights(state.pSceneUBO[m_currentFrame]);
    });
}

void Renderer::addPortalNodePasses(rg::RenderGraph& graph, FrameGraphData& frame, int index, uint32_t objectCount) {
    auto& gpu = *m_diligent;
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(index)];
    for (const PortalSurface& surface : node.surfaces) {
        if (surface.child > index) addPortalNodePasses(graph, frame, surface.child, objectCount);
    }

    auto beginTiming = [this, index, &node] {
        auto& state = *m_diligent;
        auto& snapshot = state.portalSnapshot[m_currentFrame][index];
        snapshot.label = node.label;
        snapshot.depth = node.depth;
        snapshot.timed = m_fullProfiling;
        if (m_fullProfiling) state.pImmediateContext->EndQuery(state.portalQueries[m_currentFrame][index][0]);
    };
    auto endTiming = [this, index] {
        auto& state = *m_diligent;
        if (m_fullProfiling) state.pImmediateContext->EndQuery(state.portalQueries[m_currentFrame][index][1]);
    };

    PortalHistory& history = AcquirePortalHistory(gpu, node.key, static_cast<uint64_t>(m_frameCounter) + 1);
    const Diligent::Uint64 bitsSize = gpu.pVisibilityBits->GetDesc().Size;
    std::string label = node.label.empty() ? std::format("portal {}", index) : node.label;
    graph.beginView(label);
    if (!PreparePortalHistory(gpu, history, bitsSize, m_renderWidth, m_renderHeight, m_maxMipLevel) || !m_lighting.hdr()) {
        graph.addPass("Portal View Skipped", [](rg::PassBuilder& pass) { pass.sideEffect(); }, [beginTiming, endTiming](rg::PassContext&) {
            beginTiming();
            endTiming();
        });
        graph.endView();
        return;
    }
    PortalHistory* historyPtr = &history;
    const rg::Handle bits = graph.importBuffer(rg::viewResourceName(node.key, "visibilityBits"), static_cast<void*>(history.bits.RawPtr()));

    graph.addPass("Portal View Setup", [&](rg::PassBuilder& pass) {
        pass.read(bits, rg::Usage::CopySource);
        pass.write(frame.visibilityBits, rg::Usage::CopyDest);
        pass.write(frame.sceneUbo, rg::Usage::None);
    }, [this, &frame, &node, index, historyPtr, bitsSize, beginTiming](rg::PassContext&) {
        auto& state = *m_diligent;
        auto* context = state.pImmediateContext.RawPtr();
        beginTiming();
        CopyBufferFully(context, historyPtr->bits, state.pVisibilityBits, bitsSize);
        SceneUniforms uniforms = MakePortalSceneUniforms(frame.uniforms, node);
        const glm::mat4 viewProjection = node.projection * node.view;
        FrameInput input = state.mainInput;
        input.view = node.view;
        input.projection = node.projection;
        input.cameraPosition = node.position;
        m_lighting.fitShadows(input, uniforms);
        m_viewSunShadows = node.sunScale > 0.0f;
        if (!m_viewSunShadows) uniforms.shadowParams.x = 0.0f;
        selectViewCascades(index, node, uniforms);
        uniforms.prevViewProjection = viewProjection;
        uniforms.giParams.z = 0.0f;
        uniforms.viewRect = portalViewRect(index);
        uniforms.depthRegion = portalDepthRegion(index);
        uploadCascadeCull();
        context->UpdateBuffer(state.pSceneUBO[m_currentFrame], 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_lighting.cullLights(state.pSceneUBO[m_currentFrame]);
    });

    addGeometryPasses(graph, frame, index, objectCount, historyPtr);
    addResolvePasses(graph, frame, index);

    FrameGraphData::ViewOutputs& outputs = frame.views[index];
    const rg::TextureDesc depthDesc = GraphTextureDesc(gpu.pSceneDepth->GetDesc());
    outputs.depth = graph.createTexture(rg::viewResourceName(node.key, "depth"), depthDesc);
    for (int target = 0; target < 3; ++target) {
        outputs.gbuffer[target] = {};
        Diligent::ITexture* source = m_lighting.gbuffer(target);
        if (!source) continue;
        Diligent::TextureDesc description = source->GetDesc();
        description.MipLevels = 1;
        description.BindFlags = Diligent::BIND_SHADER_RESOURCE;
        outputs.gbuffer[target] = graph.createTexture(rg::viewResourceName(node.key, std::format("gbuffer{}", target)), GraphTextureDesc(description));
    }
    graph.addPass("Portal Capture", [&](rg::PassBuilder& pass) {
        pass.read(frame.depth, rg::Usage::CopySource);
        pass.write(outputs.depth, rg::Usage::CopyDest);
        for (int target = 0; target < 3; ++target) {
            if (!outputs.gbuffer[target].valid()) continue;
            pass.read(frame.gbuffer[target], rg::Usage::CopySource);
            pass.write(outputs.gbuffer[target], rg::Usage::CopyDest);
        }
    }, [this, index, depth = outputs.depth, gbuffer = std::array<rg::Handle, 3>{outputs.gbuffer[0], outputs.gbuffer[1], outputs.gbuffer[2]}](rg::PassContext& context) {
        auto& state = *m_diligent;
        if (auto* texture = context.as<Diligent::ITexture>(depth)) {
            state.portalDepth[index] = texture;
            texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(state.pHiZSampler);
        }
        for (int target = 0; target < 3; ++target) {
            if (auto* texture = context.as<Diligent::ITexture>(gbuffer[static_cast<size_t>(target)])) {
                state.portalGBuffer[index][target] = texture;
                texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(state.pHiZSampler);
            }
        }
        CapturePortalDepth(state, index, portalViewRect(index));
        Diligent::ITexture* const targets[3] = {m_lighting.gbuffer(0), m_lighting.gbuffer(1), m_lighting.gbuffer(2)};
        CapturePortalGBuffer(state, index, targets, portalViewRect(index));
    });

    addShadePasses(graph, frame, index);

    graph.addPass("Portal Transparency", [&](rg::PassBuilder& pass) {
        pass.write(frame.accumulation, rg::Usage::RenderTarget);
        pass.write(frame.revealage, rg::Usage::RenderTarget);
        pass.write(frame.depth, rg::Usage::DepthWrite);
        pass.write(frame.hdr, rg::Usage::None);
        pass.read(frame.phases[kPhaseTransparent], rg::Usage::None);
    }, [this](rg::PassContext&) {
        if (m_diligent->sceneHasTranslucent) m_lighting.captureSceneColor();
        drawTransparency();
    });
    Diligent::TextureDesc colorDescription = m_lighting.hdr()->GetDesc();
    colorDescription.MipLevels = 1;
    colorDescription.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    outputs.color = graph.createTexture(rg::viewResourceName(node.key, "color"), GraphTextureDesc(colorDescription));
    graph.addPass("Portal Copy Out", [&](rg::PassBuilder& pass) {
        pass.read(frame.hdr, rg::Usage::CopySource);
        pass.write(outputs.color, rg::Usage::CopyDest);
    }, [this, index, color = outputs.color](rg::PassContext& context) {
        auto& state = *m_diligent;
        auto* texture = context.as<Diligent::ITexture>(color);
        if (!texture) return;
        state.portalColor[index] = texture;
        texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(state.pHiZSampler);
        CopyTextureRect(state.pImmediateContext, m_lighting.hdr(), texture, portalViewRect(index));
    });
    graph.addPass("Portal View End", [&](rg::PassBuilder& pass) {
        pass.read(frame.visibilityBits, rg::Usage::CopySource);
        pass.write(bits, rg::Usage::CopyDest);
    }, [this, historyPtr, bitsSize, endTiming](rg::PassContext&) {
        CopyBufferFully(m_diligent->pImmediateContext, m_diligent->pVisibilityBits, historyPtr->bits, bitsSize);
        historyPtr->previous = 1 - historyPtr->previous;
        endTiming();
    });
    outputs.rendered = true;
    graph.endView();
}
