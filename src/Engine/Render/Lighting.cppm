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

// World-space procedural patterns (noise, checker, wind, triplanar) sample local + origin reduced modulo this period, so they
// stay continuous across floating-origin rebases for any pattern period that divides it (snap sizes of 1024 and below do).
export inline constexpr double kOriginPatternPeriod = 4096.0;

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
    glm::vec4 originParams; // xyz world origin reduced modulo kOriginPatternPeriod (float, pattern phase), w world origin y (altitude)
    glm::vec4 cellParams;   // x portal cell of this view (-1: no cell filtering of local lights), y first portal light + 1 (0: none),
                            // z > 0: shared sun cascades (containment selection up to this view depth), w unused
    glm::vec4 cellTint;     // rgb lighting grade of the view's cell (sun and ambient), w unused
};

// Lights that reach a cell through a portal (docs/Portals.md, "Light through portals") are ordinary lights placed at the
// portal image of the source light. Their aperture limits them to rays through the portal quad: shading tests the segment
// from the receiver to the light against the quad, and virtual shadow faces clip casters behind the portal plane.
export struct LightAperture {
    glm::vec4 center;  // xyz quad centre, w edge softness (metres)
    glm::vec4 axisU;   // xyz half-extent vector along the quad's right axis
    glm::vec4 axisV;   // xyz half-extent vector along the quad's up axis
    glm::vec4 plane;   // xyz unit normal toward the receiving cell, w = -dot(normal, centre)
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

// Renderer-owned inputs of the virtual shadow map passes (one call per rendered view, after its depth pass).
export struct VirtualShadowInputs {
    Diligent::IBuffer* sceneUniforms = nullptr;
    Diligent::IBuffer* worldMatrices = nullptr;
    Diligent::IBuffer* cullSpheres = nullptr;
    Diligent::IBuffer* renderables = nullptr;
    Diligent::IBuffer* meshInfos = nullptr;      // unpatched by cluster LOD: cluster meshes cast shadows from their triangle LODs
    Diligent::IBuffer* dynamicStamps = nullptr;  // per entity version of deformed vertices (optional)
    Diligent::IBuffer* vertices = nullptr;
    Diligent::IBuffer* indices = nullptr;
    Diligent::ITexture* depth = nullptr;
    uint32_t objectCount = 0;
    uint32_t opaqueShaderCount = 0;
    float lodBias = 1.0f;
};

// Counters of the last finished frame (read back a few frames late; diagnostics only).
export struct VirtualShadowStats {
    uint32_t faces = 0;
    uint32_t requests = 0;
    uint32_t renderedPages = 0;
    uint32_t residentPages = 0;
    uint32_t poolPages = 0;
    uint32_t overflow = 0;
    uint32_t casterInstances = 0;
    uint32_t movedObjects = 0;
    float resolutionScale = 1.0f;  // effective vsmResolutionScale (automatic from the light count when vsmAutoScale)
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
    glm::dvec3 worldOrigin{0.0}; // Engine::worldOrigin(): world = local + worldOrigin (docs/LargeWorld.md)
};

// Stages of the frame as the render graph sees them (Renderer::renderFrameGraph, docs/RenderGraph.md).
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
    // Portal cell of every light (same order as setLights, -1 = outside every cell). Empty disables cell filtering.
    void setLightCells(const std::vector<int32_t>& cells);
    // Portal lights: lights first .. first + apertures.size() - 1 of setLights are portal images limited to these apertures.
    void setLightApertures(uint32_t first, const std::vector<LightAperture>& apertures);
    void setDirectionalLight(bool present, const glm::vec3& direction, const glm::vec3& illuminance);
    int loadTexture(const std::string& path, bool srgb);
    [[nodiscard]] uint64_t bindingVersion() const;

    // Floating origin rebase: every position the renderer holds is in the old frame; `delta` is what Engine subtracted from local space.
    void shiftOrigin(const glm::vec3& delta);
    void beginFrame(const FrameInput& input, SceneUniforms& uniforms);
    void fitShadows(const FrameInput& input, SceneUniforms& uniforms);
    // Sun cascades fitted for another view of the same frame (portal views share one set, docs/Portals.md): makes them the
    // current cascades and writes their matrices into `uniforms`.
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
    // Internal textures and buffers a stage touches, with the state of their first use (rg::Usage::None when a stage
    // uses a resource in several states). The renderer declares scene depth, G-buffer and HDR targets itself.
    void graphResources(LightingStage stage, std::vector<rg::ExternalAccess>& out) const;
    // Current object behind a graphResources name (resources can be recreated while the frame runs).
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
