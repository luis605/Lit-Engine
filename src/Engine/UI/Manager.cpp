module;

#include <ft2build.h>
#include FT_FREETYPE_H

#include <map>
#include <memory>
#include <vector>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/SwapChain.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

#include "Engine/Log/Log.hpp"

module Engine.UI.manager;

import Engine.glm;

struct Character {
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pTextureView;
    glm::ivec2 size;
    glm::ivec2 bearing;
    unsigned int advance;
};

struct DiligentUIData {
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> pDevice;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> pContext;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> pSwapChain;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVertexBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pConstants;

    std::map<char, Character> characters;
};

struct TextConstantBuffer {
    glm::mat4 projection;
    glm::vec4 textColor;
};

static std::string LoadSourceFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Lit::Log::Error("Failed to open shader file: {}", filepath);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

UIManager::UIManager() {
    m_diligent = new DiligentUIData();
}

UIManager::~UIManager() {
    delete static_cast<DiligentUIData*>(m_diligent);
}

void UIManager::init(Diligent::IRenderDevice* pDevice, Diligent::IDeviceContext* pContext, Diligent::ISwapChain* pSwapChain, const int windowWidth, const int windowHeight) {
    auto* d = static_cast<DiligentUIData*>(m_diligent);
    d->pDevice = pDevice;
    d->pContext = pContext;
    d->pSwapChain = pSwapChain;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        Lit::Log::Error("FREETYPE: Could not init FreeType Library");
        return;
    }

    FT_Face face;
    if (FT_New_Face(ft, "resources/fonts/Inter.ttf", 0, &face)) {
        Lit::Log::Error("FREETYPE: Failed to load font");
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, 48);

    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            Lit::Log::Warn("FREETYPE: Failed to load Glyph");
            continue;
        }

        Diligent::TextureDesc TexDesc;
        TexDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        TexDesc.Width = face->glyph->bitmap.width;
        TexDesc.Height = face->glyph->bitmap.rows;
        TexDesc.Format = Diligent::TEX_FORMAT_R8_UNORM;
        TexDesc.Usage = Diligent::USAGE_IMMUTABLE;
        TexDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
        TexDesc.MipLevels = 1;

        Diligent::RefCntAutoPtr<Diligent::ITexture> pTexture;
        Diligent::RefCntAutoPtr<Diligent::ITextureView> pTextureView;

        if (TexDesc.Width > 0 && TexDesc.Height > 0) {

            unsigned int alignedStride = (TexDesc.Width + 3) & ~3;
            std::vector<unsigned char> alignedData(alignedStride * TexDesc.Height);

            for (unsigned int r = 0; r < TexDesc.Height; ++r) {
                memcpy(alignedData.data() + r * alignedStride,
                       face->glyph->bitmap.buffer + r * face->glyph->bitmap.pitch,
                       TexDesc.Width);
            }

            Diligent::TextureSubResData InitData;
            InitData.pData = alignedData.data();
            InitData.Stride = alignedStride;

            Diligent::TextureData Data;
            Data.NumSubresources = 1;
            Data.pSubResources = &InitData;

            d->pDevice->CreateTexture(TexDesc, &Data, &pTexture);
            pTextureView = pTexture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        }

        Character character = {
            pTextureView,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x)};
        d->characters.insert(std::pair<char, Character>(c, character));
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    Diligent::BufferDesc VertBuffDesc;
    VertBuffDesc.Name = "Text Vertex Buffer";
    VertBuffDesc.Usage = Diligent::USAGE_DYNAMIC;
    VertBuffDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    VertBuffDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    VertBuffDesc.Size = sizeof(float) * 6 * 4;
    d->pDevice->CreateBuffer(VertBuffDesc, nullptr, &d->pVertexBuffer);

    Diligent::BufferDesc CBDesc;
    CBDesc.Name = "Text Constants Buffer";
    CBDesc.Usage = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    CBDesc.Size = sizeof(TextConstantBuffer);
    d->pDevice->CreateBuffer(CBDesc, nullptr, &d->pConstants);

    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Text PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = d->pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = d->pSwapChain->GetDesc().DepthBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

    auto& RT0 = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    RT0.BlendEnable = true;
    RT0.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    RT0.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOp = Diligent::BLEND_OPERATION_ADD;

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    std::string vertSource = LoadSourceFromFile("resources/shaders/text.vert");
    std::string fragSource = LoadSourceFromFile("resources/shaders/text.frag");

    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
        ShaderCI.Desc.Name = "Text VS";
        size_t versionPos = vertSource.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = vertSource.find('\n', versionPos);
            if (nextLine != std::string::npos)
                vertSource = vertSource.substr(nextLine + 1);
        }
        ShaderCI.Source = vertSource.c_str();
        d->pDevice->CreateShader(ShaderCI, &pVS);
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name = "Text PS";
        size_t versionPos = fragSource.find("#version");
        if (versionPos != std::string::npos) {
            size_t nextLine = fragSource.find('\n', versionPos);
            if (nextLine != std::string::npos)
                fragSource = fragSource.substr(nextLine + 1);
        }
        ShaderCI.Source = fragSource.c_str();
        d->pDevice->CreateShader(ShaderCI, &pPS);
    }

    if (!pVS || !pPS) {
        Lit::Log::Error("Failed to create Text Shaders");
        return;
    }

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    Diligent::LayoutElement LayoutElems[] = {
        Diligent::LayoutElement{0, 0, 4, Diligent::VT_FLOAT32, false}};
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_PIXEL, "text", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars.data();
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = Vars.size();

    Diligent::SamplerDesc SamLinearClampDesc;
    SamLinearClampDesc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    SamLinearClampDesc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    SamLinearClampDesc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    SamLinearClampDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    SamLinearClampDesc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    SamLinearClampDesc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    Diligent::ImmutableSamplerDesc ImtblSamplers[] = {
        {Diligent::SHADER_TYPE_PIXEL, "text", SamLinearClampDesc}};
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

    d->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &d->pPSO);

    if (d->pPSO) {
        d->pPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "TextConstants")->Set(d->pConstants);
        d->pPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "TextConstants")->Set(d->pConstants);
        d->pPSO->CreateShaderResourceBinding(&d->pSRB, true);
    }
}

void UIManager::cleanup() {
    auto* d = static_cast<DiligentUIData*>(m_diligent);
    if (d) {
        d->pPSO.Release();
        d->pSRB.Release();
        d->pVertexBuffer.Release();
        d->pConstants.Release();
        d->pDevice.Release();
        d->pContext.Release();
        d->pSwapChain.Release();
        d->characters.clear();
    }
}

void UIManager::addText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_texts.push_back({text, x, y, scale, color});
}

void UIManager::render() {
    auto* d = static_cast<DiligentUIData*>(m_diligent);
    if (!d || !d->pPSO || !d->pSRB)
        return;

    d->pContext->SetPipelineState(d->pPSO);

    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_windowWidth), 0.0f, static_cast<float>(m_windowHeight));

    Diligent::IBuffer* pBuffs[] = {d->pVertexBuffer};
    Diligent::Uint64 offsets[] = {0};
    d->pContext->SetVertexBuffers(0, 1, pBuffs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    for (const auto& textData : m_texts) {

        {
            Diligent::MapHelper<TextConstantBuffer> CBConstants(d->pContext, d->pConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            CBConstants->projection = projection;
            CBConstants->textColor = glm::vec4(textData.color, 1.0f);
        }

        float x = textData.x;
        std::string::const_iterator c;
        for (c = textData.text.begin(); c != textData.text.end(); c++) {
            Character ch = d->characters[*c];

            if (!ch.pTextureView)
                continue;

            float xpos = x + ch.bearing.x * textData.scale;
            float ypos = textData.y - (ch.size.y - ch.bearing.y) * textData.scale;

            float w = ch.size.x * textData.scale;
            float h = ch.size.y * textData.scale;

            struct Vertex {
                float x, y, u, v;
            };

            Vertex vertices[6] = {
                {xpos, ypos + h, 0.0f, 0.0f},
                {xpos, ypos, 0.0f, 1.0f},
                {xpos + w, ypos, 1.0f, 1.0f},

                {xpos, ypos + h, 0.0f, 0.0f},
                {xpos + w, ypos, 1.0f, 1.0f},
                {xpos + w, ypos + h, 1.0f, 0.0f}};

            {
                Diligent::MapHelper<Vertex> Verts(d->pContext, d->pVertexBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
                memcpy(Verts, vertices, sizeof(vertices));
            }

            if (auto* var = d->pSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "text")) {
                var->Set(ch.pTextureView);
            }

            d->pContext->CommitShaderResources(d->pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            Diligent::DrawAttribs DrawAttrs;
            DrawAttrs.NumVertices = 6;
            DrawAttrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
            d->pContext->Draw(DrawAttrs);

            x += (ch.advance >> 6) * textData.scale;
        }
    }
    m_texts.clear();
}
