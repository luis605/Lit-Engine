module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Query.h"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

module Engine.Render.custom;

import Engine.glm;

#include "CustomPassesInternal.hpp"

using namespace custom_detail;

namespace {

constexpr std::string_view kPostTemplatePath = "resources/shaders/effects/post_template.frag";
constexpr std::string_view kFullscreenVertexPath = "resources/shaders/fullscreen.vert";

struct GraphicsBuild {
    Diligent::GraphicsPipelineStateCreateInfo createInfo;
    RefCntAutoPtr<Diligent::IShader> vertex;
    RefCntAutoPtr<Diligent::IShader> pixel;
    RefCntAutoPtr<Diligent::IShader> mesh;
    RefCntAutoPtr<Diligent::IShader> amplification;
    litgpu::PipelineLayout layout;
    EffectStage stage = EffectStage::Hdr;
};

std::vector<litgpu::NamedSampler> customSamplers() {
    return {{"u_depth", litgpu::pointClamp()}, {"u_gbuffer0", litgpu::pointClamp()}, {"u_gbuffer1", litgpu::pointClamp()}, {"u_color", litgpu::linearClamp()}, {"u_colorTextures", litgpu::anisotropicWrap()}, {"u_dataTextures", litgpu::anisotropicWrap()}};
}

void parseParamPragma(std::istringstream& words, const std::string& indexText, Pass& pass) {
    int index = -1;
    std::istringstream(indexText) >> index;
    if (index < 0 || index >= kParamCount) {
        Lit::Log::Warn("'#pragma lit param' index must be 0..3, got '{}'", indexText);
        return;
    }
    glm::vec4 value(0.0f);
    for (int component = 0; component < 4 && (words >> value[component]); ++component) {
    }
    pass.paramDefaults[index] = value;
    pass.paramDefaultMask |= 1u << index;
}

std::string effectSource(const std::string& user, EffectStage& stage, Pass& pass) {
    std::string output;
    output.reserve(user.size());
    std::istringstream lines(user);
    std::string line;
    stage = EffectStage::Hdr;
    pass.paramDefaultMask = 0;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string directive, tool, command, argument;
        words >> directive >> tool >> command >> argument;
        if (directive == "#pragma" && tool == "lit" && command == "param") {
            parseParamPragma(words, argument, pass);
            output += "\n";
            continue;
        }
        if (directive == "#pragma" && tool == "lit" && command == "stage") {
            if (argument == "ldr") stage = EffectStage::Ldr;
            else if (argument != "hdr") Lit::Log::Warn("Unknown effect stage '{}' (use hdr or ldr)", argument);
            output += "\n";
            continue;
        }
        output += line + "\n";
    }
    return output;
}

RefCntAutoPtr<Diligent::IPipelineState> buildCompute(Diligent::IRenderDevice* device, Pass& pass, const std::vector<litgpu::NamedSampler>& samplers) {
    auto computeShader = litgpu::compileFile(device, pass.compute.path, Diligent::SHADER_TYPE_COMPUTE);
    if (!computeShader) return {};
    litgpu::PipelineLayout layout = litgpu::makeLayout({computeShader});
    for (const auto& sampler : samplers) layout.samplers.push_back({Diligent::SHADER_TYPE_COMPUTE, sampler.name, sampler.desc});
    Diligent::ComputePipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = pass.info.name.c_str();
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
    createInfo.pCS = computeShader;
    layout.apply(createInfo.PSODesc.ResourceLayout);
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateComputePipelineState(createInfo, &pso);
    return pso;
}

bool configureEffect(Diligent::IRenderDevice* device, Pass& pass, GraphicsBuild& build) {
    auto& graphics = build.createInfo.GraphicsPipeline;
    const std::string templateSource = litgpu::readText(std::string(kPostTemplatePath));
    const std::string userSource = litgpu::readText(pass.effect.path);
    if (templateSource.empty() || userSource.empty()) {
        Lit::Log::Error("Post effect '{}' source missing ({})", pass.info.name, pass.effect.path);
        litgpu::reportShaderError(pass.effect.path, "post effect source missing");
        return false;
    }
    const std::string source = litgpu::injectUserCode(templateSource, litgpu::userLine(1) + effectSource(userSource, build.stage, pass));
    std::vector<std::pair<std::string, std::string>> macros;
    if (build.stage == EffectStage::Ldr) {
        macros.push_back({"LIT_LDR_EFFECT", "1"});
        graphics.RTVFormats[0] = kLdrFormat;
    }
    build.vertex = litgpu::compileFile(device, std::string(kFullscreenVertexPath), Diligent::SHADER_TYPE_VERTEX);
    build.pixel = litgpu::compileSource(device, pass.effect.path, source, Diligent::SHADER_TYPE_PIXEL, macros, pass.effect.path);
    if (!build.vertex || !build.pixel) return false;
    graphics.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
    graphics.DepthStencilDesc.DepthEnable = false;
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    build.layout = litgpu::makeLayout({build.vertex, build.pixel});
    return true;
}

void configureBlend(Diligent::RenderTargetBlendDesc& blend, DrawBlend mode) {
    if (mode == DrawBlend::Additive) {
        blend.BlendEnable = true;
        blend.SrcBlend = blend.DestBlend = blend.SrcBlendAlpha = blend.DestBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    } else if (mode == DrawBlend::Alpha) {
        blend.BlendEnable = true;
        blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
        blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    }
}

bool configureMeshStages(Diligent::IRenderDevice* device, Pass& pass, bool meshShaders, GraphicsBuild& build) {
    const DrawDesc& draw = pass.draw;
    if (!meshShaders) {
        Lit::Log::Warn("Draw '{}' needs mesh shaders, which this GPU does not support; it is skipped", pass.info.name);
        return false;
    }
    build.createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_MESH;
    build.mesh = litgpu::compileFile(device, draw.meshPath, Diligent::SHADER_TYPE_MESH);
    if (!build.mesh) return false;
    if (!draw.taskPath.empty()) {
        build.amplification = litgpu::compileFile(device, draw.taskPath, Diligent::SHADER_TYPE_AMPLIFICATION);
        if (!build.amplification) return false;
    }
    build.createInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_UNDEFINED;
    build.layout = build.amplification ? litgpu::makeLayout({build.amplification, build.mesh, build.pixel}) : litgpu::makeLayout({build.mesh, build.pixel});
    return true;
}

bool configureVertexStages(Diligent::IRenderDevice* device, const DrawDesc& draw, GraphicsBuild& build) {
    build.vertex = litgpu::compileFile(device, draw.vertexPath, Diligent::SHADER_TYPE_VERTEX);
    if (!build.vertex) return false;
    const Diligent::PRIMITIVE_TOPOLOGY topologies[] = {Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST, Diligent::PRIMITIVE_TOPOLOGY_LINE_LIST, Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
    build.createInfo.GraphicsPipeline.PrimitiveTopology = topologies[static_cast<int>(draw.primitive)];
    build.layout = litgpu::makeLayout({build.vertex, build.pixel});
    return true;
}

bool configureDraw(Diligent::IRenderDevice* device, Pass& pass, bool meshShaders, GraphicsBuild& build) {
    const DrawDesc& draw = pass.draw;
    auto& graphics = build.createInfo.GraphicsPipeline;
    graphics.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    graphics.DepthStencilDesc.DepthEnable = draw.depthTest;
    graphics.DepthStencilDesc.DepthWriteEnable = draw.depthWrite;
    graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    graphics.RasterizerDesc.CullMode = draw.doubleSided ? Diligent::CULL_MODE_NONE : Diligent::CULL_MODE_BACK;
    graphics.RasterizerDesc.FrontCounterClockwise = true;
    configureBlend(graphics.BlendDesc.RenderTargets[0], draw.blend);
    build.pixel = litgpu::compileFile(device, draw.fragmentPath, Diligent::SHADER_TYPE_PIXEL);
    if (!build.pixel) return false;
    return pass.mesh ? configureMeshStages(device, pass, meshShaders, build) : configureVertexStages(device, draw, build);
}

RefCntAutoPtr<Diligent::IPipelineState> buildGraphics(Diligent::IRenderDevice* device, Pass& pass, bool meshShaders, const std::vector<litgpu::NamedSampler>& samplers) {
    GraphicsBuild build;
    build.createInfo.PSODesc.Name = pass.info.name.c_str();
    build.createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    auto& graphics = build.createInfo.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    const bool configured = pass.info.kind == PassKind::Effect ? configureEffect(device, pass, build) : configureDraw(device, pass, meshShaders, build);
    if (!configured) return {};
    for (const auto& sampler : samplers) build.layout.samplers.push_back({Diligent::SHADER_TYPE_ALL_GRAPHICS | Diligent::SHADER_TYPE_ALL_MESH, sampler.name, sampler.desc});
    build.createInfo.pVS = build.vertex;
    build.createInfo.pPS = build.pixel;
    build.createInfo.pMS = build.mesh;
    build.createInfo.pAS = build.amplification;
    build.layout.apply(build.createInfo.PSODesc.ResourceLayout);
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateGraphicsPipelineState(build.createInfo, &pso);
    if (pso && pass.info.kind == PassKind::Effect) pass.info.stage = build.stage;
    return pso;
}

}

namespace custom_detail {

RefCntAutoPtr<Diligent::IPipelineState> buildPass(Diligent::IRenderDevice* device, Pass& pass, bool meshShaders) {
    const auto samplers = customSamplers();
    if (pass.info.kind == PassKind::Compute) return buildCompute(device, pass, samplers);
    return buildGraphics(device, pass, meshShaders, samplers);
}

}
