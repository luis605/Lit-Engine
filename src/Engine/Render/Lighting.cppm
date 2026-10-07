module;

#include <array>
#include <cstdint>
#include <functional>
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

export constexpr int kMaxLocalShadowViews = 24;
export constexpr int kMaxLocalShadowLights = 8;

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
    glm::mat4 localShadowMatrices[kMaxLocalShadowViews];
    glm::vec4 localShadowParams;
    glm::vec4 localShadowParams2;
    glm::vec4 giParams;
    glm::vec4 moonDirection;
    glm::vec4 celestialParams;
    glm::vec4 cloudParams;
    glm::vec4 skyParams3;
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

export constexpr int kShadowLayers = 6;

export struct Cascade {
    glm::mat4 viewProjection{1.0f};
    glm::mat4 renderViewProjection{1.0f};
    glm::vec4 planes[6]{};
    int viewport[4]{};
    float split = 0.0f;
    float texelsPerUnit = 1.0f;
    float texelWorld = 1.0f;
    float depthRange = 1.0f;
    int layer = 0;
    int renderLayer = 0;
    bool due = true;
    bool clear = true;
    bool valid = false;
};

export struct LocalShadowView {
    glm::mat4 viewProjection{1.0f};
    glm::vec3 origin{0.0f};
    float texelsAtUnitDistance = 1.0f;
    float projectionScale = 1.0f;
    bool active = false;
};

export struct LocalShadowCaster {
    glm::vec4 sphere{0.0f};
    glm::vec4 cone{0.0f};
    uint32_t faceBase = 0;
    uint32_t faceMask = 0;
    uint32_t spot = 0;
    uint32_t pad = 0;
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
    [[nodiscard]] const std::array<LocalShadowView, kMaxLocalShadowViews>& localShadowViews() const;
    [[nodiscard]] const std::array<LocalShadowCaster, kMaxLocalShadowLights>& localShadowCasters() const;
    [[nodiscard]] int localShadowCasterCount() const;
    [[nodiscard]] int localShadowResolution() const;
    [[nodiscard]] float localShadowTanHalf() const;
    [[nodiscard]] Diligent::ITexture* localShadowMap() const;
    [[nodiscard]] Diligent::ITextureView* localShadowTarget(int view) const;
    void prepare(Diligent::IBuffer* sceneUniforms);
    void cullLights(Diligent::IBuffer* sceneUniforms);
    void shade(Diligent::ITexture* depth, Diligent::IBuffer* sceneUniforms, int stage = -1);
    void captureSceneColor();
    void finish(Diligent::IBuffer* sceneUniforms, Diligent::ITextureView* backBuffer, float deltaTime, const std::function<void(Diligent::ITexture*)>& afterTonemap = {});

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
    [[nodiscard]] Diligent::ITextureView* sceneColor() const;
    [[nodiscard]] Diligent::ITextureView* colorTextures() const;
    [[nodiscard]] Diligent::ITextureView* dataTextures() const;
    [[nodiscard]] float currentEv100() const;
    [[nodiscard]] glm::vec3 sunDirection() const;

  private:
    void selectLocalShadows(SceneUniforms& u, const glm::vec3& cameraPosition);
    struct Data;
    Data* m_data = nullptr;
};
