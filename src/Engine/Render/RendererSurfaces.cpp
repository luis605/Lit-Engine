module;

#include <thread>
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

enum class SurfaceKind { GBuffer, Forward, Shadow };

using Macros = std::vector<std::pair<std::string, std::string>>;

constexpr float kShadowSlopeBias = 1.75f;
constexpr int kSurfaceVariantCount = 6;
constexpr int kGBufferTargetCount = 4;
constexpr int kForwardTargetCount = 2;
constexpr uint32_t kSurfaceVertexPositionComponents = 3;
constexpr uint32_t kSurfaceVertexUvComponents = 2;

std::pair<Macros, Macros> SurfaceMacros(const SurfaceSource& source, SurfaceKind kind, bool point) {
    Macros vertexMacros;
    Macros pixelMacros;
    if (kind == SurfaceKind::Forward) {
        vertexMacros.push_back({"TRANSPARENT", "1"});
        pixelMacros.push_back({"TRANSPARENT", "1"});
    }
    if (kind == SurfaceKind::Shadow) vertexMacros.push_back({"SHADOW_PASS", "1"});
    if (point) pixelMacros.push_back({"POINT_SPRITE", "1"});
    if (source.hasVertex && !point) vertexMacros.push_back({"LIT_USER_VERTEX", "1"});
    if (source.hasSurface) pixelMacros.push_back({"LIT_USER_SURFACE", "1"});
    return {std::move(vertexMacros), std::move(pixelMacros)};
}

void ConfigureTargets(Diligent::GraphicsPipelineDesc& pipeline, SurfaceKind kind) {
    if (kind == SurfaceKind::GBuffer) {
        pipeline.NumRenderTargets = kGBufferTargetCount;
        pipeline.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
        pipeline.RTVFormats[1] = Diligent::TEX_FORMAT_RGB10A2_UNORM;
        pipeline.RTVFormats[2] = Diligent::TEX_FORMAT_RGBA8_UINT;
        pipeline.RTVFormats[3] = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    } else if (kind == SurfaceKind::Forward) {
        pipeline.NumRenderTargets = kForwardTargetCount;
        pipeline.RTVFormats[0] = kAccumulationFormat;
        pipeline.RTVFormats[1] = kRevealageFormat;
        pipeline.BlendDesc.IndependentBlendEnable = true;
        auto& accumulation = pipeline.BlendDesc.RenderTargets[0];
        accumulation.BlendEnable = true;
        accumulation.SrcBlend = accumulation.DestBlend = accumulation.SrcBlendAlpha = accumulation.DestBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        auto& revealage = pipeline.BlendDesc.RenderTargets[1];
        revealage.BlendEnable = true;
        revealage.SrcBlend = revealage.SrcBlendAlpha = Diligent::BLEND_FACTOR_ZERO;
        revealage.DestBlend = revealage.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_COLOR;
    } else {
        pipeline.NumRenderTargets = 0;
        pipeline.RasterizerDesc.DepthClipEnable = false;
        pipeline.RasterizerDesc.SlopeScaledDepthBias = kShadowSlopeBias;
        pipeline.RasterizerDesc.DepthBias = 0;
    }
}

litgpu::PipelineLayout SurfaceLayout(SurfaceKind kind, Diligent::IShader* vertexShader, Diligent::IShader* pixelShader) {
    if (kind == SurfaceKind::Shadow) return litgpu::makeLayout({vertexShader});
    return litgpu::makeLayout({vertexShader, pixelShader}, {{"u_colorTextures", litgpu::anisotropicWrap()}, {"u_dataTextures", litgpu::anisotropicWrap()}, {"u_prefilteredEnv", litgpu::linearClamp()}, {"u_dfgLut", litgpu::linearClamp()}, {"u_shadowMap", litgpu::shadowCompare()}, {"u_shadowDepth", litgpu::pointClamp()}, {"u_localShadowMap", litgpu::shadowCompare()}, {"u_sceneColor", litgpu::linearClamp()}});
}

const char* KindSuffix(SurfaceKind kind) {
    switch (kind) {
        case SurfaceKind::GBuffer:
            return " gbuffer";
        case SurfaceKind::Forward:
            return " forward";
        default:
            return " shadow";
    }
}

Diligent::RefCntAutoPtr<Diligent::IPipelineState> BuildSurfacePso(DiligentData& gpu, const std::string& name, const std::string& path, const SurfaceSource& source, SurfaceKind kind, bool point, bool twoSided) {
    const auto [vertexMacros, pixelMacros] = SurfaceMacros(source, kind, point);
    const std::string vertexPath = point ? "resources/shaders/surface/surface_point.vert" : "resources/shaders/surface/surface.vert";
    const std::string vertexTemplate = litgpu::readText(vertexPath);
    const std::string pixelTemplate = litgpu::readText("resources/shaders/surface/surface.frag");
    if (vertexTemplate.empty() || pixelTemplate.empty()) return {};
    auto vertexShader = litgpu::compileSource(gpu.pDevice, name + (point ? " points.vert" : ".vert"), litgpu::injectUserCode(vertexTemplate, source.vertexShared + source.vertexCode), Diligent::SHADER_TYPE_VERTEX, vertexMacros, path);
    if (!vertexShader) return {};
    Diligent::RefCntAutoPtr<Diligent::IShader> pixelShader;
    if (kind != SurfaceKind::Shadow) {
        pixelShader = litgpu::compileSource(gpu.pDevice, name + (point ? " points.frag" : ".frag"), litgpu::injectUserCode(pixelTemplate, source.shared + source.fragmentCode), Diligent::SHADER_TYPE_PIXEL, pixelMacros, path);
        if (!pixelShader) return {};
    }
    Diligent::GraphicsPipelineStateCreateInfo createInfo;
    const std::string pipelineName = name + KindSuffix(kind) + (point ? " points" : "");
    createInfo.PSODesc.Name = pipelineName.c_str();
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    auto& pipeline = createInfo.GraphicsPipeline;
    pipeline.DSVFormat = kSceneDepthFormat;
    pipeline.PrimitiveTopology = point ? Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST : Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline.RasterizerDesc.CullMode = twoSided ? Diligent::CULL_MODE_NONE : Diligent::CULL_MODE_BACK;
    pipeline.RasterizerDesc.FrontCounterClockwise = true;
    pipeline.DepthStencilDesc.DepthEnable = true;
    pipeline.DepthStencilDesc.DepthWriteEnable = kind != SurfaceKind::Forward;
    pipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    ConfigureTargets(pipeline, kind);
    createInfo.pVS = vertexShader;
    createInfo.pPS = pixelShader;
    const Diligent::LayoutElement layout[] = {
        {0, 0, kSurfaceVertexPositionComponents, Diligent::VT_FLOAT32, false},
        {1, 0, kSurfaceVertexPositionComponents, Diligent::VT_FLOAT32, false},
        {2, 0, kSurfaceVertexUvComponents,       Diligent::VT_FLOAT32, false}
    };
    pipeline.InputLayout.LayoutElements = layout;
    pipeline.InputLayout.NumElements = point ? 0 : static_cast<Diligent::Uint32>(std::size(layout));
    const litgpu::PipelineLayout resourceLayout = SurfaceLayout(kind, vertexShader, pixelShader);
    resourceLayout.apply(createInfo.PSODesc.ResourceLayout);
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    gpu.pDevice->CreateGraphicsPipelineState(createInfo, &pso);
    if (!pso) Lit::Log::Error("Failed to create surface pipeline '{}'", pipelineName);
    return pso;
}

void UseFallbackPipelines(DiligentData::SurfaceProgram& program, const DiligentData::SurfaceProgram& standard) {
    program.gbuffer = standard.gbuffer;
    program.gbufferPoint = standard.gbufferPoint;
    program.forward = standard.forward;
    program.forwardPoint = standard.forwardPoint;
    program.shadow = standard.shadow;
    program.shadowPoint = standard.shadowPoint;
}

void UpdateShaderBounds(DiligentData& gpu) {
    std::vector<float> bounds(kMaxSurfaceShaders, 0.0f);
    for (size_t surfaceIndex = 0; surfaceIndex < gpu.surfaces.size() && surfaceIndex < bounds.size(); ++surfaceIndex) bounds[surfaceIndex] = gpu.surfaces[surfaceIndex].bounds;
    if (gpu.pShaderBounds) gpu.pImmediateContext->UpdateBuffer(gpu.pShaderBounds, 0, sizeof(float) * bounds.size(), bounds.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

}

bool BuildSurface(DiligentData& gpu, DiligentData::SurfaceProgram& program) {
    const std::string text = litgpu::readText(program.path);
    const bool fallback = !gpu.surfaces.empty() && &program != &gpu.surfaces.front() && gpu.surfaces.front().valid;
    auto fail = [&](const std::string& reason) {
        if (!program.valid && fallback) UseFallbackPipelines(program, gpu.surfaces.front());
        program.error = reason;
        Lit::Log::Error("Surface shader '{}' failed to build; {}", program.name, program.valid ? "keeping the previous version" : fallback ? "drawing its objects with the standard surface until it compiles" : "objects using it are not drawn");
        return false;
    };
    if (text.empty()) {
        litgpu::reportShaderError(program.path, "surface shader file not found");
        return fail("surface shader file not found");
    }
    const SurfaceSource source = ParseSurface(text);
    program.bounds = source.bounds;
    program.twoSided = source.twoSided;
    litgpu::lastCompileErrors().clear();
    const std::tuple<SurfaceKind, bool> variants[kSurfaceVariantCount] = {
        {SurfaceKind::GBuffer, false},
        {SurfaceKind::GBuffer, true },
        {SurfaceKind::Forward, false},
        {SurfaceKind::Forward, true },
        {SurfaceKind::Shadow,  false},
        {SurfaceKind::Shadow,  true }
    };
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> built[kSurfaceVariantCount];
    std::string variantErrors[kSurfaceVariantCount];
    std::vector<std::thread> workers;
    for (int variantIndex = 0; variantIndex < kSurfaceVariantCount; ++variantIndex) {
        auto buildVariant = [&, variantIndex] {
            const auto& [kind, point] = variants[variantIndex];
            built[variantIndex] = BuildSurfacePso(gpu, program.name, program.path, source, kind, point, source.twoSided);
            variantErrors[variantIndex] = litgpu::lastCompileErrors();
        };
        if (std::getenv("LIT_SERIAL_SURFACES")) buildVariant();
        else workers.emplace_back(buildVariant);
    }
    for (std::thread& worker : workers) worker.join();
    for (int variantIndex = 0; variantIndex < kSurfaceVariantCount; ++variantIndex) {
        if (!built[variantIndex]) return fail(variantErrors[variantIndex].empty() ? "pipeline creation failed" : variantErrors[variantIndex]);
    }
    program.gbuffer = built[0];
    program.gbufferPoint = built[1];
    program.forward = built[2];
    program.forwardPoint = built[3];
    program.shadow = built[4];
    program.shadowPoint = built[5];
    program.valid = true;
    program.error.clear();
    return true;
}

uint32_t Renderer::loadSurfaceShader(const std::string& name, const std::string& path) {
    if (!m_initialized) return 0;
    auto& gpu = *m_diligent;
    for (uint32_t surfaceIndex = 0; surfaceIndex < gpu.surfaces.size(); ++surfaceIndex) {
        if (gpu.surfaces[surfaceIndex].name != name) continue;
        gpu.surfaces[surfaceIndex].path = path;
        gpu.pImmediateContext->WaitForIdle();
        BuildSurface(gpu, gpu.surfaces[surfaceIndex]);
        UpdateShaderBounds(gpu);
        rebindSurfaceResources();
        return surfaceIndex;
    }
    if (gpu.surfaces.size() >= kMaxSurfaceShaders) {
        Lit::Log::Error("Surface shader limit ({}) reached; '{}' not loaded", kMaxSurfaceShaders, name);
        return 0;
    }
    DiligentData::SurfaceProgram program;
    program.name = name;
    program.path = path;
    if (!BuildSurface(gpu, program) && !program.gbuffer) return 0;
    gpu.pImmediateContext->WaitForIdle();
    gpu.surfaces.push_back(std::move(program));
    m_numDrawingShaders = gpu.surfaces.size();
    reallocateBuffers(m_maxObjects);
    Lit::Log::Info("Surface shader '{}' loaded as id {}", name, gpu.surfaces.size() - 1);
    return static_cast<uint32_t>(gpu.surfaces.size() - 1);
}

std::string Renderer::surfacePath(uint32_t id) const {
    if (!m_diligent || id >= m_diligent->surfaces.size()) return {};
    return m_diligent->surfaces[id].path;
}

std::string Renderer::surfaceStatus(const std::string& name) const {
    if (!m_diligent) return "missing";
    for (const auto& surface : m_diligent->surfaces) {
        if (surface.name != name) continue;
        if (surface.valid) return "ok";
        return surface.gbuffer ? "fallback" : "failed";
    }
    return "missing";
}

std::vector<ShaderError> Renderer::surfaceErrors() const {
    std::vector<ShaderError> errors;
    if (!m_diligent) return errors;
    for (const auto& surface : m_diligent->surfaces) {
        if (!surface.error.empty()) errors.push_back({surface.path, surface.error});
    }
    return errors;
}

std::vector<std::string> Renderer::surfaceNames() const {
    std::vector<std::string> names;
    if (!m_diligent) return names;
    names.reserve(m_diligent->surfaces.size());
    for (const auto& surface : m_diligent->surfaces) names.push_back(surface.name);
    return names;
}

void Renderer::createSurfacePipelines() {
    auto& gpu = *m_diligent;
    if (gpu.surfaces.empty()) {
        const std::pair<const char*, const char*> builtIns[] = {
            {"Standard", "standard"},
            {"Checker",  "checker" },
            {"Glass",    "glass"   },
            {"Lava",     "lava"    },
            {"Marble",   "marble"  },
            {"Toon",     "toon"    },
            {"Unlit",    "unlit"   }
        };
        for (const auto& [name, file] : builtIns) {
            DiligentData::SurfaceProgram program;
            program.name = name;
            program.path = std::string("resources/shaders/surfaces/") + file + ".surface";
            gpu.surfaces.push_back(std::move(program));
        }
    }
    for (auto& program : gpu.surfaces) BuildSurface(gpu, program);
}
