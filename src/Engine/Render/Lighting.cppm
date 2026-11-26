module;

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct IBuffer;
struct ITexture;
struct ITextureView;
enum TEXTURE_FORMAT : std::uint16_t;
}

export module Engine.Render.lighting;

import Engine.glm;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Render.graph;
export import Engine.Render.reflection;

export constexpr int kMaxLocalShadowViews = 24;
export constexpr int kMaxLocalShadowLights = 8;

export inline constexpr double kOriginPatternPeriod = 4096.0;

export inline constexpr uint32_t kBindlessTextureCapacity = 1024;

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
    glm::vec4 maskParams;
    glm::vec4 maskColor;
    glm::vec4 vignetteParams;
    glm::vec4 bleedParams;
    glm::vec4 qualityParams;
    glm::vec4 qualityParams2;
    glm::vec4 vsmParams;
    glm::vec4 vsmParams2;
    glm::vec4 originParams;
    glm::vec4 cellParams;
    glm::vec4 cellTint;
};

export struct LightAperture {
    glm::vec4 center;
    glm::vec4 axisU;
    glm::vec4 axisV;
    glm::vec4 plane;
};

export constexpr uint32_t kMaxPortalLights = 1024;

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

export MaterialGpu packMaterial(const Material& material);

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

export struct VirtualShadowInputs {
    Diligent::IBuffer* sceneUniforms = nullptr;
    Diligent::IBuffer* worldMatrices = nullptr;
    Diligent::IBuffer* cullSpheres = nullptr;
    Diligent::IBuffer* renderables = nullptr;
    Diligent::IBuffer* meshInfos = nullptr;
    Diligent::IBuffer* dynamicStamps = nullptr;
    Diligent::IBuffer* vertices = nullptr;
    Diligent::IBuffer* indices = nullptr;
    Diligent::ITexture* depth = nullptr;
    uint32_t objectCount = 0;
    uint32_t opaqueShaderCount = 0;
    float lodBias = 1.0f;
};

export struct VirtualShadowStats {
    uint32_t faces = 0;
    uint32_t requests = 0;
    uint32_t renderedPages = 0;
    uint32_t residentPages = 0;
    uint32_t poolPages = 0;
    uint32_t overflow = 0;
    uint32_t casterInstances = 0;
    uint32_t movedObjects = 0;
    float resolutionScale = 1.0f;
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
    glm::dvec3 worldOrigin{0.0};
};

export enum class LightingStage : uint8_t { Prepare, AmbientOcclusion, Lighting, Reflections, Post };

export class Lighting {
  public:
    Lighting();
    ~Lighting();
    Lighting(const Lighting&) = delete;
    Lighting& operator=(const Lighting&) = delete;

    bool init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::TEXTURE_FORMAT backBufferFormat, int width, int height);
    void resize(int width, int height, int outputWidth = 0, int outputHeight = 0);
    void reloadShaders();
    void shutdown();

    [[nodiscard]] RenderSettings& settings();
    [[nodiscard]] const RenderSettings& settings() const;
    void setMaterial(uint32_t id, const Material& material);
    void setLights(const std::vector<GpuLight>& lights);
    void setLightCells(const std::vector<int32_t>& cells);
    void setLightApertures(uint32_t first, const std::vector<LightAperture>& apertures);
    void setDirectionalLight(bool present, const glm::vec3& direction, const glm::vec3& illuminance);
    int loadTexture(const std::string& path, bool srgb);
    void releaseTexture(int slot);
    [[nodiscard]] bool bindlessTextures() const;
    [[nodiscard]] uint32_t textureTableHighWater() const;
    [[nodiscard]] uint32_t textureTableUsed() const;
    [[nodiscard]] uint64_t textureTableEpoch() const;
    [[nodiscard]] uint64_t textureSlotEpoch(uint32_t slot) const;
    [[nodiscard]] Diligent::ITextureView* textureTableView(uint32_t slot) const;
    [[nodiscard]] uint64_t bindingVersion() const;

    void shiftOrigin(const glm::vec3& delta);
    void beginFrame(const FrameInput& input, SceneUniforms& uniforms);
    void fitShadows(const FrameInput& input, SceneUniforms& uniforms);
    void useCascades(const std::array<Cascade, 4>& cascades, SceneUniforms& uniforms);
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
    [[nodiscard]] bool virtualShadowsActive() const;
    void renderVirtualShadows(const VirtualShadowInputs& inputs);
    [[nodiscard]] VirtualShadowStats virtualShadowStats() const;
    [[nodiscard]] Diligent::ITexture* virtualShadowAtlas() const;
    [[nodiscard]] Diligent::IBuffer* virtualShadowFaces() const;
    [[nodiscard]] Diligent::IBuffer* virtualShadowPageTable() const;
    void prepare(Diligent::IBuffer* sceneUniforms);
    void cullLights(Diligent::IBuffer* sceneUniforms);
    void shade(Diligent::ITexture* depth, Diligent::IBuffer* sceneUniforms, int stage = -1);
    void captureSceneColor();
    void graphResources(LightingStage stage, std::vector<rg::ExternalAccess>& out) const;
    [[nodiscard]] void* graphResource(std::string_view name) const;
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
    [[nodiscard]] Diligent::ITexture* sceneColorTexture() const;
    [[nodiscard]] bool pyramidValid() const;
    void setPyramidValid(bool valid);
    void setReflectionScene(const ReflectionScene& scene);
    void bakeProbes();
    void snapExposure();
    [[nodiscard]] Diligent::ITextureView* colorTextures() const;
    [[nodiscard]] Diligent::ITextureView* dataTextures() const;
    [[nodiscard]] float currentEv100() const;
    [[nodiscard]] glm::vec3 sunDirection() const;

    struct Data;

  private:
    void selectLocalShadows(SceneUniforms& uniforms, const glm::vec3& cameraPosition);
    Data* m_data = nullptr;
};
