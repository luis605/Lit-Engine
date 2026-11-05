#pragma once

#include "TextTypes.hpp"

namespace {

std::string loadSourceFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Lit::Log::Error("Failed to open shader file: {}", filepath);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void stripVersionLine(std::string& source) {
    const size_t versionPosition = source.find("#version");
    if (versionPosition == std::string::npos) return;
    const size_t nextLine = source.find('\n', versionPosition);
    if (nextLine != std::string::npos) source.erase(0, nextLine + 1);
}

bool createTextVertexBuffer(DiligentUIData& uiData, Diligent::Uint32 numVertices) {
    Diligent::BufferDesc bufferDesc;
    bufferDesc.Name = "Text Vertex Buffer";
    bufferDesc.Usage = Diligent::USAGE_DYNAMIC;
    bufferDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    bufferDesc.Size = static_cast<Diligent::Uint64>(numVertices) * sizeof(TextVertex);

    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    uiData.device->CreateBuffer(bufferDesc, nullptr, &buffer);
    if (!buffer) {
        Lit::Log::Error("Failed to create Text Vertex Buffer ({} vertices)", numVertices);
        return false;
    }

    uiData.vertexBuffer = buffer;
    uiData.vertexCapacity = numVertices;
    return true;
}

bool openFontFace(FreeTypeFace& font) {
    if (FT_Init_FreeType(&font.library)) {
        Lit::Log::Error("FREETYPE: Could not init FreeType Library");
        return false;
    }
    if (FT_New_Face(font.library, "resources/fonts/Inter.ttf", 0, &font.face)) {
        Lit::Log::Error("FREETYPE: Failed to load font");
        font.face = nullptr;
        return false;
    }
    FT_Set_Pixel_Sizes(font.face, 0, kGlyphPixelHeight);
    return true;
}

unsigned int rasterizeGlyphs(DiligentUIData& uiData, FT_Face face, std::vector<unsigned char>& atlasPixels) {
    unsigned int penX = 0;
    unsigned int penY = 0;
    unsigned int rowHeight = 0;
    unsigned int usedHeight = 0;

    for (unsigned int code = 0; code < kGlyphCount; ++code) {
        if (FT_Load_Char(face, code, FT_LOAD_RENDER)) {
            Lit::Log::Warn("FREETYPE: Failed to load Glyph");
            continue;
        }

        Glyph& glyph = uiData.glyphs[code];
        const FT_Bitmap& bitmap = face->glyph->bitmap;
        glyph.sizeX = static_cast<int>(bitmap.width);
        glyph.sizeY = static_cast<int>(bitmap.rows);
        glyph.bearingX = face->glyph->bitmap_left;
        glyph.bearingY = face->glyph->bitmap_top;
        glyph.advance = static_cast<unsigned int>(face->glyph->advance.x);

        const unsigned int width = bitmap.width;
        const unsigned int height = bitmap.rows;
        if (width == 0 || height == 0) continue;

        if (penX + width + kAtlasPadding > kAtlasWidth) {
            penX = 0;
            penY += rowHeight + kAtlasPadding;
            rowHeight = 0;
        }

        if (penY + height > kAtlasMaxHeight) {
            Lit::Log::Warn("Font atlas is full, glyph {} was dropped", code);
            continue;
        }

        for (unsigned int row = 0; row < height; ++row) {
            std::memcpy(atlasPixels.data() + static_cast<size_t>(penY + row) * kAtlasWidth + penX, bitmap.buffer + static_cast<ptrdiff_t>(row) * bitmap.pitch, width);
        }

        glyph.u0 = static_cast<float>(penX);
        glyph.v0 = static_cast<float>(penY);
        glyph.u1 = static_cast<float>(penX + width);
        glyph.v1 = static_cast<float>(penY + height);
        glyph.valid = true;

        penX += width + kAtlasPadding;
        rowHeight = std::max(rowHeight, height);
        usedHeight = std::max(usedHeight, penY + height);
    }
    return std::max(usedHeight, 1u);
}

void normalizeGlyphUvs(DiligentUIData& uiData, unsigned int atlasHeight) {
    for (Glyph& glyph : uiData.glyphs) {
        if (!glyph.valid) continue;
        glyph.u0 /= static_cast<float>(kAtlasWidth);
        glyph.u1 /= static_cast<float>(kAtlasWidth);
        glyph.v0 /= static_cast<float>(atlasHeight);
        glyph.v1 /= static_cast<float>(atlasHeight);
    }
}

bool createAtlasTexture(DiligentUIData& uiData, const std::vector<unsigned char>& atlasPixels, unsigned int atlasHeight) {
    Diligent::TextureDesc textureDesc;
    textureDesc.Name = "Text Glyph Atlas";
    textureDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    textureDesc.Width = kAtlasWidth;
    textureDesc.Height = atlasHeight;
    textureDesc.Format = Diligent::TEX_FORMAT_R8_UNORM;
    textureDesc.Usage = Diligent::USAGE_IMMUTABLE;
    textureDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    textureDesc.MipLevels = 1;

    Diligent::TextureSubResData subresource;
    subresource.pData = atlasPixels.data();
    subresource.Stride = kAtlasWidth;

    Diligent::TextureData initialData;
    initialData.NumSubresources = 1;
    initialData.pSubResources = &subresource;

    uiData.device->CreateTexture(textureDesc, &initialData, &uiData.atlas);
    if (!uiData.atlas) {
        Lit::Log::Error("Failed to create the text glyph atlas");
        return false;
    }
    uiData.atlasView = uiData.atlas->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    return true;
}

void createConstantsBuffer(DiligentUIData& uiData) {
    Diligent::BufferDesc bufferDesc;
    bufferDesc.Name = "Text Constants Buffer";
    bufferDesc.Usage = Diligent::USAGE_DYNAMIC;
    bufferDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    bufferDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    bufferDesc.Size = sizeof(TextConstantBuffer);
    uiData.device->CreateBuffer(bufferDesc, nullptr, &uiData.constants);
}

}
