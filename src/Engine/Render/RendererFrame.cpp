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
import Engine.Render.governor;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

constexpr float kMaxFrameDelta = 0.25f;
constexpr float kProfileLogIntervalSeconds = 0.5f;
constexpr size_t kCapacityGrowthDivisor = 4;
constexpr size_t kCapacityHeadroom = 1024;
constexpr size_t kMinFallbackObjects = 1024;
constexpr float kClearColor[4] = {0.05f, 0.06f, 0.08f, 1.0f};

void RemoveResources(CustomFrame& frame, std::initializer_list<std::string_view> names) {
    std::erase_if(frame.resources, [&](const NamedResource& resource) { return std::ranges::find(names, std::string_view(resource.name)) != names.end(); });
}

}

void Renderer::beginPass(GpuPass pass) {
    if (!m_fullProfiling) return;
    m_diligent->passRecorded[m_currentFrame][static_cast<size_t>(pass)] = true;
    m_diligent->pImmediateContext->EndQuery(m_diligent->passQueries[static_cast<size_t>(pass)][m_currentFrame][0]);
}

void Renderer::endPass(GpuPass pass) {
    if (m_fullProfiling) m_diligent->pImmediateContext->EndQuery(m_diligent->passQueries[static_cast<size_t>(pass)][m_currentFrame][1]);
}

void Renderer::signalFrameFence() {
    auto& gpu = *m_diligent;
    gpu.CurrentFenceValue++;
    gpu.pImmediateContext->EnqueueSignal(gpu.pFences[m_currentFrame], gpu.CurrentFenceValue);
    gpu.FenceValues[m_currentFrame] = gpu.CurrentFenceValue;
}

void Renderer::ensureCapacity(uint32_t objectCount) {
    if (objectCount <= m_maxObjects) m_failedCapacity = 0;
    if (objectCount <= m_maxObjects || objectCount == m_failedCapacity || !m_initialized) return;

    m_diligent->pImmediateContext->WaitForIdle();
    const size_t previous = m_maxObjects;
    const size_t budget = objectBudget();
    const size_t exact = static_cast<size_t>(objectCount) + kCapacityHeadroom;
    const size_t wanted = std::min(std::max<size_t>(static_cast<size_t>(objectCount) + static_cast<size_t>(objectCount) / kCapacityGrowthDivisor, exact), budget);
    if (objectCount <= budget && (reallocateBuffers(wanted) || reallocateBuffers(exact))) return;

    m_failedCapacity = objectCount;
    Lit::Log::Error("The scene has {} renderable slots but this GPU fits about {}; the viewport is paused until entities are removed.", objectCount, budget);
    if (m_maxObjects == previous && m_diligent->visibility[kPhaseOpaque].visibleEntries) return;
    size_t fallback = previous;
    while (fallback > kMinFallbackObjects && !reallocateBuffers(fallback)) fallback = fallback / 10 * 9;
}

void Renderer::pollObservation() {
    if (!m_obsActive) return;
    auto& gpu = *m_diligent;
    if (!gpu.observationTried) {
        gpu.observationTried = true;
        gpu.observation.init(gpu.pDevice.RawPtr());
    }
    ObservationReadback readback;
    if (gpu.observation.poll(gpu.pImmediateContext.RawPtr(), m_currentFrame, readback)) m_obsReady.push_back(std::move(readback));
}

void Renderer::beginFrame(float deltaTime) {
    auto& gpu = *m_diligent;
    m_currentFrame = (m_currentFrame + 1) % NUM_FRAMES_IN_FLIGHT;

    const double fenceWaitStart = glfwGetTime();
    gpu.pFences[m_currentFrame]->Wait(gpu.FenceValues[m_currentFrame]);
    gpu.fenceWaitMilliseconds = (glfwGetTime() - fenceWaitStart) * 1000.0;

    pollObservation();
    collectTimings(deltaTime);
    readPortalStats();
    for (auto& snapshot : gpu.portalSnapshot[m_currentFrame]) snapshot.recorded = false;
    std::ranges::fill(gpu.passRecorded[m_currentFrame], false);
}

void Renderer::collectTimings(float deltaTime) {
    auto& gpu = *m_diligent;
    if (gpu.QueryReady[m_currentFrame]) {
        ReadGpuTimings(gpu, static_cast<int>(m_currentFrame));
        m_custom.readTimings(m_currentFrame);
        for (const PassInfo& pass : m_custom.passes()) {
            if (pass.kind != PassKind::Effect || pass.stage != EffectStage::Ldr) gpu.gpuTotalMilliseconds += pass.gpuMilliseconds;
        }
        gpu.profileLogTimer += deltaTime;
        if (m_fullProfiling && gpu.profileLogTimer >= kProfileLogIntervalSeconds) {
            LogGpuTimings(gpu);
            std::string customLine;
            for (const PassInfo& pass : m_custom.passes()) {
                if (pass.enabled && pass.valid) customLine += std::format("{}{}: {:.3f} ms", customLine.empty() ? "  Custom " : " | ", pass.name, pass.gpuMilliseconds);
            }
            if (!customLine.empty()) Lit::Log::Info("{}", customLine);
            gpu.profileLogTimer = 0.0f;
        }
    }
    m_custom.resetTimers(m_currentFrame);
}

void Renderer::drawEmptyFrame(const Camera& camera) {
    auto& gpu = *m_diligent;
    gpu.QueryReady[m_currentFrame] = false;
    bindBackBuffer(false);
    gpu.pImmediateContext->ClearRenderTarget(gpu.pSwapChain->GetCurrentBackBufferRTV(), kClearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    gpu.pImmediateContext->ClearDepthStencil(gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    drawOverlays(camera);
    signalFrameFence();
}

FrameInput Renderer::makeFrameInput(const Camera& camera, float currentFrameTime, float deltaTime) {
    const auto& swapChainDesc = m_diligent->pSwapChain->GetDesc();
    FrameInput frameInput;
    frameInput.view = camera.getViewMatrix();
    frameInput.projection = camera.getProjectionMatrix();
    frameInput.cameraPosition = camera.getPosition();
    frameInput.nearPlane = camera.getNearPlane();
    frameInput.farPlane = camera.getFarPlane();
    frameInput.fovY = camera.getFov();
    frameInput.aspect = static_cast<float>(swapChainDesc.Width) / static_cast<float>(std::max<Diligent::Uint32>(swapChainDesc.Height, 1));
    frameInput.time = currentFrameTime;
    frameInput.deltaTime = std::clamp(deltaTime, 0.0f, kMaxFrameDelta);
    frameInput.frame = ++m_frameCounter;
    frameInput.width = m_renderWidth;
    frameInput.height = m_renderHeight;
    return frameInput;
}

void Renderer::renderPortalPrepass(const SceneUniforms& sceneUniforms, uint32_t objectCount) {
    auto& gpu = *m_diligent;
    SceneUniforms viewUniforms = sceneUniforms;
    const PortalViewNode& root = m_portalFrame.nodes[0];
    viewUniforms.sunIlluminance = glm::vec4(glm::vec3(viewUniforms.sunIlluminance) * root.sunScale, viewUniforms.sunIlluminance.w);
    viewUniforms.skyGradient[1].w *= root.ambientScale;
    gpu.pImmediateContext->UpdateBuffer(gpu.pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &viewUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (!root.surfaces.empty()) renderPortalViews(sceneUniforms, viewUniforms, objectCount);
}

void Renderer::renderOpaqueGeometry(uint32_t objectCount, bool portalsActive) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const int previousFrame = static_cast<int>((m_currentFrame + NUM_FRAMES_IN_FLIGHT - 1) % NUM_FRAMES_IN_FLIGHT);

    uploadCascadeCull();
    beginPass(GpuPass::Cull);
    cullVisibilityPhase(kPhaseOpaque, objectCount, previousFrame);
    endPass(GpuPass::Cull);
    beginPass(GpuPass::CommandGen);
    buildVisibilityDraws(kPhaseOpaque);
    endPass(GpuPass::CommandGen);

    bindSceneTargets(true);
    beginPass(GpuPass::OpaqueDraw);
    drawVisibilityPhase(kPhaseOpaque);
    endPass(GpuPass::OpaqueDraw);

    UnbindRenderTargets(context);
    beginPass(GpuPass::HiZ);
    buildHiZ();
    endPass(GpuPass::HiZ);

    beginPass(GpuPass::RetestCull);
    cullVisibilityPhase(kPhaseRetest, objectCount, static_cast<int>(m_currentFrame));
    endPass(GpuPass::RetestCull);
    beginPass(GpuPass::RetestCommandGen);
    buildVisibilityDraws(kPhaseRetest);
    endPass(GpuPass::RetestCommandGen);

    bindSceneTargets(false);
    beginPass(GpuPass::RetestDraw);
    drawVisibilityPhase(kPhaseRetest);
    endPass(GpuPass::RetestDraw);
    if (portalsActive) recordPortalStats(0);
    if (m_obsActive && gpu.observation.ready()) {
        UnbindRenderTargets(context);
        gpu.observation.record(context, gpu.pSceneDepth.RawPtr(), m_currentFrame, m_obsQuery);
    }
}

void Renderer::renderShadowsAndTransparentCull(uint32_t objectCount) {
    beginPass(GpuPass::Shadows);
    drawShadows(objectCount);
    endPass(GpuPass::Shadows);

    beginPass(GpuPass::TransparentCull);
    cullVisibilityPhase(kPhaseTransparent, objectCount, static_cast<int>(m_currentFrame));
    endPass(GpuPass::TransparentCull);
    beginPass(GpuPass::TransparentCommandGen);
    buildVisibilityDraws(kPhaseTransparent);
    endPass(GpuPass::TransparentCommandGen);
}

void Renderer::shadeScene(bool portalsActive) {
    auto& gpu = *m_diligent;
    UnbindRenderTargets(gpu.pImmediateContext);
    if (portalsActive) resolvePortalDepth(0);
    const std::pair<GpuPass, int> shadeStages[] = {
        {GpuPass::AmbientOcclusion, 0},
        {GpuPass::Lighting,         1},
        {GpuPass::Reflections,      2}
    };
    for (const auto& [pass, stage] : shadeStages) {
        beginPass(pass);
        m_lighting.shade(gpu.pSceneDepth, gpu.pSceneUBO[m_currentFrame], stage);
        endPass(pass);
    }

    if (portalsActive) compositePortals(0);

    CustomFrame lit = customFrame();
    m_custom.runCompute(ComputeStage::AfterLighting, lit);
    RemoveResources(lit, {"u_depth", "u_hdr"});
    m_custom.runDraws(lit, litgpu::rtv(m_lighting.hdr()), gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL));
}

void Renderer::renderTransparentAndCustomEffects() {
    beginPass(GpuPass::TransparentDraw);
    m_lighting.captureSceneColor();
    drawTransparency();
    endPass(GpuPass::TransparentDraw);

    const CustomFrame post = customFrame();
    m_custom.runCompute(ComputeStage::BeforePost, post);
    if (!m_custom.hasEffects()) return;
    CustomFrame effects = post;
    RemoveResources(effects, {"u_hdr"});
    Diligent::ITexture* targets[2] = {m_lighting.hdrTarget(m_lighting.hdrIndex()), m_lighting.hdrTarget(1 - m_lighting.hdrIndex())};
    const int result = m_custom.runEffects(effects, targets, 0);
    if (result == 1) m_lighting.setHdrIndex(1 - m_lighting.hdrIndex());
}

void Renderer::finishFrame(const FrameInput& frameInput, const Camera& camera) {
    auto& gpu = *m_diligent;
    beginPass(GpuPass::Post);
    m_lighting.finish(gpu.pSceneUBO[m_currentFrame], gpu.pSwapChain->GetCurrentBackBufferRTV(), frameInput.deltaTime, [&](Diligent::ITexture* ldr) {
        if (!m_custom.hasEffects(EffectStage::Ldr)) return;
        CustomFrame effects = customFrame();
        RemoveResources(effects, {"u_hdr"});
        m_custom.runLdrEffects(effects, ldr);
    });
    endPass(GpuPass::Post);
    bindBackBuffer();

    beginPass(GpuPass::Ui);
    drawOverlays(camera);
    endPass(GpuPass::Ui);
    gpu.QueryReady[m_currentFrame] = m_fullProfiling;

    signalFrameFence();
}

void Renderer::drawScene(SceneDatabase& sceneDatabase, const Camera& camera) {
    auto& gpu = *m_diligent;
    const float currentFrameTime = static_cast<float>(glfwGetTime());
    const float deltaTime = currentFrameTime - m_lastFrameTime;
    m_lastFrameTime = currentFrameTime;
    m_lastDeltaTime = deltaTime;

    const auto objectCount = static_cast<uint32_t>(sceneDatabase.renderables.size());
    ensureCapacity(objectCount);
    const bool overCapacity = objectCount > m_maxObjects;

    beginFrame(deltaTime);
    updateQualityScale(deltaTime);
    syncVersions(sceneDatabase);

    if (!m_initialized || gMeshInfos.empty() || !pipelinesReady()) {
        gpu.QueryReady[m_currentFrame] = false;
        return;
    }
    if (objectCount == 0 || overCapacity) {
        drawEmptyFrame(camera);
        return;
    }

    beginPass(GpuPass::Upload);
    const TransformPlan plan = uploadSceneData(sceneDatabase);
    endPass(GpuPass::Upload);

    beginPass(GpuPass::Transform);
    uploadMeshInfo();
    dispatchTransforms(sceneDatabase, plan);
    endPass(GpuPass::Transform);

    const FrameInput frameInput = makeFrameInput(camera, currentFrameTime, deltaTime);
    SceneUniforms sceneUniforms{};
    m_lighting.beginFrame(frameInput, sceneUniforms);
    gpu.mainInput = frameInput;
    gpu.pImmediateContext->UpdateBuffer(gpu.pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &sceneUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (gpu.surfaceBindingVersion != m_lighting.bindingVersion()) rebindSurfaceResources();

    m_custom.runCompute(ComputeStage::BeforeScene, customFrame());

    beginPass(GpuPass::Sky);
    m_lighting.prepare(gpu.pSceneUBO[m_currentFrame]);
    endPass(GpuPass::Sky);

    const bool portalsActive = !m_portalFrame.nodes.empty();
    if (portalsActive) renderPortalPrepass(sceneUniforms, objectCount);

    renderOpaqueGeometry(objectCount, portalsActive);
    renderShadowsAndTransparentCull(objectCount);
    shadeScene(portalsActive);
    renderTransparentAndCustomEffects();
    finishFrame(frameInput, camera);
}
