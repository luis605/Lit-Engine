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

constexpr size_t kLightVectors = 4;
constexpr float kMinLightRange = 0.01f;
constexpr float kMinSpotCosineGap = 1e-3f;
constexpr float kMinSpotScaleGap = 1e-4f;
constexpr float kDirectionBias = 1e-6f;
constexpr float kSpotLightType = 2.0f;
constexpr float kPointLightType = 1.0f;
constexpr float kSpotFlagThreshold = 0.5f;
constexpr size_t kTransformUniformBytes = 256;
constexpr const char* kCullShader = "resources/shaders/cull.comp";

GpuLight PackLight(const glm::vec4& positionRange, const glm::vec4& colorIntensity, const glm::vec4& directionCone, const glm::vec4& params) {
    const bool spot = params.y > kSpotFlagThreshold;
    const float range = std::max(positionRange.w, kMinLightRange);
    const float candela = colorIntensity.w / (4.0f * kPi);
    const float cosOuter = directionCone.w;
    const float cosInner = std::max(params.x, cosOuter + kMinSpotCosineGap);
    const float spotScale = 1.0f / std::max(cosInner - cosOuter, kMinSpotScaleGap);
    GpuLight light{};
    light.positionRange = glm::vec4(glm::vec3(positionRange), range);
    light.colorType = glm::vec4(glm::vec3(colorIntensity) * candela, spot ? kSpotLightType : kPointLightType);
    light.directionCos = glm::vec4(glm::normalize(glm::vec3(directionCone) + glm::vec3(0.0f, 0.0f, kDirectionBias)), cosOuter);
    light.params = spot ? glm::vec4(spotScale, -cosOuter * spotScale, 1.0f / (range * range), -1.0f) : glm::vec4(std::max(params.z, 0.0f), std::max(params.w, 0.0f), 1.0f / (range * range), -1.0f);
    return light;
}

}

void Renderer::createPipelines() {
    createSurfacePipelines();
    createTransparentPSO();
    createTransformPSO();
    createHiZPSO();
    createCullingPSO();
    createCommandGenPSO();
    createPrefixSumPSO();
    createScatterPSO();
    createDispatchArgsPSO();
    createApplyDirtyPSO();
    createTransparentCullPSO();
    createDebugDepthPSO();
    createDepthUpscalePSO();
    createDebugLinePSO();
    createPortalPSO();
}

void Renderer::reloadShaders() {
    if (!m_initialized || !m_diligent) return;
    if (!AllShadersCompile(m_diligent->pDevice)) {
        Lit::Log::Error("Shader reload skipped: fix the compile errors above and save again.");
        return;
    }
    m_diligent->pImmediateContext->WaitForIdle();
    createPipelines();
    m_lighting.reloadShaders();
    m_custom.reload();
    reallocateBuffers(m_maxObjects);
    Lit::Log::Info("Shaders reloaded");
}

bool Renderer::pipelinesReady() const {
    const auto& gpu = *m_diligent;
    const bool computeReady = gpu.pTransformPSO && gpu.pCullingPSO && gpu.pVisibilityCompactPSO && gpu.pRetestCullingPSO && gpu.pPrefixSumPSO && gpu.pScatterPSO && gpu.pCommandGenPSO && gpu.pDispatchArgsPSO && gpu.pHiZMipmapPSO && gpu.pHiZFromDepthPSO && gpu.pApplyDirtyPSO && gpu.pTransparentCullPSO;
    const bool surfacesReady = !gpu.surfaces.empty() && gpu.surfaces.front().valid;
    return computeReady && surfacesReady && gpu.pCompositePSO && gpu.pSceneDepth && gpu.pAccumulation && gpu.pRevealage;
}

void Renderer::setLights(bool enabled, const std::array<glm::vec4, 2>& directional, const std::vector<glm::vec4>& packed) {
    std::vector<GpuLight> lights;
    lights.reserve(packed.size() / kLightVectors);
    for (size_t offset = 0; offset + kLightVectors - 1 < packed.size(); offset += kLightVectors) lights.push_back(PackLight(packed[offset], packed[offset + 1], packed[offset + 2], packed[offset + 3]));
    m_lighting.setLights(lights);
    const bool hasDirectional = enabled && glm::length(glm::vec3(directional[1])) > 0.0f;
    m_lighting.setDirectionalLight(hasDirectional, glm::vec3(directional[0]), glm::vec3(directional[1]));
}

void Renderer::setReflectionScene(const ReflectionScene& scene) { m_lighting.setReflectionScene(scene); }

void Renderer::bakeProbes() { m_lighting.bakeProbes(); }

void Renderer::createTransformPSO() {
    m_diligent->pTransformPSO = CreateComputePipeline(m_diligent->pDevice, "Transform PSO", "resources/shaders/transform.comp");
    m_diligent->pTransformUniforms = CreateUniformBuffer(m_diligent->pDevice, "Transform Uniforms", kTransformUniformBytes, Diligent::USAGE_DEFAULT);
}

void Renderer::createTransparentCullPSO() {
    const Diligent::ShaderMacro transparentMacros[] = {
        {"TRANSPARENT", "1"}
    };
    m_diligent->pTransparentCullPSO = CreateComputePipeline(m_diligent->pDevice, "Transparent Cull PSO", kCullShader, {"u_hizTexture"}, Diligent::ShaderMacroArray{transparentMacros, 1});
}

void Renderer::createCullingPSO() {
    auto& gpu = *m_diligent;
    gpu.pCullingPSO = CreateComputePipeline(gpu.pDevice, "Culling PSO", kCullShader, {"u_hizTexture"});
    gpu.pVisibilityCompactPSO = CreateComputePipeline(gpu.pDevice, "Visibility Compact PSO", "resources/shaders/visibility_compact.comp");
    const Diligent::ShaderMacro retestMacros[] = {
        {"RETEST", "1"}
    };
    gpu.pRetestCullingPSO = CreateComputePipeline(gpu.pDevice, "Retest Culling PSO", kCullShader, {"u_hizTexture"}, Diligent::ShaderMacroArray{retestMacros, 1});
    const Diligent::ShaderMacro shadowMacros[] = {
        {"SHADOW",      "1"},
        {"SHADOW_LATE", "1"}
    };
    gpu.pShadowCullPSO = CreateComputePipeline(gpu.pDevice, "Shadow Culling PSO", kCullShader, {"u_hizTexture", "u_shadowHiz"}, Diligent::ShaderMacroArray{shadowMacros, 1});
    gpu.pShadowLateCullPSO = CreateComputePipeline(gpu.pDevice, "Shadow Late Culling PSO", kCullShader, {"u_hizTexture", "u_shadowHiz"}, Diligent::ShaderMacroArray{shadowMacros, 2});
    const Diligent::ShaderMacro localMacros[] = {
        {"SHADOW",       "1"},
        {"SHADOW_LATE",  "1"},
        {"LOCAL_SHADOW", "1"}
    };
    gpu.pLocalShadowCullPSO = CreateComputePipeline(gpu.pDevice, "Local Shadow Culling PSO", kCullShader, {"u_hizTexture", "u_shadowHiz"}, Diligent::ShaderMacroArray{localMacros, 3});
}

void Renderer::createCommandGenPSO() {
    m_diligent->pCommandGenPSO = CreateComputePipeline(m_diligent->pDevice, "Command Gen PSO", "resources/shaders/command_gen.comp");
    m_diligent->pCommandGenConstants = CreateUniformBuffer(m_diligent->pDevice, "Command Gen Constants", sizeof(CommandGenUniforms));
}

void Renderer::createPrefixSumPSO() {
    m_diligent->pPrefixSumPSO = CreateComputePipeline(m_diligent->pDevice, "Prefix Sum PSO", "resources/shaders/prefix_sum.comp");
    m_diligent->pPrefixSumConstants = CreateUniformBuffer(m_diligent->pDevice, "Prefix Sum Constants", sizeof(PrefixSumConstants));
}

void Renderer::createScatterPSO() {
    m_diligent->pScatterPSO = CreateComputePipeline(m_diligent->pDevice, "Scatter PSO", "resources/shaders/scatter.comp");
    m_diligent->pScatterConstants = CreateUniformBuffer(m_diligent->pDevice, "Scatter Constants", sizeof(ScatterConstants));
}

void Renderer::createDispatchArgsPSO() {
    m_diligent->pDispatchArgsPSO = CreateComputePipeline(m_diligent->pDevice, "Dispatch Args PSO", "resources/shaders/dispatch_args.comp");
    m_diligent->pDispatchArgsConstants = CreateUniformBuffer(m_diligent->pDevice, "Dispatch Args Constants", sizeof(DispatchArgsConstants), Diligent::USAGE_DEFAULT);
    m_diligent->pObjectDispatchArgsConstants = CreateUniformBuffer(m_diligent->pDevice, "Object Dispatch Args Constants", sizeof(DispatchArgsConstants), Diligent::USAGE_DEFAULT);
}

void Renderer::createApplyDirtyPSO() {
    m_diligent->pApplyDirtyPSO = CreateComputePipeline(m_diligent->pDevice, "Apply Dirty PSO", "resources/shaders/apply_dirty.comp");
    m_diligent->pApplyDirtyConstants = CreateUniformBuffer(m_diligent->pDevice, "Apply Dirty Constants", sizeof(ApplyDirtyConstants));
}

void Renderer::createTransparentPSO() {
    auto& gpu = *m_diligent;
    auto vertexShader = CompileShader(gpu.pDevice, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pixelShader = CompileShader(gpu.pDevice, "resources/shaders/transparent_composite.frag", Diligent::SHADER_TYPE_PIXEL);
    gpu.pCompositePSO.Release();
    gpu.pCompositeSRB.Release();
    if (!vertexShader || !pixelShader) return;

    Diligent::GraphicsPipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = "Transparent Composite PSO";
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    createInfo.GraphicsPipeline.NumRenderTargets = 1;
    createInfo.GraphicsPipeline.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    createInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
    createInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    createInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    auto& blend = createInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    createInfo.pVS = vertexShader;
    createInfo.pPS = pixelShader;
    const auto variables = DescribeVariables({vertexShader, pixelShader}, {"u_accumulation", "u_revealage"});
    UseVariables(createInfo.PSODesc.ResourceLayout, variables);
    gpu.pDevice->CreateGraphicsPipelineState(createInfo, &gpu.pCompositePSO);
    if (!gpu.pCompositePSO) {
        Lit::Log::Error("Failed to create Transparent Composite PSO");
        return;
    }
    gpu.pCompositePSO->CreateShaderResourceBinding(&gpu.pCompositeSRB, true);
}

void Renderer::drawTransparency() {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    Diligent::ITextureView* targets[] = {gpu.pAccumulation->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET), gpu.pRevealage->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)};
    auto* depthView = gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    context->SetRenderTargets(2, targets, depthView, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float noCoverage[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float fullRevealage[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    context->ClearRenderTarget(targets[0], noCoverage, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearRenderTarget(targets[1], fullRevealage, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    drawVisibilityPhase(kPhaseTransparent);

    Diligent::ITextureView* hdrTarget = litgpu::rtv(m_lighting.hdr());
    context->SetRenderTargets(1, &hdrTarget, depthView, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    SetDynamicVariable(gpu.pCompositeSRB, "u_accumulation", gpu.pAccumulation->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
    SetDynamicVariable(gpu.pCompositeSRB, "u_revealage", gpu.pRevealage->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SHADER_TYPE_PIXEL);
    context->SetPipelineState(gpu.pCompositePSO);
    context->CommitShaderResources(gpu.pCompositeSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawFullscreenTriangle(context);
}
