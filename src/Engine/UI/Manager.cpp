module;

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
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

inline constexpr unsigned int kGlyphCount = 128;
inline constexpr unsigned int kAtlasWidth = 1024;
inline constexpr unsigned int kAtlasMaxHeight = 1024;
inline constexpr unsigned int kAtlasPadding = 2;
inline constexpr unsigned int kInitialGlyphCapacity = 2048;

struct Glyph {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    int sizeX = 0, sizeY = 0;
    int bearingX = 0, bearingY = 0;
    unsigned int advance = 0;
    bool valid = false;
};

struct TextVertex {
    float x, y, u, v;
};

struct DrawBatch {
    Diligent::Uint32 firstVertex;
    Diligent::Uint32 numVertices;
    glm::vec3 color;
};

struct DiligentUIData {
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> pDevice;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> pContext;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> pSwapChain;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVertexBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pConstants;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pAtlas;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pAtlasView;

    Glyph glyphs[kGlyphCount];

    std::vector<TextVertex> vertexScratch;
    std::vector<DrawBatch> batches;

    Diligent::Uint32 vertexCapacity = 0;
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

static bool CreateTextVertexBuffer(DiligentUIData* d, Diligent::Uint32 numVertices) {
    Diligent::BufferDesc VertBuffDesc;
    VertBuffDesc.Name = "Text Vertex Buffer";
    VertBuffDesc.Usage = Diligent::USAGE_DYNAMIC;
    VertBuffDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    VertBuffDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    VertBuffDesc.Size = static_cast<Diligent::Uint64>(numVertices) * sizeof(TextVertex);

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBuffer;
    d->pDevice->CreateBuffer(VertBuffDesc, nullptr, &pBuffer);
    if (!pBuffer) {
        Lit::Log::Error("Failed to create Text Vertex Buffer ({} vertices)", numVertices);
        return false;
    }

    d->pVertexBuffer = pBuffer;
    d->vertexCapacity = numVertices;
    return true;
}

UIManager::UIManager() {
    m_diligent = new DiligentUIData();
    m_texts.reserve(64);
    m_textArena.reserve(4096);
}

UIManager::~UIManager() { delete static_cast<DiligentUIData*>(m_diligent); }

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

    std::vector<unsigned char> atlasPixels(static_cast<size_t>(kAtlasWidth) * kAtlasMaxHeight, 0);
    unsigned int penX = 0;
    unsigned int penY = 0;
    unsigned int rowHeight = 0;
    unsigned int usedHeight = 0;

    for (unsigned char c = 0; c < kGlyphCount; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            Lit::Log::Warn("FREETYPE: Failed to load Glyph");
            continue;
        }

        Glyph& glyph = d->glyphs[c];
        glyph.sizeX = static_cast<int>(face->glyph->bitmap.width);
        glyph.sizeY = static_cast<int>(face->glyph->bitmap.rows);
        glyph.bearingX = face->glyph->bitmap_left;
        glyph.bearingY = face->glyph->bitmap_top;
        glyph.advance = static_cast<unsigned int>(face->glyph->advance.x);

        const unsigned int w = face->glyph->bitmap.width;
        const unsigned int h = face->glyph->bitmap.rows;
        if (w == 0 || h == 0)
            continue;

        if (penX + w + kAtlasPadding > kAtlasWidth) {
            penX = 0;
            penY += rowHeight + kAtlasPadding;
            rowHeight = 0;
        }

        if (penY + h > kAtlasMaxHeight) {
            Lit::Log::Warn("Font atlas is full, glyph {} was dropped", static_cast<int>(c));
            continue;
        }

        for (unsigned int r = 0; r < h; ++r) { memcpy(atlasPixels.data() + static_cast<size_t>(penY + r) * kAtlasWidth + penX, face->glyph->bitmap.buffer + static_cast<ptrdiff_t>(r) * face->glyph->bitmap.pitch, w); }

        glyph.u0 = static_cast<float>(penX);
        glyph.v0 = static_cast<float>(penY);
        glyph.u1 = static_cast<float>(penX + w);
        glyph.v1 = static_cast<float>(penY + h);
        glyph.valid = true;

        penX += w + kAtlasPadding;
        rowHeight = std::max(rowHeight, h);
        usedHeight = std::max(usedHeight, penY + h);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    const unsigned int atlasHeight = std::max(usedHeight, 1u);
    for (Glyph& glyph : d->glyphs) {
        if (!glyph.valid)
            continue;
        glyph.u0 /= static_cast<float>(kAtlasWidth);
        glyph.u1 /= static_cast<float>(kAtlasWidth);
        glyph.v0 /= static_cast<float>(atlasHeight);
        glyph.v1 /= static_cast<float>(atlasHeight);
    }

    {
        Diligent::TextureDesc TexDesc;
        TexDesc.Name = "Text Glyph Atlas";
        TexDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        TexDesc.Width = kAtlasWidth;
        TexDesc.Height = atlasHeight;
        TexDesc.Format = Diligent::TEX_FORMAT_R8_UNORM;
        TexDesc.Usage = Diligent::USAGE_IMMUTABLE;
        TexDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
        TexDesc.MipLevels = 1;

        Diligent::TextureSubResData InitData;
        InitData.pData = atlasPixels.data();
        InitData.Stride = kAtlasWidth;

        Diligent::TextureData Data;
        Data.NumSubresources = 1;
        Data.pSubResources = &InitData;

        d->pDevice->CreateTexture(TexDesc, &Data, &d->pAtlas);
        if (!d->pAtlas) {
            Lit::Log::Error("Failed to create the text glyph atlas");
            return;
        }
        d->pAtlasView = d->pAtlas->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    }

    CreateTextVertexBuffer(d, kInitialGlyphCapacity * 6);
    d->vertexScratch.reserve(kInitialGlyphCapacity * 6);
    d->batches.reserve(64);

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
            if (nextLine != std::string::npos) vertSource = vertSource.substr(nextLine + 1);
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
            if (nextLine != std::string::npos) fragSource = fragSource.substr(nextLine + 1);
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
        Diligent::LayoutElement{0, 0, 4, Diligent::VT_FLOAT32, false}
    };
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    std::vector<Diligent::ShaderResourceVariableDesc> Vars = {
        {Diligent::SHADER_TYPE_PIXEL, "text", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
    };
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
        {Diligent::SHADER_TYPE_PIXEL, "text", SamLinearClampDesc}
    };
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

    d->pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &d->pPSO);

    if (d->pPSO) {
        if (auto* vsVar = d->pPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "TextConstants"))
            vsVar->Set(d->pConstants);
        if (auto* psVar = d->pPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "TextConstants"))
            psVar->Set(d->pConstants);

        d->pPSO->CreateShaderResourceBinding(&d->pSRB, true);

        if (d->pSRB) {
            if (auto* var = d->pSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "text"))
                var->Set(d->pAtlasView);
        }
    }
}

void UIManager::cleanup() {
    auto* d = static_cast<DiligentUIData*>(m_diligent);
    if (d) {
        d->pPSO.Release();
        d->pSRB.Release();
        d->pVertexBuffer.Release();
        d->pConstants.Release();
        d->pAtlasView.Release();
        d->pAtlas.Release();
        d->pDevice.Release();
        d->pContext.Release();
        d->pSwapChain.Release();
        d->vertexScratch.clear();
        d->vertexScratch.shrink_to_fit();
        d->batches.clear();
        d->batches.shrink_to_fit();
        d->vertexCapacity = 0;
    }
}

void UIManager::addText(std::string_view text, float x, float y, float scale, const glm::vec3& color) {
    const std::uint32_t offset = static_cast<std::uint32_t>(m_textArena.size());
    m_textArena.insert(m_textArena.end(), text.begin(), text.end());
    m_texts.push_back({offset, static_cast<std::uint32_t>(text.size()), x, y, scale, color});
}

void UIManager::render() {
    auto* d = static_cast<DiligentUIData*>(m_diligent);
    if (!d || !d->pPSO || !d->pSRB) {
        m_texts.clear();
        m_textArena.clear();
        return;
    }

    d->vertexScratch.clear();
    d->batches.clear();

    const char* const arena = m_textArena.data();
    for (const TextData& textData : m_texts) {
        const Diligent::Uint32 firstVertex = static_cast<Diligent::Uint32>(d->vertexScratch.size());

        float x = textData.x;
        const char* const str = arena + textData.offset;
        for (std::uint32_t i = 0; i < textData.length; ++i) {
            const unsigned char code = static_cast<unsigned char>(str[i]);
            if (code >= kGlyphCount)
                continue;

            const Glyph& ch = d->glyphs[code];
            if (!ch.valid)
                continue;

            const float xpos = x + ch.bearingX * textData.scale;
            const float ypos = textData.y - (ch.sizeY - ch.bearingY) * textData.scale;

            const float w = ch.sizeX * textData.scale;
            const float h = ch.sizeY * textData.scale;

            d->vertexScratch.push_back({xpos,     ypos + h, ch.u0, ch.v0});
            d->vertexScratch.push_back({xpos,     ypos,     ch.u0, ch.v1});
            d->vertexScratch.push_back({xpos + w, ypos,     ch.u1, ch.v1});

            d->vertexScratch.push_back({xpos,     ypos + h, ch.u0, ch.v0});
            d->vertexScratch.push_back({xpos + w, ypos,     ch.u1, ch.v1});
            d->vertexScratch.push_back({xpos + w, ypos + h, ch.u1, ch.v0});

            x += (ch.advance >> 6) * textData.scale;
        }

        const Diligent::Uint32 numVertices = static_cast<Diligent::Uint32>(d->vertexScratch.size()) - firstVertex;
        if (numVertices > 0)
            d->batches.push_back({firstVertex, numVertices, textData.color});
    }

    m_texts.clear();
    m_textArena.clear();

    if (d->batches.empty())
        return;

    const Diligent::Uint32 numVertices = static_cast<Diligent::Uint32>(d->vertexScratch.size());
    if (numVertices > d->vertexCapacity) {
        if (!CreateTextVertexBuffer(d, std::max(numVertices, d->vertexCapacity * 2)))
            return;
    }

    {
        Diligent::MapHelper<TextVertex> Verts(d->pContext, d->pVertexBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        memcpy(static_cast<TextVertex*>(Verts), d->vertexScratch.data(), static_cast<size_t>(numVertices) * sizeof(TextVertex));
    }

    d->pContext->SetPipelineState(d->pPSO);

    Diligent::IBuffer* pBuffs[] = {d->pVertexBuffer};
    Diligent::Uint64 offsets[] = {0};
    d->pContext->SetVertexBuffers(0, 1, pBuffs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_windowWidth), 0.0f, static_cast<float>(m_windowHeight));

    glm::vec3 lastColor(0.0f);
    bool colorValid = false;
    for (const DrawBatch& batch : d->batches) {
        const bool sameColor = colorValid && batch.color.x == lastColor.x && batch.color.y == lastColor.y && batch.color.z == lastColor.z;
        if (!sameColor) {
            {
                Diligent::MapHelper<TextConstantBuffer> CBConstants(d->pContext, d->pConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
                CBConstants->projection = projection;
                CBConstants->textColor = glm::vec4(batch.color, 1.0f);
            }

            d->pContext->CommitShaderResources(d->pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            lastColor = batch.color;
            colorValid = true;
        }

        Diligent::DrawAttribs DrawAttrs;
        DrawAttrs.NumVertices = batch.numVertices;
        DrawAttrs.StartVertexLocation = batch.firstVertex;
        DrawAttrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        d->pContext->Draw(DrawAttrs);
    }
}