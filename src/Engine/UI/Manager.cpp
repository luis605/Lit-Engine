module;

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
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
#include "Engine/Render/GpuHelpers.hpp"

module Engine.UI.manager;

import Engine.glm;

#include "TextPipeline.hpp"

UIManager::UIManager() {
    m_diligent = new DiligentUIData();
    m_texts.reserve(kInitialTextCapacity);
    m_textArena.reserve(kInitialArenaCapacity);
}

UIManager::~UIManager() { delete static_cast<DiligentUIData*>(m_diligent); }

void UIManager::resize(int windowWidth, int windowHeight) {
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
}

void UIManager::init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::ISwapChain* swapChain, Diligent::TEXTURE_FORMAT depthFormat, const int windowWidth, const int windowHeight) {
    auto& uiData = *static_cast<DiligentUIData*>(m_diligent);
    uiData.device = device;
    uiData.context = context;
    uiData.swapChain = swapChain;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

    unsigned int atlasHeight = 1;
    std::vector<unsigned char> atlasPixels(static_cast<size_t>(kAtlasWidth) * kAtlasMaxHeight, 0);
    {
        FreeTypeFace font;
        if (!openFontFace(font)) return;
        atlasHeight = rasterizeGlyphs(uiData, font.face, atlasPixels);
    }
    normalizeGlyphUvs(uiData, atlasHeight);
    if (!createAtlasTexture(uiData, atlasPixels, atlasHeight)) return;

    createTextVertexBuffer(uiData, kInitialGlyphCapacity * kVerticesPerGlyph);
    uiData.vertexScratch.reserve(kInitialGlyphCapacity * kVerticesPerGlyph);
    uiData.batches.reserve(kInitialBatchCapacity);
    createConstantsBuffer(uiData);

    Diligent::RefCntAutoPtr<Diligent::IShader> vertexShader;
    Diligent::RefCntAutoPtr<Diligent::IShader> pixelShader;
    if (!createTextShaders(uiData, vertexShader, pixelShader)) {
        Lit::Log::Error("Failed to create Text Shaders");
        return;
    }
    createTextPipeline(uiData, depthFormat, vertexShader, pixelShader);
    bindPipelineResources(uiData);
}

void UIManager::cleanup() {
    auto* uiData = static_cast<DiligentUIData*>(m_diligent);
    if (!uiData) return;
    uiData->pipeline.Release();
    uiData->resourceBinding.Release();
    uiData->vertexBuffer.Release();
    uiData->constants.Release();
    uiData->atlasView.Release();
    uiData->atlas.Release();
    uiData->device.Release();
    uiData->context.Release();
    uiData->swapChain.Release();
    uiData->vertexScratch.clear();
    uiData->vertexScratch.shrink_to_fit();
    uiData->batches.clear();
    uiData->batches.shrink_to_fit();
    uiData->vertexCapacity = 0;
}

void UIManager::addText(std::string_view text, float x, float y, float scale, const glm::vec3& color) {
    const std::uint32_t offset = static_cast<std::uint32_t>(m_textArena.size());
    m_textArena.insert(m_textArena.end(), text.begin(), text.end());
    m_texts.push_back({offset, static_cast<std::uint32_t>(text.size()), x, y, scale, color});
}

void UIManager::render() {
    auto* uiData = static_cast<DiligentUIData*>(m_diligent);
    if (!uiData || !uiData->pipeline || !uiData->resourceBinding) {
        m_texts.clear();
        m_textArena.clear();
        return;
    }

    uiData->vertexScratch.clear();
    uiData->batches.clear();

    const char* const arena = m_textArena.data();
    for (const TextData& textData : m_texts) {
        const Diligent::Uint32 firstVertex = static_cast<Diligent::Uint32>(uiData->vertexScratch.size());

        float penX = textData.x;
        const char* const characters = arena + textData.offset;
        for (std::uint32_t charIndex = 0; charIndex < textData.length; ++charIndex) {
            const unsigned char code = static_cast<unsigned char>(characters[charIndex]);
            if (code >= kGlyphCount) continue;

            const Glyph& glyph = uiData->glyphs[code];
            if (glyph.valid) appendGlyphQuad(uiData->vertexScratch, glyph, penX, textData.y, textData.scale);
            penX += advanceInPixels(glyph, textData.scale);
        }

        const Diligent::Uint32 numVertices = static_cast<Diligent::Uint32>(uiData->vertexScratch.size()) - firstVertex;
        if (numVertices > 0) uiData->batches.push_back({firstVertex, numVertices, textData.color});
    }

    m_texts.clear();
    m_textArena.clear();

    if (uiData->batches.empty()) return;

    const Diligent::Uint32 numVertices = static_cast<Diligent::Uint32>(uiData->vertexScratch.size());
    if (numVertices > uiData->vertexCapacity) {
        if (!createTextVertexBuffer(*uiData, std::max(numVertices, uiData->vertexCapacity * 2))) return;
    }

    {
        Diligent::MapHelper<TextVertex> vertices(uiData->context, uiData->vertexBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        std::memcpy(static_cast<TextVertex*>(vertices), uiData->vertexScratch.data(), static_cast<size_t>(numVertices) * sizeof(TextVertex));
    }

    uiData->context->SetPipelineState(uiData->pipeline);

    Diligent::IBuffer* vertexBuffers[] = {uiData->vertexBuffer};
    const Diligent::Uint64 offsets[] = {0};
    uiData->context->SetVertexBuffers(0, 1, vertexBuffers, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_windowWidth), 0.0f, static_cast<float>(m_windowHeight));

    glm::vec3 lastColor(0.0f);
    bool colorValid = false;
    for (const DrawBatch& batch : uiData->batches) {
        if (!colorValid || batch.color != lastColor) {
            {
                Diligent::MapHelper<TextConstantBuffer> constants(uiData->context, uiData->constants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
                constants->projection = projection;
                constants->textColor = glm::vec4(batch.color, 1.0f);
            }
            uiData->context->CommitShaderResources(uiData->resourceBinding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            lastColor = batch.color;
            colorValid = true;
        }

        Diligent::DrawAttribs drawAttribs;
        drawAttribs.NumVertices = batch.numVertices;
        drawAttribs.StartVertexLocation = batch.firstVertex;
        drawAttribs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        uiData->context->Draw(drawAttribs);
    }
}

