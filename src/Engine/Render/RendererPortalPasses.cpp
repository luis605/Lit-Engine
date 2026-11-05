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
    if (!gpu.pPortalUBO) gpu.pPortalUBO = CreateUniformBuffer(gpu.pDevice, "Portal Surface", sizeof(PortalSurfaceUniforms), Diligent::USAGE_DEFAULT);
    if (!vertexShader || !pixelShader || !gpu.pPortalUBO) return;
    Diligent::GraphicsPipelineStateCreateInfo info;
    InitPortalPipelineInfo(info, "Portal Composite PSO", Diligent::TEX_FORMAT_RGBA16_FLOAT, Diligent::COMPARISON_FUNC_LESS);
    info.pVS = vertexShader;
    info.pPS = pixelShader;
    const auto variables = DescribeVariables({vertexShader, pixelShader}, {"u_child"});
    UseVariables(info.PSODesc.ResourceLayout, variables);
    gpu.pDevice->CreateGraphicsPipelineState(info, &gpu.pPortalPSO);
    if (!gpu.pPortalPSO) {
        Lit::Log::Error("Failed to create Portal Composite PSO");
        return;
    }
    CreateBoundSRB(gpu.pPortalPSO, gpu.pPortalSRB, {{"PortalSurfaceData", gpu.pPortalUBO}});
}

void Renderer::createPortalPSO() {
    auto& gpu = *m_diligent;
    createPortalCompositePSO();

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
    if (node.surfaces.empty() || !gpu.pPortalDepthPSO || !gpu.pPortalDepthSRB) return;
    const bool anyChildDepth = std::ranges::any_of(node.surfaces, [&](const PortalSurface& surface) { return surface.child > 0 && gpu.portalDepth[surface.child]; });
    if (!anyChildDepth) return;
    auto* context = gpu.pImmediateContext.RawPtr();
    if (!ensurePortalScratch()) return;
    UnbindRenderTargets(context);
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = gpu.pSceneDepth;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = gpu.portalDepthScratch;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    context->SetRenderTargets(0, nullptr, gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    SetSceneViewport(gpu);
    context->SetPipelineState(gpu.pPortalDepthPSO);
    for (const PortalSurface& surface : node.surfaces) {
        if (surface.child <= 0 || !gpu.portalDepth[surface.child]) continue;
        const PortalViewNode& child = m_portalFrame.nodes[static_cast<size_t>(surface.child)];
        PortalDepthUniforms uniforms{};
        uniforms.projection = node.projection;
        uniforms.childInvProjection = glm::inverse(child.projection);
        uniforms.viewProjection = node.projection * node.view;
        uniforms.invView = glm::inverse(node.view);
        SetPortalSurfaceGeometry(surface, uniforms.center, uniforms.axisU, uniforms.axisV, uniforms.normal);
        uniforms.camera = glm::vec4(node.position, 0.0f);
        uniforms.params = PortalScreenParams(node, gpu);
        context->UpdateBuffer(gpu.pPortalDepthUBO, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        SetDynamicVariable(gpu.pPortalDepthSRB, "u_childDepth", gpu.portalDepth[surface.child]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        SetDynamicVariable(gpu.pPortalDepthSRB, "u_sceneDepth", gpu.portalDepthScratch->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        context->CommitShaderResources(gpu.pPortalDepthSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawFullscreenTriangle(context);
    }
    UnbindRenderTargets(context);
}

void Renderer::compositePortals(int index) {
    auto& gpu = *m_diligent;
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(index)];
    if (node.surfaces.empty() || !gpu.pPortalPSO || !gpu.pPortalSRB) return;
    auto* context = gpu.pImmediateContext.RawPtr();
    Diligent::ITextureView* hdr = litgpu::rtv(m_lighting.hdr());
    context->SetRenderTargets(1, &hdr, gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    SetSceneViewport(gpu);
    context->SetPipelineState(gpu.pPortalPSO);
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
        context->UpdateBuffer(gpu.pPortalUBO, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::ITexture* source = hasChild ? gpu.portalColor[surface.child].RawPtr() : gpu.pAccumulation.RawPtr();
        SetDynamicVariable(gpu.pPortalSRB, "u_child", source->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
        context->CommitShaderResources(gpu.pPortalSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
