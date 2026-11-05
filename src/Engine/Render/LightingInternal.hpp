#pragma once

namespace lighting_detail {

inline constexpr Diligent::TEXTURE_FORMAT kHdrFormat = Diligent::TEX_FORMAT_RGBA16_FLOAT;
inline constexpr Diligent::TEXTURE_FORMAT kLdrFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
inline constexpr uint32_t kMaxMaterials = 1024;
inline constexpr uint32_t kMaxLights = 1024;
inline constexpr uint32_t kClusterCount = 16 * 9 * 24;
inline constexpr uint32_t kClusterStride = 128;
inline constexpr uint32_t kEnvSize = 256;
inline constexpr uint32_t kSkyViewWidth = 384;
inline constexpr uint32_t kSkyViewHeight = 216;
inline constexpr uint32_t kEnvMips = 6;
inline constexpr uint32_t kBloomLevels = 6;
inline constexpr uint32_t kPyramidMips = 7;
inline constexpr uint32_t kTextureSize = 1024;
inline constexpr uint32_t kTextureMips = 11;
inline constexpr uint32_t kTextureCapacity = 16;
inline constexpr uint32_t kCloudNoiseMips = 8;
inline constexpr uint32_t kThreadGroupSize = 8;
inline constexpr int kCascadeSlots = 4;
inline constexpr float kBottomRadius = 6360.0f;
inline constexpr float kTopRadius = 6460.0f;
inline constexpr float kCameraAltitude = 0.15f;
inline constexpr float kPi = 3.14159265f;

using litgpu::Named;
using litgpu::NamedSampler;
using Diligent::RefCntAutoPtr;

struct ExposureConstants {
    glm::vec4 range;
    glm::vec4 adapt;
    glm::vec4 speed;
};

}

#include "LightingReflections.hpp"

struct Lighting::Data {
    Diligent::IRenderDevice* device = nullptr;
    Diligent::IDeviceContext* context = nullptr;
    Diligent::TEXTURE_FORMAT backBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
    int width = 1;
    int height = 1;
    int outputWidth = 1;
    int outputHeight = 1;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> upscaled[2];
    lighting_detail::RefCntAutoPtr<Diligent::IPipelineState> easuPso, rcasPso;
    lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding> easuSrb, rcasSrb;
    RenderSettings settings;
    uint64_t bindingVersion = 1;

    lighting_detail::RefCntAutoPtr<Diligent::ITexture> gbuffer[3];
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> hdrTargets[2];
    int hdrIndex = 0;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> ldr;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> aoRaw;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> aoDepth;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> clouds;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> cloudNoise;
    lighting_detail::RefCntAutoPtr<Diligent::IPipelineState> cloudNoisePso, cloudNoiseDownPso;
    bool cloudNoiseDirty = true;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> godRays[2];
    lighting_detail::RefCntAutoPtr<Diligent::IPipelineState> godRayMaskPso, godRayBlurPso, godRayCompositePso;
    lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding> godRayMaskSrb, godRayBlurSrb[2], godRayCompositeSrb[2];
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> godRayConstants;
    uint64_t godRayVersion = 0;
    Diligent::ITexture* godRayDepth = nullptr;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> aoFiltered;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> aoTemp;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> aoBentRaw, aoBentTemp, aoBent;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> sceneColor;
    std::vector<lighting_detail::RefCntAutoPtr<Diligent::ITextureView>> sceneColorMips;
    lighting_detail::RefCntAutoPtr<Diligent::IPipelineState> pyramidFirstPso, pyramidPso, gtaoBlurVerticalPso;
    lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding> pyramidFirstSrb[2], gtaoBlurVerticalSrb;
    std::vector<lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding>> pyramidSrbs;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> ssrHiz;
    std::vector<lighting_detail::RefCntAutoPtr<Diligent::ITextureView>> ssrHizMips;
    lighting_detail::RefCntAutoPtr<Diligent::IPipelineState> ssrHizFirstPso, ssrHizPso;
    lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding> ssrHizFirstSrb;
    std::vector<lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding>> ssrHizSrbs;
    uint64_t pyramidVersion = 0;
    bool pyramidValid = false;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> ssr;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> bloomDown[lighting_detail::kBloomLevels];
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> bloomUp[lighting_detail::kBloomLevels];

    lighting_detail::RefCntAutoPtr<Diligent::ITexture> transmittanceLut;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> multiScatterLut;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> skyViewLut;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> aerialLut;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> envCapture;
    lighting_detail::RefCntAutoPtr<Diligent::ITextureView> envCaptureMips[lighting_detail::kEnvMips + 2];
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> envPrefiltered;
    lighting_detail::RefCntAutoPtr<Diligent::ITextureView> envPrefilteredMips[lighting_detail::kEnvMips];
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> dfg;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> hdri;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> shadowMap;
    lighting_detail::RefCntAutoPtr<Diligent::ITextureView> shadowTargets[kShadowLayers];
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> colorTextures;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> dataTextures;
    int textureCount = 0;
    int shadowResolution = 0;
    lighting_detail::RefCntAutoPtr<Diligent::ITexture> localShadowMap;
    lighting_detail::RefCntAutoPtr<Diligent::ITextureView> localShadowTargets[kMaxLocalShadowViews];
    int localShadowResolution = 0;
    std::array<LocalShadowView, kMaxLocalShadowViews> localViews{};
    std::array<LocalShadowCaster, kMaxLocalShadowLights> localCasters{};
    int localCasterCount = 0;
    float localTanHalf = 1.0f;
    std::vector<GpuLight> cpuLights;
    std::vector<uint32_t> shadowedLights;

    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> materials;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> lights;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> clusters;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> exposure;
    bool snapExposure = false;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> histogram;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> irradiance;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> exposureConstants;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> prefilterConstants;
    lighting_detail::RefCntAutoPtr<Diligent::IBuffer> bloomConstants;

    lighting_detail::RefCntAutoPtr<Diligent::IPipelineState> transmittancePso, multiScatterPso, skyViewPso, aerialPso, envCapturePso, envDownsamplePso, envPrefilterPso, envShPso, dfgPso, lightCullPso, cloudsPso, gtaoDepthPso, gtaoPso, gtaoBlurPso, ssrPso, lightingPso, histogramPso,
        exposurePso, commitPso, bloomDownPso, bloomUpPso, tonemapPso, fxaaPso;
    lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding> transmittanceSrb, multiScatterSrb, skyViewSrb, aerialSrb, envCaptureSrb, envShSrb, dfgSrb, lightCullSrb, cloudsSrb, gtaoDepthSrb, gtaoSrb, gtaoBlurSrb, exposureSrb, commitSrb, fxaaSrb;
    lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding> ssrSrb[2], lightingSrb[2], histogramSrb[2], tonemapSrb[2], bloomSourceSrb[2];
    std::vector<lighting_detail::RefCntAutoPtr<Diligent::IShaderResourceBinding>> envDownsampleSrbs, envPrefilterSrbs, bloomDownSrbs, bloomUpSrbs;

    lighting_detail::ReflectionGpu reflections;
    uint64_t envVersion = 0;
    uint32_t lightCount = 0;
    bool lutsDirty = true;
    bool dfgDirty = true;
    bool envDirty = true;
    uint64_t skyHash = 0;
    uint32_t frame = 0;
    glm::vec2 cloudOffset{0.0f};
    float cloudTime = 0.0f;
    float cloudTravelSinceCapture = 0.0f;
    float nightDarkening = 0.0f;
    bool hasDirectional = false;
    glm::vec3 directionalDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 directionalIlluminance{0.0f};
    glm::vec3 sun{0.0f, 1.0f, 0.0f};
    std::array<Cascade, 4> cascades{};
    float lastEv = 12.0f;
    std::string hdriPath;
    int hdriLoaded = 0;
    uint64_t atmosphereHash = 0;
    uint64_t shadeVersion = 0;
    uint64_t finishVersion = 0;
    Diligent::ITexture* shadeDepth = nullptr;
    Diligent::ITexture* tonemapDepth = nullptr;
    int colorLayers = 0;
    int dataLayers = 0;
};

namespace lighting_detail {

glm::vec3 cpuTransmittance(float sunZenithCos, const RenderSettings& settings);
void extractPlanes(const glm::mat4& viewProjection, glm::vec4* planes);
void createShadowArray(Diligent::IRenderDevice* device, const char* name, uint32_t resolution, uint32_t layers, RefCntAutoPtr<Diligent::ITexture>& texture, RefCntAutoPtr<Diligent::ITextureView>* targets);
void loadHdri(Lighting::Data& state);
void createReflectionResources(Lighting::Data& state);
void loadReflectionPipelines(Lighting::Data& state);
void updateReflectionAssets(Lighting::Data& state, Diligent::IBuffer* scene);
bool upscaleActive(const Lighting::Data& state);
Diligent::ITexture* finalColorTexture(const Lighting::Data& state);
void createUpscaleBindings(Lighting::Data& state, Diligent::IBuffer* scene);
void runUpscale(Lighting::Data& state);
void runReflectionComposite(Lighting::Data& state, Diligent::IBuffer* scene, uint32_t groupsX, uint32_t groupsY);

}
