#pragma once

#include "TextAtlas.hpp"

namespace {

bool createTextShaders(DiligentUIData& uiData, Diligent::RefCntAutoPtr<Diligent::IShader>& vertexShader, Diligent::RefCntAutoPtr<Diligent::IShader>& pixelShader) {
    Diligent::ShaderCreateInfo shaderInfo;
    shaderInfo.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    shaderInfo.Desc.UseCombinedTextureSamplers = true;

    std::string vertexSource = loadSourceFromFile("resources/shaders/text.vert");
    std::string fragmentSource = loadSourceFromFile("resources/shaders/text.frag");
    stripVersionLine(vertexSource);
    stripVersionLine(fragmentSource);

    shaderInfo.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shaderInfo.Desc.Name = "Text VS";
    shaderInfo.Source = vertexSource.c_str();
    vertexShader = litgpu::createShader(uiData.device, shaderInfo);

    shaderInfo.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shaderInfo.Desc.Name = "Text PS";
    shaderInfo.Source = fragmentSource.c_str();
    pixelShader = litgpu::createShader(uiData.device, shaderInfo);

    return vertexShader && pixelShader;
}

void createTextPipeline(DiligentUIData& uiData, Diligent::TEXTURE_FORMAT depthFormat, Diligent::IShader* vertexShader, Diligent::IShader* pixelShader) {
    Diligent::GraphicsPipelineStateCreateInfo pipelineInfo;
    pipelineInfo.PSODesc.Name = "Text PSO";
    pipelineInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pipelineInfo.GraphicsPipeline.NumRenderTargets = 1;
    pipelineInfo.GraphicsPipeline.RTVFormats[0] = uiData.swapChain->GetDesc().ColorBufferFormat;
    pipelineInfo.GraphicsPipeline.DSVFormat = depthFormat;
    pipelineInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    pipelineInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

    auto& blend = pipelineInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;

    pipelineInfo.pVS = vertexShader;
    pipelineInfo.pPS = pixelShader;

    Diligent::LayoutElement layoutElements[] = {
        Diligent::LayoutElement{0, 0, 4, Diligent::VT_FLOAT32, false}
    };
    pipelineInfo.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
    pipelineInfo.GraphicsPipeline.InputLayout.NumElements = _countof(layoutElements);

    pipelineInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    Diligent::ShaderResourceVariableDesc variables[] = {
        {Diligent::SHADER_TYPE_PIXEL, "text", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
    pipelineInfo.PSODesc.ResourceLayout.Variables = variables;
    pipelineInfo.PSODesc.ResourceLayout.NumVariables = _countof(variables);

    Diligent::SamplerDesc linearClamp;
    linearClamp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    linearClamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    linearClamp.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    Diligent::ImmutableSamplerDesc immutableSamplers[] = {
        {Diligent::SHADER_TYPE_PIXEL, "text", linearClamp}
    };
    pipelineInfo.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
    pipelineInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(immutableSamplers);

    uiData.device->CreateGraphicsPipelineState(pipelineInfo, &uiData.pipeline);
}

void bindPipelineResources(DiligentUIData& uiData) {
    if (!uiData.pipeline) return;
    if (auto* vertexVariable = uiData.pipeline->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "TextConstants")) vertexVariable->Set(uiData.constants);
    if (auto* pixelVariable = uiData.pipeline->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "TextConstants")) pixelVariable->Set(uiData.constants);

    uiData.pipeline->CreateShaderResourceBinding(&uiData.resourceBinding, true);
    if (!uiData.resourceBinding) return;
    if (auto* atlasVariable = uiData.resourceBinding->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "text")) atlasVariable->Set(uiData.atlasView);
}

float advanceInPixels(const Glyph& glyph, float scale) { return static_cast<float>(glyph.advance >> kFixedPointShift) * scale; }

void appendGlyphQuad(std::vector<TextVertex>& vertices, const Glyph& glyph, float penX, float baselineY, float scale) {
    const float left = penX + static_cast<float>(glyph.bearingX) * scale;
    const float bottom = baselineY - static_cast<float>(glyph.sizeY - glyph.bearingY) * scale;
    const float width = static_cast<float>(glyph.sizeX) * scale;
    const float height = static_cast<float>(glyph.sizeY) * scale;

    vertices.push_back({left, bottom + height, glyph.u0, glyph.v0});
    vertices.push_back({left, bottom, glyph.u0, glyph.v1});
    vertices.push_back({left + width, bottom, glyph.u1, glyph.v1});

    vertices.push_back({left, bottom + height, glyph.u0, glyph.v0});
    vertices.push_back({left + width, bottom, glyph.u1, glyph.v1});
    vertices.push_back({left + width, bottom + height, glyph.u1, glyph.v0});
}

}
