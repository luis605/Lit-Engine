module;

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct IDeviceObject;
struct ITexture;
struct ITextureView;
}  // namespace Diligent

export module Engine.Render.custom;

import Engine.glm;

export enum class ComputeStage { BeforeScene, AfterLighting, BeforePost };
export enum class DrawPrimitive { Points, Lines, Triangles, TriangleStrip };
export enum class DrawBlend { Opaque, Alpha, Additive };

export struct ComputeDesc {
    std::string name;
    std::string path;
    glm::ivec3 groups{1, 1, 1};
    ComputeStage stage = ComputeStage::BeforeScene;
    bool operator==(const ComputeDesc&) const = default;
};

export struct DrawDesc {
    std::string name;
    std::string vertexPath;
    std::string fragmentPath;
    std::string meshPath;
    std::string taskPath;
    DrawPrimitive primitive = DrawPrimitive::Triangles;
    uint32_t vertexCount = 3;
    uint32_t instanceCount = 1;
    glm::ivec3 meshGroups{1, 1, 1};
    DrawBlend blend = DrawBlend::Opaque;
    bool depthTest = true;
    bool depthWrite = true;
    bool doubleSided = true;
    bool operator==(const DrawDesc&) const = default;
};

export struct PostEffectDesc {
    std::string name;
    std::string path;
    bool operator==(const PostEffectDesc&) const = default;
};

export struct NamedResource {
    std::string name;
    Diligent::IDeviceObject* object = nullptr;
};

export struct CustomFrame {
    std::vector<NamedResource> resources;
    uint32_t frameSlot = 0;
    uint32_t parity = 0;
    float time = 0.0f;
    float deltaTime = 0.0f;
    uint32_t frame = 0;
    int width = 1;
    int height = 1;
};

export enum class PassKind { Compute, Draw, Effect };

export struct PassInfo {
    PassKind kind = PassKind::Compute;
    uint32_t id = 0;
    std::string name;
    bool enabled = true;
    bool valid = false;
    glm::vec4 params[4]{};
};

export class CustomPasses {
  public:
    CustomPasses();
    ~CustomPasses();
    CustomPasses(const CustomPasses&) = delete;
    CustomPasses& operator=(const CustomPasses&) = delete;

    void init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context);
    void shutdown();
    void reload();
    void clearBindings();

    uint32_t addCompute(const ComputeDesc& desc);
    uint32_t addDraw(const DrawDesc& desc);
    uint32_t addEffect(const PostEffectDesc& desc);
    void setBuffer(const std::string& name, uint32_t stride, uint32_t count, const void* data);
    [[nodiscard]] std::vector<uint8_t> readBuffer(const std::string& name);
    void setParam(PassKind kind, uint32_t id, int index, const glm::vec4& value);
    void setEnabled(PassKind kind, uint32_t id, bool enabled);
    void setComputeGroups(uint32_t id, const glm::ivec3& groups);
    void setDrawCounts(uint32_t id, uint32_t vertexCount, uint32_t instanceCount);
    [[nodiscard]] std::vector<PassInfo> passes() const;
    void save(std::ostream& out, std::string_view prefix) const;
    bool load(std::string_view line);
    [[nodiscard]] bool meshShadersSupported() const;
    [[nodiscard]] bool hasEffects() const;

    void runCompute(ComputeStage stage, const CustomFrame& frame);
    void runDraws(const CustomFrame& frame, Diligent::ITextureView* colorTarget, Diligent::ITextureView* depthTarget);
    int runEffects(const CustomFrame& frame, Diligent::ITexture* const targets[2], int current);

  private:
    struct Data;
    Data* m_data = nullptr;
};
