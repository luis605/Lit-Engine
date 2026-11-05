#pragma once

namespace {

inline constexpr unsigned int kGlyphCount = 128;
inline constexpr unsigned int kAtlasWidth = 1024;
inline constexpr unsigned int kAtlasMaxHeight = 1024;
inline constexpr unsigned int kAtlasPadding = 2;
inline constexpr unsigned int kInitialGlyphCapacity = 2048;
inline constexpr unsigned int kVerticesPerGlyph = 6;
inline constexpr unsigned int kGlyphPixelHeight = 48;
inline constexpr unsigned int kFixedPointShift = 6;
inline constexpr size_t kInitialBatchCapacity = 64;
inline constexpr size_t kInitialTextCapacity = 64;
inline constexpr size_t kInitialArenaCapacity = 4096;

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

struct TextConstantBuffer {
    glm::mat4 projection;
    glm::vec4 textColor;
};

struct DiligentUIData {
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> resourceBinding;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertexBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constants;
    Diligent::RefCntAutoPtr<Diligent::ITexture> atlas;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> atlasView;

    Glyph glyphs[kGlyphCount];

    std::vector<TextVertex> vertexScratch;
    std::vector<DrawBatch> batches;

    Diligent::Uint32 vertexCapacity = 0;
};

struct FreeTypeFace {
    FT_Library library = nullptr;
    FT_Face face = nullptr;

    FreeTypeFace() = default;
    FreeTypeFace(const FreeTypeFace&) = delete;
    FreeTypeFace& operator=(const FreeTypeFace&) = delete;
    ~FreeTypeFace() {
        if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
    }
};

}
