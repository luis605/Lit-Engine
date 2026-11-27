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

constexpr size_t kStatsPhaseCount = 2;
constexpr float kPortalShimmerSpeed = 0.5f;
constexpr uint32_t kStatsReadbackBytes = kStatsPhaseCount * sizeof(uint32_t);

void InitPortalPipelineInfo(Diligent::GraphicsPipelineStateCreateInfo& info, const char* name, Diligent::TEXTURE_FORMAT colorFormat, Diligent::COMPARISON_FUNCTION depthFunction) {
    info.PSODesc.Name = name;
    info.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    info.GraphicsPipeline.NumRenderTargets = colorFormat == Diligent::TEX_FORMAT_UNKNOWN ? 0 : 1;
    info.GraphicsPipeline.RTVFormats[0] = colorFormat;
    info.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
    info.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    info.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    info.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
    info.GraphicsPipeline.DepthStencilDesc.DepthFunc = depthFunction;
}

glm::vec4 PortalScreenParams(const PortalViewNode& node, const DiligentData& gpu) {
    const auto& swapChainDesc = gpu.pSwapChain->GetDesc();
    const Diligent::Uint32 width = gpu.renderWidth > 0 ? gpu.renderWidth : swapChainDesc.Width;
    const Diligent::Uint32 height = gpu.renderHeight > 0 ? gpu.renderHeight : swapChainDesc.Height;
    return glm::vec4(node.projection[0][0], node.projection[1][1], static_cast<float>(width), static_cast<float>(height));
}

void SetPortalScissor(DiligentData& gpu, const glm::ivec4& rect) {
    const Diligent::Rect scissor(rect.x, rect.y, rect.x + rect.z, rect.y + rect.w);
    gpu.pImmediateContext->SetScissorRects(1, &scissor, gpu.renderWidth, gpu.renderHeight);
}

void SetPortalSurfaceGeometry(const PortalSurface& surface, glm::vec4& center, glm::vec4& axisU, glm::vec4& axisV, glm::vec4& normal) {
    center = glm::vec4(surface.center, 0.0f);
    axisU = PackPortalAxis(surface.axisU);
    axisV = PackPortalAxis(surface.axisV);
    normal = glm::vec4(surface.normal, 0.0f);
}

}

void Renderer::createPortalCompositePSO() {
    auto& gpu = *m_diligent;
    auto vertexShader = CompileShader(gpu.pDevice, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pixelShader = CompileShader(gpu.pDevice, "resources/shaders/portal_composite.frag", Diligent::SHADER_TYPE_PIXEL);
    gpu.pPortalPSO.Release();
    gpu.pPortalSRB.Release();
    gpu.pPortalColorPSO.Release();
    gpu.pPortalColorSRB.Release();
    if (!gpu.pPortalUBO) gpu.pPortalUBO = CreateUniformBuffer(gpu.pDevice, "Portal Surface", sizeof(PortalSurfaceUniforms), Diligent::USAGE_DEFAULT);
    if (!vertexShader || !pixelShader || !gpu.pPortalUBO) return;
    const auto variables = DescribeVariables({vertexShader, pixelShader}, {"u_child"});
    for (const bool writeDepth : {true, false}) {
        Diligent::GraphicsPipelineStateCreateInfo info;
        InitPortalPipelineInfo(info, writeDepth ? "Portal Composite PSO" : "Portal Composite Color PSO", Diligent::TEX_FORMAT_RGBA16_FLOAT, Diligent::COMPARISON_FUNC_GREATER);
        info.GraphicsPipeline.RasterizerDesc.ScissorEnable = true;
        info.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = writeDepth;
        auto& blend = info.GraphicsPipeline.BlendDesc.RenderTargets[0];
        blend.BlendEnable = true;
        blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
        blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
        blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
        info.pVS = vertexShader;
        info.pPS = pixelShader;
        UseVariables(info.PSODesc.ResourceLayout, variables);
        auto& pso = writeDepth ? gpu.pPortalPSO : gpu.pPortalColorPSO;
        auto& srb = writeDepth ? gpu.pPortalSRB : gpu.pPortalColorSRB;
        info.pPSOCache = litgpu::pipelineCache();
        gpu.pDevice->CreateGraphicsPipelineState(info, &pso);
        if (!pso) {
            Lit::Log::Error("Failed to create Portal Composite PSO");
            return;
        }
        CreateBoundSRB(pso, srb, {{"PortalSurfaceData", gpu.pPortalUBO}});
    }
}

static void CreatePortalResolvePSO(DiligentData& gpu) {
    auto vertexShader = CompileShader(gpu.pDevice, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
    auto resolveShader = CompileShader(gpu.pDevice, "resources/shaders/portal_resolve.frag", Diligent::SHADER_TYPE_PIXEL);
    gpu.pPortalResolvePSO.Release();
    gpu.pPortalResolveSRB.Release();
    if (!gpu.pPortalResolveUBO) gpu.pPortalResolveUBO = CreateUniformBuffer(gpu.pDevice, "Portal Resolve", sizeof(PortalResolveUniforms), Diligent::USAGE_DEFAULT);
    if (!vertexShader || !resolveShader || !gpu.pPortalResolveUBO) return;
    Diligent::GraphicsPipelineStateCreateInfo info;
    InitPortalPipelineInfo(info, "Portal Resolve PSO", Diligent::TEX_FORMAT_UNKNOWN, Diligent::COMPARISON_FUNC_ALWAYS);
    info.GraphicsPipeline.RasterizerDesc.ScissorEnable = true;
    info.GraphicsPipeline.NumRenderTargets = 4;
    info.GraphicsPipeline.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    info.GraphicsPipeline.RTVFormats[1] = Diligent::TEX_FORMAT_RGB10A2_UNORM;
    info.GraphicsPipeline.RTVFormats[2] = Diligent::TEX_FORMAT_RGBA8_UINT;
    info.GraphicsPipeline.RTVFormats[3] = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    info.GraphicsPipeline.BlendDesc.IndependentBlendEnable = true;
    auto& marker = info.GraphicsPipeline.BlendDesc.RenderTargets[3];
    marker.BlendEnable = true;
    marker.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    marker.DestBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    marker.BlendOpAlpha = Diligent::BLEND_OPERATION_MIN;
    marker.RenderTargetWriteMask = Diligent::COLOR_MASK_ALPHA;
    info.pVS = vertexShader;
    info.pPS = resolveShader;
    const auto variables = DescribeVariables({vertexShader, resolveShader}, {"u_childDepth", "u_sceneDepth", "u_childGBuffer0", "u_childGBuffer1", "u_childGBuffer2"});
    UseVariables(info.PSODesc.ResourceLayout, variables);
    info.pPSOCache = litgpu::pipelineCache();
    gpu.pDevice->CreateGraphicsPipelineState(info, &gpu.pPortalResolvePSO);
    if (!gpu.pPortalResolvePSO) {
        Lit::Log::Error("Failed to create Portal Resolve PSO");
        return;
    }
    CreateBoundSRB(gpu.pPortalResolvePSO, gpu.pPortalResolveSRB, {{"PortalDepthData", gpu.pPortalResolveUBO}});
}

void Renderer::createPortalPSO() {
    auto& gpu = *m_diligent;
    createPortalCompositePSO();
    CreatePortalResolvePSO(gpu);

    auto vertexShader = CompileShader(gpu.pDevice, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
    auto depthShader = CompileShader(gpu.pDevice, "resources/shaders/portal_depth.frag", Diligent::SHADER_TYPE_PIXEL);
    gpu.pPortalDepthPSO.Release();
    gpu.pPortalDepthSRB.Release();
    if (!gpu.pPortalDepthUBO) gpu.pPortalDepthUBO = CreateUniformBuffer(gpu.pDevice, "Portal Depth", sizeof(PortalDepthUniforms), Diligent::USAGE_DEFAULT);
    if (!vertexShader || !depthShader || !gpu.pPortalDepthUBO) return;
    Diligent::GraphicsPipelineStateCreateInfo depthInfo;
    InitPortalPipelineInfo(depthInfo, "Portal Depth PSO", Diligent::TEX_FORMAT_UNKNOWN, Diligent::COMPARISON_FUNC_ALWAYS);
    depthInfo.pVS = vertexShader;
    depthInfo.pPS = depthShader;
    const auto depthVariables = DescribeVariables({vertexShader, depthShader}, {"u_childDepth", "u_sceneDepth"});
    UseVariables(depthInfo.PSODesc.ResourceLayout, depthVariables);
    depthInfo.pPSOCache = litgpu::pipelineCache();
    gpu.pDevice->CreateGraphicsPipelineState(depthInfo, &gpu.pPortalDepthPSO);
    if (!gpu.pPortalDepthPSO) {
        Lit::Log::Error("Failed to create Portal Depth PSO");
        return;
    }
    CreateBoundSRB(gpu.pPortalDepthPSO, gpu.pPortalDepthSRB, {{"PortalDepthData", gpu.pPortalDepthUBO}});
}

bool Renderer::ensurePortalScratch() {
    auto& gpu = *m_diligent;
    const Diligent::TextureDesc sceneDesc = gpu.pSceneDepth->GetDesc();
    if (!gpu.portalDepthScratch || gpu.portalDepthScratch->GetDesc().Width != sceneDesc.Width || gpu.portalDepthScratch->GetDesc().Height != sceneDesc.Height) {
        Diligent::TextureDesc description = sceneDesc;
        description.Name = "Portal Depth Scratch";
        gpu.portalDepthScratch.Release();
        gpu.pDevice->CreateTexture(description, nullptr, &gpu.portalDepthScratch);
        if (gpu.portalDepthScratch) gpu.portalDepthScratch->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(gpu.pHiZSampler);
    }
    return gpu.portalDepthScratch != nullptr;
}

void Renderer::maskPortalView(int index) {
    auto& gpu = *m_diligent;
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(index)];
    if (!node.masked || !gpu.pPortalDepthPSO || !gpu.pPortalDepthSRB || !gpu.pPortalDepthUBO) return;
    auto* context = gpu.pImmediateContext.RawPtr();
    if (!ensurePortalScratch()) return;
    context->SetRenderTargets(0, nullptr, gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    SetSceneViewport(gpu);
    context->SetPipelineState(gpu.pPortalDepthPSO);
    PortalDepthUniforms uniforms{};
    uniforms.invView = glm::inverse(node.view);
    uniforms.center = glm::vec4(node.entryCenter, 0.0f);
    uniforms.axisU = PackPortalAxis(node.entryAxisU);
    uniforms.axisV = PackPortalAxis(node.entryAxisV);
    uniforms.normal = glm::vec4(node.entryNormal, 0.0f);
    uniforms.camera = glm::vec4(node.position, 1.0f);
    uniforms.params = PortalScreenParams(node, gpu);
    context->UpdateBuffer(gpu.pPortalDepthUBO, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto* scratchView = gpu.portalDepthScratch->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    SetDynamicVariable(gpu.pPortalDepthSRB, "u_childDepth", scratchView, Diligent::SHADER_TYPE_PIXEL);
    SetDynamicVariable(gpu.pPortalDepthSRB, "u_sceneDepth", scratchView, Diligent::SHADER_TYPE_PIXEL);
    context->CommitShaderResources(gpu.pPortalDepthSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawFullscreenTriangle(context);
    UnbindRenderTargets(context);
}

void Renderer::resolvePortalDepth(int index) {
    auto& gpu = *m_diligent;
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(index)];
    if (node.surfaces.empty() || !gpu.pPortalResolvePSO || !gpu.pPortalResolveSRB) return;
    auto hasChildData = [&](int child) { return child > 0 && gpu.portalDepth[child] && gpu.portalGBuffer[child][0] && gpu.portalGBuffer[child][1] && gpu.portalGBuffer[child][2]; };
    const bool anyChildDepth = std::ranges::any_of(node.surfaces, [&](const PortalSurface& surface) { return hasChildData(surface.child); });
    if (!anyChildDepth) return;
    auto* context = gpu.pImmediateContext.RawPtr();
    if (!ensurePortalScratch()) return;
    UnbindRenderTargets(context);
    glm::ivec2 readLow(static_cast<int>(m_renderWidth), static_cast<int>(m_renderHeight));
    glm::ivec2 readHigh(0);
    for (const PortalSurface& surface : node.surfaces) {
        if (!hasChildData(surface.child)) continue;
        const glm::ivec4 rect = portalSurfaceRect(surface);
        readLow = glm::min(readLow, glm::ivec2(rect));
        readHigh = glm::max(readHigh, glm::ivec2(rect) + glm::ivec2(rect.z, rect.w));
    }
    const Diligent::Box readBox(static_cast<Diligent::Uint32>(readLow.x), static_cast<Diligent::Uint32>(readHigh.x), static_cast<Diligent::Uint32>(readLow.y), static_cast<Diligent::Uint32>(readHigh.y));
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = gpu.pSceneDepth;
    copy.pSrcBox = &readBox;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = gpu.portalDepthScratch;
    copy.DstX = readBox.MinX;
    copy.DstY = readBox.MinY;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    Diligent::ITextureView* targets[4] = {litgpu::rtv(m_lighting.gbuffer(0)), litgpu::rtv(m_lighting.gbuffer(1)), litgpu::rtv(m_lighting.gbuffer(2)), litgpu::rtv(m_lighting.hdr())};
    context->SetRenderTargets(4, targets, gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    SetSceneViewport(gpu);
    context->SetPipelineState(gpu.pPortalResolvePSO);
    const glm::mat4 parentInvView = glm::inverse(node.view);
    for (const PortalSurface& surface : node.surfaces) {
        if (!hasChildData(surface.child)) continue;
        const PortalViewNode& child = m_portalFrame.nodes[static_cast<size_t>(surface.child)];
        PortalResolveUniforms uniforms{};
        uniforms.projection = node.projection;
        uniforms.childInvProjection = glm::inverse(child.projection);
        uniforms.viewProjection = node.projection * node.view;
        uniforms.invView = parentInvView;
        uniforms.normalToParent = glm::mat4(glm::mat3(parentInvView) * glm::mat3(child.view));
        SetPortalSurfaceGeometry(surface, uniforms.center, uniforms.axisU, uniforms.axisV, uniforms.normal);
        uniforms.camera = glm::vec4(node.position, gpu.mainInput.nearPlane);
        uniforms.params = PortalScreenParams(node, gpu);
        uniforms.shape = surface.shape;
        uniforms.edge = surface.edge;
        context->UpdateBuffer(gpu.pPortalResolveUBO, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        SetDynamicVariable(gpu.pPortalResolveSRB, "u_childDepth", gpu.portalDepth[surface.child]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        SetDynamicVariable(gpu.pPortalResolveSRB, "u_sceneDepth", gpu.portalDepthScratch->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        SetDynamicVariable(gpu.pPortalResolveSRB, "u_childGBuffer0", gpu.portalGBuffer[surface.child][0]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        SetDynamicVariable(gpu.pPortalResolveSRB, "u_childGBuffer1", gpu.portalGBuffer[surface.child][1]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        SetDynamicVariable(gpu.pPortalResolveSRB, "u_childGBuffer2", gpu.portalGBuffer[surface.child][2]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        context->CommitShaderResources(gpu.pPortalResolveSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        SetPortalScissor(gpu, portalSurfaceRect(surface));
        DrawFullscreenTriangle(context);
    }
    UnbindRenderTargets(context);
}

void SavePortalEffectsDepth(DiligentData& gpu) {
    const Diligent::TextureDesc sceneDesc = gpu.pSceneDepth->GetDesc();
    auto& saved = gpu.portalEffectsDepth;
    if (saved && (saved->GetDesc().Width != sceneDesc.Width || saved->GetDesc().Height != sceneDesc.Height)) saved.Release();
    if (!saved) {
        Diligent::TextureDesc description = sceneDesc;
        description.Name = "Portal Effects Depth";
        gpu.pDevice->CreateTexture(description, nullptr, &saved);
    }
    if (!saved) return;
    UnbindRenderTargets(gpu.pImmediateContext);
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = gpu.pSceneDepth;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = saved;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    gpu.pImmediateContext->CopyTexture(copy);
}

void RestorePortalEffectsDepth(DiligentData& gpu) {
    if (!gpu.portalEffectsDepth) return;
    UnbindRenderTargets(gpu.pImmediateContext);
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = gpu.portalEffectsDepth;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = gpu.pSceneDepth;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    gpu.pImmediateContext->CopyTexture(copy);
}

void Renderer::compositePortals(int index) {
    auto& gpu = *m_diligent;
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(index)];
    Diligent::IPipelineState* pso = gpu.portalCompositeWritesDepth ? gpu.pPortalPSO.RawPtr() : gpu.pPortalColorPSO.RawPtr();
    Diligent::IShaderResourceBinding* srb = gpu.portalCompositeWritesDepth ? gpu.pPortalSRB.RawPtr() : gpu.pPortalColorSRB.RawPtr();
    if (node.surfaces.empty() || !pso || !srb) return;
    auto* context = gpu.pImmediateContext.RawPtr();
    Diligent::ITextureView* hdr = litgpu::rtv(m_lighting.hdr());
    context->SetRenderTargets(1, &hdr, gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    SetSceneViewport(gpu);
    context->SetPipelineState(pso);
    const glm::mat4 viewProjection = node.projection * node.view;
    const glm::mat4 invView = glm::inverse(node.view);
    const glm::vec4 screenParams = PortalScreenParams(node, gpu);
    for (const PortalSurface& surface : node.surfaces) {
        const bool hasChild = surface.child >= 0 && gpu.portalColor[surface.child];
        PortalSurfaceUniforms uniforms{};
        uniforms.viewProjection = viewProjection;
        uniforms.invView = invView;
        SetPortalSurfaceGeometry(surface, uniforms.center, uniforms.axisU, uniforms.axisV, uniforms.normal);
        uniforms.camera = glm::vec4(node.position, hasChild ? 0.0f : 1.0f);
        uniforms.params = screenParams;
        uniforms.color = glm::vec4(surface.fallback, 1.0f);
        uniforms.shape = surface.shape;
        uniforms.edge = surface.edge;
        uniforms.rim = surface.rim;
        uniforms.misc = glm::vec4(gpu.mainInput.time, kPortalShimmerSpeed, gpu.mainInput.nearPlane, 0.0f);
        context->UpdateBuffer(gpu.pPortalUBO, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::ITexture* source = hasChild ? gpu.portalColor[surface.child].RawPtr() : gpu.pAccumulation.RawPtr();
        SetDynamicVariable(srb, "u_child", source->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        context->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        SetPortalScissor(gpu, portalSurfaceRect(surface));
        DrawFullscreenTriangle(context);
    }
}

void Renderer::recordPortalStats(int index) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    auto& staging = gpu.portalStaging[m_currentFrame][index];
    if (!staging) {
        Diligent::BufferDesc description;
        description.Name = "Portal Stats Readback";
        description.Usage = Diligent::USAGE_STAGING;
        description.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
        description.Size = kStatsReadbackBytes;
        gpu.pDevice->CreateBuffer(description, nullptr, &staging);
    }
    if (staging) {
        for (size_t phase = 0; phase < kStatsPhaseCount; ++phase) context->CopyBuffer(gpu.visibility[phase].visibleCounter, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, phase * sizeof(uint32_t), sizeof(uint32_t), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    auto& snapshot = gpu.portalSnapshot[m_currentFrame][index];
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(index)];
    snapshot.label = node.label;
    snapshot.depth = node.depth;
    snapshot.recorded = staging != nullptr;
}

void Renderer::readPortalStats() {
    auto& gpu = *m_diligent;
    m_portalStats.clear();
    for (int index = 0; index < kMaxPortalViews; ++index) {
        const auto& snapshot = gpu.portalSnapshot[m_currentFrame][index];
        if (!snapshot.recorded) continue;
        PortalViewStats stats;
        stats.label = snapshot.label;
        stats.depth = snapshot.depth;
        stats.sunCascades = snapshot.sunCascades;
        stats.sharedCascades = snapshot.sharedCascades;
        auto& staging = gpu.portalStaging[m_currentFrame][index];
        if (staging) {
            Diligent::MapHelper<uint32_t> mapped(gpu.pImmediateContext, staging, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
            if (mapped) stats.drawn = static_cast<const uint32_t*>(mapped)[0] + static_cast<const uint32_t*>(mapped)[1];
        }
        if (snapshot.timed) {
            Diligent::QueryDataTimestamp start{};
            Diligent::QueryDataTimestamp end{};
            auto& queries = gpu.portalQueries[m_currentFrame][index];
            const bool ready = queries[0] && queries[1] && queries[0]->GetData(&start, sizeof(start), false) && queries[1]->GetData(&end, sizeof(end), false);
            if (ready && end.Counter > start.Counter && end.Frequency > 0) stats.gpuMilliseconds = static_cast<double>(end.Counter - start.Counter) / static_cast<double>(end.Frequency) * 1000.0;
        }
        m_portalStats.push_back(std::move(stats));
    }
    if (!m_portalStats.empty() && !m_portalStats.front().label.empty()) m_portalStats.front().gpuMilliseconds = gpu.gpuTotalMilliseconds;
}

void Renderer::setObservationQuery(ObservationQuery query) {
    m_obsQuery = std::move(query);
    m_obsActive = true;
}

void Renderer::clearObservationQuery() {
    m_obsActive = false;
    m_obsReady.clear();
}

bool Renderer::takeObservation(ObservationReadback& out) {
    if (m_obsReady.empty()) return false;
    out = std::move(m_obsReady.front());
    m_obsReady.erase(m_obsReady.begin());
    return true;
}
