module;

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct IBuffer;
struct ITexture;
struct ITextureView;
enum TEXTURE_FORMAT : std::uint16_t;
}  // namespace Diligent

export module Engine.Render.lighting;

import Engine.glm;
import Engine.Render.settings;
import Engine.Render.material;

export struct SceneUniforms {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 viewProjection;
    glm::mat4 invView;
    glm::mat4 invProjection;
    glm::mat4 invViewProjection;
    glm::mat4 prevViewProjection;
    glm::vec4 frustumPlanes[6];
    glm::vec3 viewPos;
    float nearPlane;
    glm::vec4 screenParams;
    glm::vec4 timeParams;
    glm::vec4 sunDirection;
    glm::vec4 sunIlluminance;
    glm::vec4 exposureParams;
    glm::vec4 lightParams;
    glm::vec4 clusterParams;
    glm::mat4 cascadeViewProj[4];
    glm::vec4 cascadeSplits;
    glm::vec4 cascadeInfo[4];
    glm::vec4 shadowParams;
    glm::vec4 shadowParams2;
    glm::vec4 atmosphereParams;
    glm::vec4 skyParams;
    glm::vec4 skyParams2;
    glm::vec4 skyGradient[3];
    glm::vec4 renderFlags;
    glm::vec4 aoParams;
    glm::vec4 ssrParams;
    glm::vec4 fogParams;
    glm::vec4 postParams;
    glm::vec4 postParams2;
};

export struct GpuLight {
    glm::vec4 positionRange;
    glm::vec4 colorType;
    glm::vec4 directionCos;
    glm::vec4 params;
};

export struct MaterialGpu {
    glm::vec4 baseColorOpacity;
    glm::vec4 emissive;
    glm::vec4 pbr;
    glm::vec4 coat;
    glm::vec4 sheen;
    glm::vec4 subsurface;
    glm::vec4 transmission;
    int32_t textures[4];
    glm::vec4 flags;
    glm::vec4 custom0;
    glm::vec4 custom1;
};

export MaterialGpu packMaterial(const Material& m);

export struct Cascade {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 planes[6]{};
    float split = 0.0f;
    float texelsPerUnit = 1.0f;
    float texelWorld = 1.0f;
    float depthRange = 1.0f;
    bool due = true;
    bool valid = false;
};

export struct FrameInput {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float fovY = 45.0f;
    float aspect = 1.0f;
    float time = 0.0f;
    float deltaTime = 0.016f;
    uint32_t frame = 0;
    int width = 1;
    int height = 1;
};

export class Lighting {
  public:
    Lighting();
    ~Lighting();
    Lighting(const Lighting&) = delete;
    Lighting& operator=(const Lighting&) = delete;

    bool init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::TEXTURE_FORMAT backBufferFormat, int width, int height);
    void resize(int width, int height);
    void reloadShaders();
    void shutdown();

    [[nodiscard]] RenderSettings& settings();
    void setMaterial(uint32_t id, const Material& material);
    void setLights(const std::vector<GpuLight>& lights);
    void setDirectionalLight(bool present, const glm::vec3& direction, const glm::vec3& illuminance);
    int loadTexture(const std::string& path, bool srgb);
    [[nodiscard]] uint64_t bindingVersion() const;

    void beginFrame(const FrameInput& input, SceneUniforms& uniforms);
    [[nodiscard]] const std::array<Cascade, 4>& cascades() const;
    [[nodiscard]] int cascadeCount() const;
    [[nodiscard]] int shadowResolution() const;
    void prepare(Diligent::IBuffer* sceneUniforms);
    void shade(Diligent::ITexture* depth, Diligent::IBuffer* sceneUniforms, int stage = -1);
    void finish(Diligent::IBuffer* sceneUniforms, Diligent::ITextureView* backBuffer, float deltaTime);

    [[nodiscard]] Diligent::ITexture* gbuffer(int index) const;
    [[nodiscard]] Diligent::ITexture* hdr() const;
    [[nodiscard]] Diligent::ITexture* hdrTarget(int index) const;
    [[nodiscard]] int hdrIndex() const;
    void setHdrIndex(int index);
    [[nodiscard]] Diligent::ITexture* shadowMap() const;
    [[nodiscard]] Diligent::ITextureView* shadowTarget(int cascade) const;
    [[nodiscard]] Diligent::IBuffer* materialBuffer() const;
    [[nodiscard]] Diligent::IBuffer* lightBuffer() const;
    [[nodiscard]] Diligent::IBuffer* clusterBuffer() const;
    [[nodiscard]] Diligent::IBuffer* exposureBuffer() const;
    [[nodiscard]] Diligent::IBuffer* irradianceBuffer() const;
    [[nodiscard]] Diligent::ITextureView* prefilteredEnvironment() const;
    [[nodiscard]] Diligent::ITextureView* dfgLut() const;
    [[nodiscard]] Diligent::ITextureView* colorTextures() const;
    [[nodiscard]] Diligent::ITextureView* dataTextures() const;
    [[nodiscard]] float currentEv100() const;
    [[nodiscard]] glm::vec3 sunDirection() const;

  private:
    struct Data;
    Data* m_data = nullptr;
};
