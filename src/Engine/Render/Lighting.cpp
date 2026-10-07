module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

module Engine.Render.lighting;

import Engine.glm;
import Engine.Render.settings;
import Engine.Render.material;

namespace {

constexpr Diligent::TEXTURE_FORMAT kHdrFormat = Diligent::TEX_FORMAT_RGBA16_FLOAT;
constexpr Diligent::TEXTURE_FORMAT kLdrFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
constexpr uint32_t kMaxMaterials = 1024;
constexpr uint32_t kMaxLights = 1024;
constexpr uint32_t kClusterCount = 16 * 9 * 24;
constexpr uint32_t kClusterStride = 128;
constexpr uint32_t kEnvSize = 256;
constexpr uint32_t kSkyViewWidth = 384;
constexpr uint32_t kSkyViewHeight = 216;
constexpr uint32_t kEnvMips = 6;
constexpr uint32_t kBloomLevels = 6;
constexpr uint32_t kPyramidMips = 7;
constexpr uint32_t kTextureSize = 1024;
constexpr uint32_t kTextureMips = 11;
constexpr uint32_t kTextureCapacity = 16;
constexpr float kBottomRadius = 6360.0f;
constexpr float kTopRadius = 6460.0f;
constexpr float kCameraAltitude = 0.15f;

using litgpu::Named;
using litgpu::NamedSampler;
using Diligent::RefCntAutoPtr;

struct ExposureConstants {
    glm::vec4 range;
    glm::vec4 adapt;
    glm::vec4 speed;
};

glm::vec3 cpuTransmittance(float sunZenithCos, const RenderSettings& s) {
    const float r = kBottomRadius + kCameraAltitude;
    const glm::vec3 pos(0.0f, r, 0.0f);
    const glm::vec3 dir(std::sqrt(std::max(0.0f, 1.0f - sunZenithCos * sunZenithCos)), sunZenithCos, 0.0f);
    const float b = glm::dot(pos, dir);
    const float cTop = glm::dot(pos, pos) - kTopRadius * kTopRadius;
    const float cBottom = glm::dot(pos, pos) - kBottomRadius * kBottomRadius;
    const float dBottom = b * b - cBottom;
    if (dBottom >= 0.0f && -b - std::sqrt(dBottom) > 0.0f) return glm::vec3(0.0f);
    const float tMax = -b + std::sqrt(std::max(b * b - cTop, 0.0f));
    const glm::vec3 rayleigh = glm::vec3(5.802f, 13.558f, 33.1f) * 1e-3f * s.rayleighScale;
    const glm::vec3 ozone = glm::vec3(0.650f, 1.881f, 0.085f) * 1e-3f * s.ozoneScale;
    const float mieExtinction = 4.440e-3f * s.mieScale;
    glm::vec3 depth(0.0f);
    constexpr int steps = 48;
    const float dt = tMax / steps;
    for (int i = 0; i < steps; ++i) {
        const glm::vec3 p = pos + dir * ((static_cast<float>(i) + 0.5f) * dt);
        const float h = std::max(glm::length(p) - kBottomRadius, 0.0f);
        const float dO = std::max(0.0f, 1.0f - std::abs(h - 25.0f) / 15.0f);
        depth += (rayleigh * std::exp(-h / 8.0f) + glm::vec3(mieExtinction * std::exp(-h / 1.2f)) + ozone * dO) * dt;
    }
    return glm::vec3(std::exp(-depth.x), std::exp(-depth.y), std::exp(-depth.z));
}

void extractPlanes(const glm::mat4& vp, glm::vec4* planes) {
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    planes[4] = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);
    for (int i = 0; i < 6; ++i) planes[i] /= glm::length(glm::vec3(planes[i]));
}

glm::mat4 orthoZO(float halfWidth, float nearZ, float farZ) {
    glm::mat4 m(1.0f);
    m[0][0] = 1.0f / halfWidth;
    m[1][1] = 1.0f / halfWidth;
    m[2][2] = -1.0f / (farZ - nearZ);
    m[3][2] = -nearZ / (farZ - nearZ);
    return m;
}

glm::vec3 sunFromTime(float hours, float azimuthDegrees) {
    const float dayPhase = (hours - 6.0f) / 12.0f * 3.14159265f;
    const float elevation = std::sin(dayPhase) * glm::radians(62.0f);
    const float azimuth = glm::radians(azimuthDegrees) + (hours - 12.0f) / 12.0f * 3.14159265f * 0.5f;
    return glm::normalize(glm::vec3(std::cos(elevation) * std::sin(azimuth), std::sin(elevation), std::cos(elevation) * std::cos(azimuth)));
}

}  // namespace

MaterialGpu packMaterial(const Material& m) {
    MaterialGpu g{};
    g.baseColorOpacity = glm::vec4(m.baseColor, std::clamp(m.opacity, 0.0f, 1.0f));
    g.emissive = glm::vec4(m.emissive * std::max(m.emissiveIntensity, 0.0f), m.normalStrength);
    g.pbr = glm::vec4(std::clamp(m.metallic, 0.0f, 1.0f), std::clamp(m.roughness, 0.0f, 1.0f), std::clamp(m.reflectance, 0.0f, 1.0f), std::clamp(m.occlusion, 0.0f, 1.0f));
    g.coat = glm::vec4(std::clamp(m.clearcoat, 0.0f, 1.0f), std::clamp(m.clearcoatRoughness, 0.0f, 1.0f), std::clamp(m.anisotropy, -1.0f, 1.0f), m.anisotropyRotation);
    g.sheen = glm::vec4(m.sheenColor, m.sheenRoughness);
    g.subsurface = glm::vec4(m.subsurfaceColor, std::clamp(m.subsurface, 0.0f, 1.0f));
    g.transmission = glm::vec4(std::clamp(m.transmission, 0.0f, 1.0f), m.ior, std::clamp(m.thickness, 0.0f, 1.0f), std::max(m.uvScale, 1e-4f));
    g.textures[0] = m.baseColorTexture;
    g.textures[1] = m.normalTexture;
    g.textures[2] = m.ormTexture;
    g.textures[3] = m.emissiveTexture;
    g.flags = glm::vec4(m.triplanar ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    g.custom0 = m.custom0;
    g.custom1 = m.custom1;
    return g;
}

struct Lighting::Data {
    Diligent::IRenderDevice* device = nullptr;
    Diligent::IDeviceContext* context = nullptr;
    Diligent::TEXTURE_FORMAT backBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
    int width = 1;
    int height = 1;
    RenderSettings settings;
    uint64_t bindingVersion = 1;

    RefCntAutoPtr<Diligent::ITexture> gbuffer[3];
    RefCntAutoPtr<Diligent::ITexture> hdrTargets[2];
    int hdrIndex = 0;
    RefCntAutoPtr<Diligent::ITexture> ldr;
    RefCntAutoPtr<Diligent::ITexture> aoRaw;
    RefCntAutoPtr<Diligent::ITexture> aoDepth;
    RefCntAutoPtr<Diligent::ITexture> clouds;
    RefCntAutoPtr<Diligent::ITexture> cloudNoise;
    RefCntAutoPtr<Diligent::IPipelineState> cloudNoisePso, cloudNoiseDownPso;
    bool cloudNoiseDirty = true;
    RefCntAutoPtr<Diligent::ITexture> godRays[2];
    RefCntAutoPtr<Diligent::IPipelineState> godRayMaskPso, godRayBlurPso, godRayCompositePso;
    RefCntAutoPtr<Diligent::IShaderResourceBinding> godRayMaskSrb, godRayBlurSrb[2], godRayCompositeSrb[2];
    RefCntAutoPtr<Diligent::IBuffer> godRayConstants;
    uint64_t godRayVersion = 0;
    Diligent::ITexture* godRayDepth = nullptr;
    RefCntAutoPtr<Diligent::ITexture> aoFiltered;
    RefCntAutoPtr<Diligent::ITexture> aoTemp;
    RefCntAutoPtr<Diligent::ITexture> sceneColor;
    std::vector<RefCntAutoPtr<Diligent::ITextureView>> sceneColorMips;
    RefCntAutoPtr<Diligent::IPipelineState> pyramidFirstPso, pyramidPso, gtaoBlurVerticalPso;
    RefCntAutoPtr<Diligent::IShaderResourceBinding> pyramidFirstSrb[2], gtaoBlurVerticalSrb;
    std::vector<RefCntAutoPtr<Diligent::IShaderResourceBinding>> pyramidSrbs;
    RefCntAutoPtr<Diligent::ITexture> ssrHiz;
    std::vector<RefCntAutoPtr<Diligent::ITextureView>> ssrHizMips;
    RefCntAutoPtr<Diligent::IPipelineState> ssrHizFirstPso, ssrHizPso;
    RefCntAutoPtr<Diligent::IShaderResourceBinding> ssrHizFirstSrb;
    std::vector<RefCntAutoPtr<Diligent::IShaderResourceBinding>> ssrHizSrbs;
    uint64_t pyramidVersion = 0;
    bool pyramidValid = false;
    RefCntAutoPtr<Diligent::ITexture> ssr;
    RefCntAutoPtr<Diligent::ITexture> bloomDown[kBloomLevels];
    RefCntAutoPtr<Diligent::ITexture> bloomUp[kBloomLevels];

    RefCntAutoPtr<Diligent::ITexture> transmittanceLut;
    RefCntAutoPtr<Diligent::ITexture> multiScatterLut;
    RefCntAutoPtr<Diligent::ITexture> skyViewLut;
    RefCntAutoPtr<Diligent::ITexture> aerialLut;
    RefCntAutoPtr<Diligent::ITexture> envCapture;
    RefCntAutoPtr<Diligent::ITextureView> envCaptureMips[kEnvMips + 2];
    RefCntAutoPtr<Diligent::ITexture> envPrefiltered;
    RefCntAutoPtr<Diligent::ITextureView> envPrefilteredMips[kEnvMips];
    RefCntAutoPtr<Diligent::ITexture> dfg;
    RefCntAutoPtr<Diligent::ITexture> hdri;
    RefCntAutoPtr<Diligent::ITexture> shadowMap;
    RefCntAutoPtr<Diligent::ITextureView> shadowTargets[kShadowLayers];
    RefCntAutoPtr<Diligent::ITexture> colorTextures;
    RefCntAutoPtr<Diligent::ITexture> dataTextures;
    int textureCount = 0;
    int shadowResolution = 0;
    RefCntAutoPtr<Diligent::ITexture> localShadowMap;
    RefCntAutoPtr<Diligent::ITextureView> localShadowTargets[kMaxLocalShadowViews];
    int localShadowResolution = 0;
    std::array<LocalShadowView, kMaxLocalShadowViews> localViews{};
    std::array<LocalShadowCaster, kMaxLocalShadowLights> localCasters{};
    int localCasterCount = 0;
    float localTanHalf = 1.0f;
    std::vector<GpuLight> cpuLights;
    std::vector<uint32_t> shadowedLights;

    RefCntAutoPtr<Diligent::IBuffer> materials;
    RefCntAutoPtr<Diligent::IBuffer> lights;
    RefCntAutoPtr<Diligent::IBuffer> clusters;
    RefCntAutoPtr<Diligent::IBuffer> exposure;
    RefCntAutoPtr<Diligent::IBuffer> histogram;
    RefCntAutoPtr<Diligent::IBuffer> irradiance;
    RefCntAutoPtr<Diligent::IBuffer> exposureConstants;
    RefCntAutoPtr<Diligent::IBuffer> prefilterConstants;
    RefCntAutoPtr<Diligent::IBuffer> bloomConstants;

    RefCntAutoPtr<Diligent::IPipelineState> transmittancePso, multiScatterPso, skyViewPso, aerialPso, envCapturePso, envDownsamplePso, envPrefilterPso, envShPso, dfgPso, lightCullPso, cloudsPso, gtaoDepthPso, gtaoPso, gtaoBlurPso, ssrPso, lightingPso, histogramPso, exposurePso, commitPso, bloomDownPso, bloomUpPso, tonemapPso, fxaaPso;
    RefCntAutoPtr<Diligent::IShaderResourceBinding> transmittanceSrb, multiScatterSrb, skyViewSrb, aerialSrb, envCaptureSrb, envShSrb, dfgSrb, lightCullSrb, cloudsSrb, gtaoDepthSrb, gtaoSrb, gtaoBlurSrb, exposureSrb, commitSrb, fxaaSrb;
    RefCntAutoPtr<Diligent::IShaderResourceBinding> ssrSrb[2], lightingSrb[2], histogramSrb[2], tonemapSrb[2], bloomSourceSrb[2];
    std::vector<RefCntAutoPtr<Diligent::IShaderResourceBinding>> envDownsampleSrbs, envPrefilterSrbs, bloomDownSrbs, bloomUpSrbs;

    uint32_t lightCount = 0;
    bool lutsDirty = true;
    bool dfgDirty = true;
    bool envDirty = true;
    uint64_t skyHash = 0;
    uint32_t frame = 0;
    glm::vec2 cloudOffset{0.0f};
    float cloudTime = 0.0f;
    float nightDarkening = 0.0f;
    bool hasDirectional = false;
    glm::vec3 directionalDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 directionalIlluminance{0.0f};
    glm::vec3 sun{0.0f, 1.0f, 0.0f};
    std::array<Cascade, 4> cascades{};
    glm::mat4 previousViewProjection{1.0f};
    bool hasPrevious = false;
    float lastEv = 12.0f;
    std::string hdriPath;
    int hdriLoaded = 0;
    uint64_t atmosphereHash = 0;
    uint64_t shadeVersion = 0;
    uint64_t finishVersion = 0;
    Diligent::ITexture* shadeDepth = nullptr;
    int colorLayers = 0;
    int dataLayers = 0;
};

Lighting::Lighting() = default;
Lighting::~Lighting() { shutdown(); }

RenderSettings& Lighting::settings() { return m_data->settings; }
uint64_t Lighting::bindingVersion() const { return m_data ? m_data->bindingVersion : 0; }
const std::array<Cascade, 4>& Lighting::cascades() const { return m_data->cascades; }
int Lighting::cascadeCount() const { return m_data->settings.shadows ? std::clamp(m_data->settings.shadowCascades, 1, 4) : 0; }
int Lighting::shadowResolution() const { return m_data->shadowResolution; }
const std::array<LocalShadowView, kMaxLocalShadowViews>& Lighting::localShadowViews() const { return m_data->localViews; }
const std::array<LocalShadowCaster, kMaxLocalShadowLights>& Lighting::localShadowCasters() const { return m_data->localCasters; }
int Lighting::localShadowCasterCount() const { return m_data->localCasterCount; }
int Lighting::localShadowResolution() const { return m_data->localShadowResolution; }
float Lighting::localShadowTanHalf() const { return m_data->localTanHalf; }
Diligent::ITexture* Lighting::localShadowMap() const { return m_data->localShadowMap; }
Diligent::ITextureView* Lighting::localShadowTarget(int view) const { return m_data->localShadowTargets[view]; }
Diligent::ITexture* Lighting::gbuffer(int index) const { return m_data->gbuffer[index]; }
Diligent::ITexture* Lighting::hdr() const { return m_data->hdrTargets[m_data->hdrIndex]; }
Diligent::ITexture* Lighting::hdrTarget(int index) const { return m_data->hdrTargets[index & 1]; }
int Lighting::hdrIndex() const { return m_data->hdrIndex; }
void Lighting::setHdrIndex(int index) { m_data->hdrIndex = index & 1; }
Diligent::ITexture* Lighting::shadowMap() const { return m_data->shadowMap; }
Diligent::ITextureView* Lighting::shadowTarget(int cascade) const { return m_data->shadowTargets[cascade]; }
Diligent::IBuffer* Lighting::materialBuffer() const { return m_data->materials; }
Diligent::IBuffer* Lighting::lightBuffer() const { return m_data->lights; }
Diligent::IBuffer* Lighting::clusterBuffer() const { return m_data->clusters; }
Diligent::IBuffer* Lighting::exposureBuffer() const { return m_data->exposure; }
Diligent::IBuffer* Lighting::irradianceBuffer() const { return m_data->irradiance; }
Diligent::ITextureView* Lighting::prefilteredEnvironment() const { return litgpu::srv(m_data->envPrefiltered); }
Diligent::ITextureView* Lighting::dfgLut() const { return litgpu::srv(m_data->dfg); }
Diligent::ITextureView* Lighting::colorTextures() const { return litgpu::srv(m_data->colorTextures); }
Diligent::ITextureView* Lighting::dataTextures() const { return litgpu::srv(m_data->dataTextures); }
float Lighting::currentEv100() const { return m_data ? m_data->lastEv : 0.0f; }
glm::vec3 Lighting::sunDirection() const { return m_data ? m_data->sun : glm::vec3(0.0f, 1.0f, 0.0f); }

namespace {

void createShadowArray(Diligent::IRenderDevice* device, const char* name, uint32_t resolution, uint32_t layers, RefCntAutoPtr<Diligent::ITexture>& texture, RefCntAutoPtr<Diligent::ITextureView>* targets) {
    Diligent::TextureDesc desc;
    desc.Name = name;
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
    desc.Width = desc.Height = resolution;
    desc.ArraySize = layers;
    desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
    texture.Release();
    device->CreateTexture(desc, nullptr, &texture);
    for (uint32_t layer = 0; layer < layers; ++layer) targets[layer] = litgpu::mipView(texture, Diligent::TEXTURE_VIEW_DEPTH_STENCIL, 0, Diligent::RESOURCE_DIM_TEX_2D_ARRAY, layer, 1);
}

bool sphereInFrustum(const glm::vec4* planes, const glm::vec3& center, float radius) {
    for (int i = 0; i < 6; ++i) {
        if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius) return false;
    }
    return true;
}

bool pyramidInFrustum(const glm::vec4* planes, const glm::vec3& apex, const glm::vec3& forward, const glm::vec3& right, const glm::vec3& up, float tanHalf, float range) {
    const glm::vec3 points[5] = {apex, apex + range * (forward + tanHalf * (right + up)), apex + range * (forward + tanHalf * (right - up)), apex + range * (forward - tanHalf * (right + up)), apex + range * (forward - tanHalf * (right - up))};
    for (int i = 0; i < 6; ++i) {
        bool outside = true;
        for (const glm::vec3& p : points) {
            if (glm::dot(glm::vec3(planes[i]), p) + planes[i].w >= 0.0f) {
                outside = false;
                break;
            }
        }
        if (outside) return false;
    }
    return true;
}

RefCntAutoPtr<Diligent::ITexture> placeholderArray(Diligent::IRenderDevice* device, const char* name, Diligent::TEXTURE_FORMAT format, uint32_t rgba) {
    Diligent::TextureDesc desc;
    desc.Name = name;
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
    desc.Width = 4;
    desc.Height = 4;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    std::vector<uint32_t> pixels(16, rgba);
    Diligent::TextureSubResData sub;
    sub.pData = pixels.data();
    sub.Stride = 16;
    Diligent::TextureData data;
    data.NumSubresources = 1;
    data.pSubResources = &sub;
    RefCntAutoPtr<Diligent::ITexture> tex;
    device->CreateTexture(desc, &data, &tex);
    return tex;
}

}  // namespace

bool Lighting::init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::TEXTURE_FORMAT backBufferFormat, int width, int height) {
    if (!m_data) m_data = new Data();
    Data& d = *m_data;
    d.device = device;
    d.context = context;
    d.backBufferFormat = backBufferFormat;

    std::vector<MaterialGpu> defaults(kMaxMaterials, packMaterial(Material{}));
    d.materials = litgpu::structuredBuffer(device, "Material Buffer", sizeof(MaterialGpu), kMaxMaterials, defaults.data());
    std::vector<GpuLight> noLights(kMaxLights, GpuLight{});
    d.lights = litgpu::structuredBuffer(device, "Light Buffer", sizeof(GpuLight), kMaxLights, noLights.data());
    std::vector<uint32_t> emptyClusters(static_cast<size_t>(kClusterCount) * kClusterStride, 0u);
    d.clusters = litgpu::structuredBuffer(device, "Cluster Buffer", sizeof(uint32_t), kClusterCount * kClusterStride, emptyClusters.data());
    const glm::vec4 exposureInit(1.0f / (1.2f * std::exp2(12.0f)), 1.0f / (1.2f * std::exp2(12.0f)), 12.0f, 0.0f);
    d.exposure = litgpu::structuredBuffer(device, "Exposure Buffer", sizeof(glm::vec4), 1, &exposureInit);
    std::vector<uint32_t> zeros(256, 0u);
    d.histogram = litgpu::structuredBuffer(device, "Histogram Buffer", sizeof(uint32_t), 256, zeros.data());
    std::vector<glm::vec4> sh(9, glm::vec4(0.0f));
    sh[0] = glm::vec4(0.5f);
    d.irradiance = litgpu::structuredBuffer(device, "Irradiance SH", sizeof(glm::vec4), 9, sh.data());
    d.exposureConstants = litgpu::uniformBuffer(device, "Exposure Constants", sizeof(ExposureConstants));
    d.prefilterConstants = litgpu::uniformBuffer(device, "Prefilter Constants", sizeof(glm::vec4));
    d.bloomConstants = litgpu::uniformBuffer(device, "Bloom Constants", sizeof(glm::vec4));

    using litgpu::TextureSpec;
    d.transmittanceLut = litgpu::texture(device, TextureSpec{"Transmittance LUT", Diligent::RESOURCE_DIM_TEX_2D, 256, 64});
    d.multiScatterLut = litgpu::texture(device, TextureSpec{"Multi Scattering LUT", Diligent::RESOURCE_DIM_TEX_2D, 32, 32});
    d.skyViewLut = litgpu::texture(device, TextureSpec{"Sky View LUT", Diligent::RESOURCE_DIM_TEX_2D, kSkyViewWidth, kSkyViewHeight});
    d.aerialLut = litgpu::texture(device, TextureSpec{"Aerial Perspective LUT", Diligent::RESOURCE_DIM_TEX_3D, 32, 32, 32});
    d.envCapture = litgpu::texture(device, TextureSpec{"Environment Capture", Diligent::RESOURCE_DIM_TEX_CUBE, kEnvSize, kEnvSize, 6, kEnvMips + 2});
    for (uint32_t mip = 0; mip < kEnvMips + 2; ++mip) d.envCaptureMips[mip] = litgpu::mipView(d.envCapture, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D_ARRAY, 0, 6);
    d.envPrefiltered = litgpu::texture(device, TextureSpec{"Prefiltered Environment", Diligent::RESOURCE_DIM_TEX_CUBE, kEnvSize, kEnvSize, 6, kEnvMips});
    for (uint32_t mip = 0; mip < kEnvMips; ++mip) d.envPrefilteredMips[mip] = litgpu::mipView(d.envPrefiltered, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D_ARRAY, 0, 6);
    d.dfg = litgpu::texture(device, TextureSpec{"DFG LUT", Diligent::RESOURCE_DIM_TEX_2D, 128, 128});
    d.cloudNoise = litgpu::texture(device, TextureSpec{"Cloud Noise", Diligent::RESOURCE_DIM_TEX_3D, 128, 128, 128, 8, Diligent::TEX_FORMAT_RGBA8_UNORM});
    d.godRayConstants = litgpu::uniformBuffer(device, "GodRayConstants", sizeof(glm::vec4));
    d.hdri = litgpu::texture(device, TextureSpec{"HDRI", Diligent::RESOURCE_DIM_TEX_2D, 4, 2, 1, 1, Diligent::TEX_FORMAT_RGBA16_FLOAT, Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS});
    d.colorTextures = placeholderArray(device, "Color Textures", Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB, 0xFFFFFFFFu);
    d.dataTextures = placeholderArray(device, "Data Textures", Diligent::TEX_FORMAT_RGBA8_UNORM, 0xFFFF8080u);
    createShadowArray(device, "Shadow Cascades", 16, kShadowLayers, d.shadowMap, d.shadowTargets);
    d.shadowResolution = 16;
    for (int c = 0; c < 4; ++c) d.cascades[c].layer = d.cascades[c].renderLayer = c;
    createShadowArray(device, "Local Shadows", 16, kMaxLocalShadowViews, d.localShadowMap, d.localShadowTargets);
    d.localShadowResolution = 16;
    resize(width, height);
    reloadShaders();
    return d.lightingPso != nullptr;
}

void Lighting::shutdown() {
    delete m_data;
    m_data = nullptr;
}

void Lighting::resize(int width, int height) {
    Data& d = *m_data;
    d.width = std::max(width, 1);
    d.height = std::max(height, 1);
    const auto w = static_cast<uint32_t>(d.width);
    const auto h = static_cast<uint32_t>(d.height);
    using litgpu::TextureSpec;
    const Diligent::BIND_FLAGS rt = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    d.gbuffer[0] = litgpu::texture(d.device, TextureSpec{"GBuffer Base Color", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, 1, Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB, rt});
    d.gbuffer[1] = litgpu::texture(d.device, TextureSpec{"GBuffer Normal Roughness", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, 1, Diligent::TEX_FORMAT_RGB10A2_UNORM, rt});
    d.gbuffer[2] = litgpu::texture(d.device, TextureSpec{"GBuffer Material", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, 1, Diligent::TEX_FORMAT_RGBA8_UINT, rt});
    d.hdrTargets[0] = litgpu::texture(d.device, TextureSpec{"HDR Color A", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, 1, kHdrFormat, rt | Diligent::BIND_UNORDERED_ACCESS});
    d.hdrTargets[1] = litgpu::texture(d.device, TextureSpec{"HDR Color B", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, 1, kHdrFormat, rt | Diligent::BIND_UNORDERED_ACCESS});
    d.ldr = litgpu::texture(d.device, TextureSpec{"LDR Color", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, 1, kLdrFormat, rt});
    d.aoRaw = litgpu::texture(d.device, TextureSpec{"GTAO Raw", Diligent::RESOURCE_DIM_TEX_2D, std::max(w / 2, 1u), std::max(h / 2, 1u), 1, 1, kHdrFormat});
    d.aoTemp = litgpu::texture(d.device, TextureSpec{"GTAO Blur", Diligent::RESOURCE_DIM_TEX_2D, std::max(w / 2, 1u), std::max(h / 2, 1u), 1, 1, kHdrFormat});
    d.clouds = litgpu::texture(d.device, TextureSpec{"Clouds", Diligent::RESOURCE_DIM_TEX_2D, std::max(w / 2, 1u), std::max(h / 2, 1u), 1, 1, Diligent::TEX_FORMAT_RGBA16_FLOAT});
    for (auto& rays : d.godRays) rays = litgpu::texture(d.device, TextureSpec{"God Rays", Diligent::RESOURCE_DIM_TEX_2D, std::max(w / 2, 1u), std::max(h / 2, 1u), 1, 1, Diligent::TEX_FORMAT_R16_FLOAT});
    d.aoDepth = litgpu::texture(d.device, TextureSpec{"GTAO Depth", Diligent::RESOURCE_DIM_TEX_2D, std::max(w / 2, 1u), std::max(h / 2, 1u), 1, 1, Diligent::TEX_FORMAT_R32_FLOAT});
    d.aoFiltered = litgpu::texture(d.device, TextureSpec{"GTAO", Diligent::RESOURCE_DIM_TEX_2D, std::max(w / 2, 1u), std::max(h / 2, 1u), 1, 1, kHdrFormat});
    d.sceneColor = litgpu::texture(d.device, TextureSpec{"Scene Color Pyramid", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, kPyramidMips, kHdrFormat});
    d.sceneColorMips.clear();
    for (uint32_t mip = 0; mip < kPyramidMips; ++mip) d.sceneColorMips.push_back(litgpu::mipView(d.sceneColor, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D));
    d.pyramidValid = false;
    {
        const uint32_t hw = w;
        const uint32_t hh = h;
        const uint32_t mips = std::min<uint32_t>(9, static_cast<uint32_t>(std::bit_width(std::max(hw, hh))));
        d.ssrHiz = litgpu::texture(d.device, TextureSpec{"SSR Hi-Z", Diligent::RESOURCE_DIM_TEX_2D, hw, hh, 1, mips, Diligent::TEX_FORMAT_R32_FLOAT});
        d.ssrHizMips.clear();
        for (uint32_t mip = 0; mip < mips; ++mip) d.ssrHizMips.push_back(litgpu::mipView(d.ssrHiz, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D));
    }
    d.ssr = litgpu::texture(d.device, TextureSpec{"SSR", Diligent::RESOURCE_DIM_TEX_2D, w, h, 1, 1, kHdrFormat});
    for (uint32_t i = 0; i < kBloomLevels; ++i) {
        const uint32_t bw = std::max(w >> (i + 1), 1u);
        const uint32_t bh = std::max(h >> (i + 1), 1u);
        d.bloomDown[i] = litgpu::texture(d.device, TextureSpec{"Bloom Down", Diligent::RESOURCE_DIM_TEX_2D, bw, bh, 1, 1, kHdrFormat});
        d.bloomUp[i] = litgpu::texture(d.device, TextureSpec{"Bloom Up", Diligent::RESOURCE_DIM_TEX_2D, bw, bh, 1, 1, kHdrFormat});
    }
    d.hasPrevious = false;
    ++d.bindingVersion;
    if (d.lightingPso) reloadShaders();
}

void Lighting::reloadShaders() {
    Data& d = *m_data;
    Diligent::IRenderDevice* device = d.device;
    const NamedSampler linear{"u_transmittanceLut", litgpu::linearClamp()};
    auto L = [](const char* name) { return NamedSampler{name, litgpu::linearClamp()}; };
    auto P = [](const char* name) { return NamedSampler{name, litgpu::pointClamp()}; };
    d.transmittancePso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_transmittance.comp");
    d.multiScatterPso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_multiscatter.comp", {linear});
    d.skyViewPso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_view.comp", {linear, L("u_multiScatterLut")});
    d.aerialPso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_aerial.comp", {linear, L("u_multiScatterLut")});
    d.envCapturePso = litgpu::computePipeline(device, "resources/shaders/lighting/env_capture.comp", {linear, L("u_skyViewLut"), {"u_hdri", litgpu::linearWrap()}, {"u_cloudNoise", litgpu::linearWrap()}});
    d.cloudNoisePso = litgpu::computePipeline(device, "resources/shaders/lighting/cloud_noise.comp");
    d.cloudNoiseDownPso = litgpu::computePipeline(device, "resources/shaders/lighting/cloud_noise.comp", {}, {{"DOWNSAMPLE", "1"}});
    d.cloudNoiseDirty = true;
    d.godRayMaskPso = litgpu::computePipeline(device, "resources/shaders/post/godrays.comp", {P("u_depth"), L("u_clouds")}, {{"MASK", "1"}});
    d.godRayBlurPso = litgpu::computePipeline(device, "resources/shaders/post/godrays.comp", {L("u_input")}, {{"BLUR", "1"}});
    d.godRayCompositePso = litgpu::computePipeline(device, "resources/shaders/post/godrays.comp", {L("u_shafts")});
    d.godRayVersion = 0;
    d.envDownsamplePso = litgpu::computePipeline(device, "resources/shaders/lighting/env_downsample.comp");
    d.envPrefilterPso = litgpu::computePipeline(device, "resources/shaders/lighting/env_prefilter.comp", {L("u_environment")});
    d.envShPso = litgpu::computePipeline(device, "resources/shaders/lighting/env_sh.comp", {L("u_environment")});
    d.dfgPso = litgpu::computePipeline(device, "resources/shaders/lighting/dfg_lut.comp");
    d.lightCullPso = litgpu::computePipeline(device, "resources/shaders/lighting/light_cull.comp");
    d.cloudsPso = litgpu::computePipeline(device, "resources/shaders/lighting/clouds.comp", {linear, L("u_skyViewLut"), {"u_hdri", litgpu::linearWrap()}, {"u_cloudNoise", litgpu::linearWrap()}});
    d.gtaoDepthPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao_depth.comp", {P("u_depth")});
    d.gtaoPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao.comp", {P("u_depth"), P("u_linearDepth"), P("u_gbuffer1"), L("u_sceneColor")});
    d.gtaoBlurPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao_blur.comp", {P("u_linearDepth"), P("u_input")});
    d.gtaoBlurVerticalPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao_blur.comp", {P("u_linearDepth"), P("u_input")}, {{"VERTICAL", "1"}});
    d.pyramidFirstPso = litgpu::computePipeline(device, "resources/shaders/lighting/color_pyramid.comp", {L("u_source")}, {{"FIRST_LEVEL", "1"}});
    d.pyramidPso = litgpu::computePipeline(device, "resources/shaders/lighting/color_pyramid.comp");
    d.ssrHizFirstPso = litgpu::computePipeline(device, "resources/shaders/lighting/ssr_hiz.comp", {P("u_depth")}, {{"FIRST_LEVEL", "1"}});
    d.ssrHizPso = litgpu::computePipeline(device, "resources/shaders/lighting/ssr_hiz.comp");
    d.ssrPso = litgpu::computePipeline(device, "resources/shaders/lighting/ssr.comp", {P("u_depth"), P("u_gbuffer1"), L("u_previousColor"), P("u_hiz")});
    d.lightingPso = litgpu::computePipeline(device, "resources/shaders/lighting/lighting.comp",
                                            {P("u_depth"), P("u_gbuffer0"), P("u_gbuffer1"), P("u_gbuffer2"), P("u_ao"), L("u_ssr"), L("u_aerial"), linear, L("u_skyViewLut"), {"u_hdri", litgpu::linearWrap()}, L("u_prefilteredEnv"), L("u_dfgLut"),
                                             {"u_shadowMap", litgpu::shadowCompare()}, P("u_shadowDepth"), {"u_localShadowMap", litgpu::shadowCompare()}, P("u_aoDepth"), L("u_clouds")});
    d.histogramPso = litgpu::computePipeline(device, "resources/shaders/post/histogram.comp", {P("u_hdr")});
    d.exposurePso = litgpu::computePipeline(device, "resources/shaders/post/exposure.comp");
    d.commitPso = litgpu::computePipeline(device, "resources/shaders/post/exposure_commit.comp");
    d.bloomDownPso = litgpu::computePipeline(device, "resources/shaders/post/bloom_down.comp", {L("u_source")});
    d.bloomUpPso = litgpu::computePipeline(device, "resources/shaders/post/bloom_up.comp", {L("u_lower"), L("u_current")});
    d.tonemapPso = litgpu::fullscreenPipeline(device, "Tonemap", "resources/shaders/post/tonemap.frag", kLdrFormat, {P("u_hdr"), L("u_bloom")});
    d.fxaaPso = litgpu::fullscreenPipeline(device, "FXAA", "resources/shaders/post/fxaa.frag", d.backBufferFormat, {L("u_ldr")});
    d.lutsDirty = true;
    d.dfgDirty = true;
    d.envDirty = true;
    ++d.bindingVersion;
}

void Lighting::setMaterial(uint32_t id, const Material& material) {
    if (!m_data || id >= kMaxMaterials) return;
    const MaterialGpu packed = packMaterial(material);
    m_data->context->UpdateBuffer(m_data->materials, static_cast<Diligent::Uint64>(id) * sizeof(MaterialGpu), sizeof(MaterialGpu), &packed, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Lighting::setLights(const std::vector<GpuLight>& lights) {
    Data& d = *m_data;
    d.lightCount = static_cast<uint32_t>(std::min<size_t>(lights.size(), kMaxLights));
    d.cpuLights.assign(lights.begin(), lights.begin() + d.lightCount);
}

void Lighting::setDirectionalLight(bool present, const glm::vec3& direction, const glm::vec3& illuminance) {
    Data& d = *m_data;
    d.hasDirectional = present;
    if (present) {
        d.directionalDirection = glm::normalize(direction);
        d.directionalIlluminance = illuminance;
    }
}

int Lighting::loadTexture(const std::string& path, bool srgb) {
    Data& d = *m_data;
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        Lit::Log::Warn("Could not load texture {}", path);
        return -1;
    }
    RefCntAutoPtr<Diligent::ITexture>& target = srgb ? d.colorTextures : d.dataTextures;
    if (target->GetDesc().Width != kTextureSize) {
        Diligent::TextureDesc desc;
        desc.Name = srgb ? "Color Textures" : "Data Textures";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
        desc.Width = desc.Height = kTextureSize;
        desc.ArraySize = kTextureCapacity;
        desc.MipLevels = kTextureMips;
        desc.Format = srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB : Diligent::TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;
        desc.MiscFlags = Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS;
        RefCntAutoPtr<Diligent::ITexture> array;
        d.device->CreateTexture(desc, nullptr, &array);
        if (!array) {
            stbi_image_free(pixels);
            return -1;
        }
        target = array;
        ++d.bindingVersion;
    }
    int& layers = srgb ? d.colorLayers : d.dataLayers;
    if (layers >= static_cast<int>(kTextureCapacity)) {
        stbi_image_free(pixels);
        Lit::Log::Warn("Texture array is full; {} not loaded", path);
        return -1;
    }
    std::vector<uint32_t> resized(static_cast<size_t>(kTextureSize) * kTextureSize);
    const auto* src = reinterpret_cast<const uint32_t*>(pixels);
    for (uint32_t y = 0; y < kTextureSize; ++y) {
        const int sy = std::min(static_cast<int>(static_cast<uint64_t>(y) * h / kTextureSize), h - 1);
        for (uint32_t x = 0; x < kTextureSize; ++x) {
            const int sx = std::min(static_cast<int>(static_cast<uint64_t>(x) * w / kTextureSize), w - 1);
            resized[static_cast<size_t>(y) * kTextureSize + x] = src[static_cast<size_t>(sy) * w + sx];
        }
    }
    stbi_image_free(pixels);
    Diligent::Box box(0, kTextureSize, 0, kTextureSize);
    Diligent::TextureSubResData sub;
    sub.pData = resized.data();
    sub.Stride = kTextureSize * 4;
    d.context->UpdateTexture(target, 0, static_cast<Diligent::Uint32>(layers), box, sub, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    d.context->GenerateMips(litgpu::srv(target));
    return layers++;
}

void Lighting::selectLocalShadows(SceneUniforms& u, const glm::vec3& cameraPosition) {
    Data& d = *m_data;
    const RenderSettings& s = d.settings;
    const int resolution = std::clamp(s.localShadowResolution, 128, 2048);
    const int maxLights = s.localShadows ? std::clamp(s.localShadowCount, 0, kMaxLocalShadowLights) : 0;
    if (maxLights > 0 && resolution != d.localShadowResolution) {
        createShadowArray(d.device, "Local Shadows", static_cast<uint32_t>(resolution), kMaxLocalShadowViews, d.localShadowMap, d.localShadowTargets);
        d.localShadowResolution = resolution;
        ++d.bindingVersion;
    }
    const float res = static_cast<float>(d.localShadowResolution);
    const float margin = res / std::max(res - 8.0f, 1.0f);
    d.localTanHalf = margin;
    struct Candidate {
        uint32_t index;
        float score;
    };
    std::vector<Candidate> candidates;
    for (uint32_t i = 0; i < d.cpuLights.size(); ++i) {
        GpuLight& light = d.cpuLights[i];
        light.params.w = -1.0f;
        if (maxLights == 0) continue;
        const glm::vec3 position(light.positionRange);
        const float range = light.positionRange.w;
        if (!sphereInFrustum(u.frustumPlanes, position, range)) continue;
        const glm::vec3 color(light.colorType);
        const float luminance = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        if (luminance <= 0.0f) continue;
        const float distance = std::max(glm::length(position - cameraPosition) - range * 0.25f, 0.5f);
        const bool previous = std::find(d.shadowedLights.begin(), d.shadowedLights.end(), i) != d.shadowedLights.end();
        candidates.push_back({i, luminance / (distance * distance) * (previous ? 1.5f : 1.0f)});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    d.shadowedLights.clear();
    d.localCasterCount = 0;
    for (LocalShadowView& view : d.localViews) view.active = false;
    int faces = 0;
    for (const Candidate& candidate : candidates) {
        if (d.localCasterCount >= maxLights) break;
        GpuLight& light = d.cpuLights[candidate.index];
        const bool spot = light.colorType.w > 1.5f;
        const int needed = spot ? 1 : 6;
        if (faces + needed > kMaxLocalShadowViews) continue;
        const glm::vec3 position(light.positionRange);
        const float range = light.positionRange.w;
        const float nearZ = std::clamp(range * 0.004f, 0.02f, 0.5f);
        LocalShadowCaster& caster = d.localCasters[static_cast<size_t>(d.localCasterCount)];
        caster.sphere = glm::vec4(position, range);
        caster.faceBase = static_cast<uint32_t>(faces);
        caster.spot = spot ? 1u : 0u;
        caster.faceMask = 0u;
        auto addView = [&](int face, const glm::vec3& forward, const glm::vec3& upHint, float tanHalf) {
            LocalShadowView& view = d.localViews[static_cast<size_t>(faces + face)];
            const glm::mat4 lightView = glm::lookAt(position, position + forward, upHint);
            const glm::mat4 projection = glm::perspectiveRH_ZO(2.0f * std::atan(tanHalf), 1.0f, nearZ, range);
            view.viewProjection = projection * lightView;
            view.origin = position;
            view.texelsAtUnitDistance = res / (2.0f * tanHalf);
            view.projectionScale = 1.0f / tanHalf;
            const glm::vec3 right = glm::normalize(glm::cross(forward, upHint));
            const glm::vec3 up = glm::cross(right, forward);
            view.active = pyramidInFrustum(u.frustumPlanes, position, forward, right, up, tanHalf, range);
            if (view.active) caster.faceMask |= 1u << face;
            u.localShadowMatrices[faces + face] = view.viewProjection;
        };
        if (spot) {
            const glm::vec3 direction = glm::normalize(glm::vec3(light.directionCos));
            const float cosOuter = std::clamp(light.directionCos.w, 0.0872f, 0.9999f);
            const float tanHalf = std::min(std::sqrt(1.0f - cosOuter * cosOuter) / cosOuter, 11.4f) * margin;
            const glm::vec3 upHint = std::abs(direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            addView(0, direction, upHint, tanHalf);
            const float cosWide = std::cos(std::atan(tanHalf * 1.4142f));
            caster.cone = glm::vec4(direction, cosWide);
        } else {
            const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
            for (int face = 0; face < 6; ++face) addView(face, axes[face], ups[face], margin);
            caster.cone = glm::vec4(0.0f, 0.0f, 0.0f, margin);
        }
        light.params.w = static_cast<float>(faces);
        faces += needed;
        ++d.localCasterCount;
        d.shadowedLights.push_back(candidate.index);
    }
    u.localShadowParams = glm::vec4(static_cast<float>(faces), 1.0f / res, s.localShadowSoftness, s.shadowNormalBias);
    u.localShadowParams2 = glm::vec4(s.shadowDepthBias, 0.0f, 0.0f, 0.0f);
    if (!d.cpuLights.empty()) d.context->UpdateBuffer(d.lights, 0, sizeof(GpuLight) * d.cpuLights.size(), d.cpuLights.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Lighting::beginFrame(const FrameInput& input, SceneUniforms& u) {
    Data& d = *m_data;
    RenderSettings& s = d.settings;
    ++d.frame;
    if (s.animateTime) {
        s.timeOfDay = std::fmod(s.timeOfDay + input.deltaTime * s.timeSpeed, 24.0f);
        if (s.timeOfDay < 0.0f) s.timeOfDay += 24.0f;
    }
    glm::vec3 toa;
    const glm::vec3 moon = sunFromTime(std::fmod(s.timeOfDay + 13.2f, 24.0f), s.sunAzimuth + 25.0f);
    bool moonLit = false;
    if (d.hasDirectional && s.sunFollowsLight) {
        d.sun = d.directionalDirection;
        toa = d.directionalIlluminance;
    } else {
        d.sun = sunFromTime(s.timeOfDay, s.sunAzimuth);
        toa = glm::vec3(s.sunIlluminance);
    }
    const glm::vec3 trueSun = d.sun;
    d.nightDarkening = s.sky == SkyMode::Physical ? 3.0f * std::clamp((-trueSun.y - 0.02f) / 0.2f, 0.0f, 1.0f) : 0.0f;
    if (s.moon && s.sky == SkyMode::Physical && !(d.hasDirectional && s.sunFollowsLight) && d.sun.y < -0.16f && moon.y > 0.0f) {
        moonLit = true;
        d.sun = moon;
        toa = glm::vec3(0.82f, 0.9f, 1.0f) * s.moonIlluminance * std::clamp((-trueSun.y - 0.16f) / 0.14f, 0.0f, 1.0f);
    }
    d.cloudTime += input.deltaTime;
    d.cloudOffset += glm::vec2(1.0f, 0.35f) * (s.cloudSpeed * 2.5f * input.deltaTime);
    d.cloudOffset = glm::vec2(std::fmod(d.cloudOffset.x, 1.26e6f), std::fmod(d.cloudOffset.y, 1.26e6f));
    const glm::vec3 transmittance = s.sky == SkyMode::Physical ? cpuTransmittance(d.sun.y, s) : glm::vec3(d.sun.y > -0.02f ? 1.0f : 0.0f);
    const float sunUp = glm::length(transmittance) > 1e-6f ? 1.0f : 0.0f;

    const glm::mat4 viewProjection = input.projection * input.view;
    u.projection = input.projection;
    u.view = input.view;
    u.viewProjection = viewProjection;
    u.invView = glm::inverse(input.view);
    u.invProjection = glm::inverse(input.projection);
    u.invViewProjection = glm::inverse(viewProjection);
    u.prevViewProjection = d.hasPrevious ? d.previousViewProjection : viewProjection;
    d.previousViewProjection = viewProjection;
    d.hasPrevious = true;
    extractPlanes(viewProjection, u.frustumPlanes);
    u.viewPos = input.cameraPosition;
    u.nearPlane = input.nearPlane;
    u.screenParams = glm::vec4(static_cast<float>(input.width), static_cast<float>(input.height), 1.0f / static_cast<float>(input.width), 1.0f / static_cast<float>(input.height));
    u.timeParams = glm::vec4(input.time, input.deltaTime, static_cast<float>(input.frame % 4096u), input.farPlane);
    u.sunDirection = glm::vec4(d.sun, glm::radians(s.sunAngularDiameter) * 0.5f);
    u.sunIlluminance = glm::vec4(toa * transmittance, sunUp);
    u.exposureParams = glm::vec4(s.skyBrightness, 0.0f, s.environmentIntensity, static_cast<float>(static_cast<int>(s.debugView)));
    u.lightParams = glm::vec4(static_cast<float>(d.lightCount), 16.0f, 9.0f, 24.0f);
    u.clusterParams = glm::vec4(24.0f / std::log(std::max(input.farPlane / input.nearPlane, 1.0001f)), 0.0f, static_cast<float>(input.width) / 16.0f, static_cast<float>(input.height) / 9.0f);

    const int count = cascadeCount();
    const int resolution = std::clamp(s.shadowResolution, 512, 4096);
    if (count > 0 && resolution != d.shadowResolution) {
        createShadowArray(d.device, "Shadow Cascades", static_cast<uint32_t>(resolution), kShadowLayers, d.shadowMap, d.shadowTargets);
        d.shadowResolution = resolution;
        for (Cascade& c : d.cascades) c.valid = false;
        ++d.bindingVersion;
    }
    const float nearZ = input.nearPlane;
    const float farZ = std::min(s.shadowDistance, input.farPlane);
    const glm::vec3 L = glm::normalize(d.sun);
    const float tanY = std::tan(glm::radians(input.fovY) * 0.5f);
    const float tanX = tanY * input.aspect;
    const bool orthographic = input.projection[3][3] > 0.5f;
    const glm::mat4 invView = u.invView;
    for (int c = 0; c < 4; ++c) {
        Cascade& cascade = d.cascades[c];
        const float p0 = static_cast<float>(c) / static_cast<float>(std::max(count, 1));
        const float p1 = static_cast<float>(c + 1) / static_cast<float>(std::max(count, 1));
        const float lambda = 0.82f;
        auto split = [&](float p) { return lambda * nearZ * std::pow(farZ / nearZ, p) + (1.0f - lambda) * (nearZ + (farZ - nearZ) * p); };
        const float z0 = c == 0 ? nearZ : split(p0);
        const float z1 = split(p1);
        cascade.split = z1;
        cascade.due = c < count;
        if (c >= count) {
            cascade.valid = false;
            continue;
        }
        const float zMid = 0.5f * (z0 + z1);
        glm::vec3 centerView(0.0f, 0.0f, -zMid);
        float radius = 0.0f;
        for (float z : {z0, z1}) {
            for (float sx : {-1.0f, 1.0f}) {
                for (float sy : {-1.0f, 1.0f}) {
                    const glm::vec3 corner(sx * (orthographic ? 1.0f / input.projection[0][0] : tanX * z), sy * (orthographic ? 1.0f / input.projection[1][1] : tanY * z), -z);
                    radius = std::max(radius, glm::length(corner - centerView));
                }
            }
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;
        glm::vec3 center = glm::vec3(invView * glm::vec4(centerView, 1.0f));
        const glm::vec3 up = std::abs(L.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const float texelWorld = 2.0f * radius / static_cast<float>(d.shadowResolution);
        glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -L, up);
        glm::vec3 lightSpace = glm::vec3(lightView * glm::vec4(center, 1.0f));
        lightSpace.x = std::floor(lightSpace.x / texelWorld) * texelWorld;
        lightSpace.y = std::floor(lightSpace.y / texelWorld) * texelWorld;
        lightSpace.z = std::floor(lightSpace.z / texelWorld) * texelWorld;
        center = glm::vec3(glm::inverse(lightView) * glm::vec4(lightSpace, 1.0f));
        const float casterExtension = 1200.0f;
        const glm::vec3 eye = center + L * (radius + casterExtension);
        lightView = glm::lookAt(eye, center, up);
        const float depthRange = 2.0f * radius + casterExtension;
        cascade.viewProjection = orthoZO(radius, 0.0f, depthRange) * lightView;
        cascade.renderViewProjection = cascade.viewProjection;
        extractPlanes(cascade.renderViewProjection, cascade.planes);
        cascade.texelsPerUnit = static_cast<float>(d.shadowResolution) / (2.0f * radius);
        cascade.texelWorld = texelWorld;
        cascade.depthRange = depthRange;
        cascade.layer = cascade.renderLayer = c;
        cascade.viewport[0] = 0;
        cascade.viewport[1] = 0;
        cascade.viewport[2] = d.shadowResolution;
        cascade.viewport[3] = d.shadowResolution;
        cascade.clear = true;
        cascade.valid = true;
    }
    for (int c = 0; c < 4; ++c) {
        u.cascadeViewProj[c] = d.cascades[c].viewProjection;
        u.cascadeInfo[c] = glm::vec4(d.cascades[c].texelWorld, d.cascades[c].depthRange, static_cast<float>(d.cascades[c].layer), 0.0f);
    }
    u.cascadeSplits = glm::vec4(d.cascades[0].split, d.cascades[1].split, d.cascades[2].split, d.cascades[3].split);
    selectLocalShadows(u, input.cameraPosition);
    u.shadowParams = glm::vec4(count > 0 ? 1.0f : 0.0f, 1.0f / static_cast<float>(std::max(d.shadowResolution, 1)), s.shadowSoftness, s.shadowNormalBias);
    u.shadowParams2 = glm::vec4(static_cast<float>(count), s.shadowDepthBias, 0.0f, s.pcss ? 1.0f : 0.0f);
    u.atmosphereParams = glm::vec4(s.rayleighScale, s.mieScale, s.ozoneScale, s.mieAnisotropy);
    const float skyIntensity = s.sky == SkyMode::Gradient ? s.gradientIntensity : s.hdriIntensity;
    u.skyParams = glm::vec4(static_cast<float>(static_cast<int>(s.sky == SkyMode::Hdri && d.hdriLoaded == 0 ? SkyMode::Gradient : s.sky)), skyIntensity, s.hdriRotation, s.clouds ? s.cloudCoverage : 0.0f);
    u.skyParams2 = glm::vec4(s.cloudSpeed, s.stars ? 1.0f : 0.0f, s.cloudDensity, s.groundAlbedo);
    u.skyGradient[0] = glm::vec4(s.gradientZenith, s.cloudAltitude);
    u.skyGradient[1] = glm::vec4(s.gradientHorizon, s.environmentIntensity);
    u.skyGradient[2] = glm::vec4(s.gradientGround, glm::length(toa) > 0.0f ? (toa.x + toa.y + toa.z) / 3.0f : 0.0f);
    u.renderFlags = glm::vec4(s.ssao ? 1.0f : 0.0f, s.ssr ? 1.0f : 0.0f, s.multiScatter ? 1.0f : 0.0f, s.specularAntiAliasing ? 1.0f : 0.0f);
    u.aoParams = glm::vec4(s.aoRadius, s.aoIntensity, static_cast<float>(std::clamp(s.aoSlices, 1, 8)), static_cast<float>(std::clamp(s.aoSteps, 2, 16)));
    u.ssrParams = glm::vec4(s.ssrMaxRoughness, s.ssrThickness, static_cast<float>(std::clamp(s.ssrSteps, 8, 128)), s.fxaa ? 1.0f : 0.0f);
    u.fogParams = glm::vec4(s.fog ? s.fogDensity : 0.0f, s.fogHeightFalloff, s.fogHeight, s.fog ? s.aerialDistanceScale : 0.0f);
    u.postParams = glm::vec4(s.bloom ? s.bloomIntensity : 0.0f, s.vignette, s.grain, static_cast<float>(static_cast<int>(s.tonemapper)));
    u.postParams2 = glm::vec4(s.contrast, s.saturation, s.temperature, s.tint);
    u.giParams = glm::vec4(s.ssao && s.ssgi ? s.ssgiIntensity : 0.0f, s.ssgiThickness, d.pyramidValid ? 1.0f : 0.0f, static_cast<float>(kPyramidMips));
    u.moonDirection = glm::vec4(moon, 0.0075f);
    u.celestialParams = glm::vec4(trueSun, moonLit ? 1.0f : 0.0f);
    u.cloudParams = glm::vec4(d.cloudOffset, std::fmod(d.cloudTime, 10000.0f), s.cloudSpeed);
    u.skyParams3 = glm::vec4(s.moonIlluminance, s.stars ? 1.0f : 0.0f, s.godRays ? s.godRayIntensity : 0.0f, s.moon && s.sky == SkyMode::Physical ? 1.0f : 0.0f);

    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        hash = (hash ^ bits) * 1099511628211ull;
    };
    for (float v : {d.sun.x, d.sun.y, d.sun.z, s.rayleighScale, s.mieScale, s.ozoneScale, s.mieAnisotropy, s.groundAlbedo, s.cloudCoverage, s.cloudDensity, s.cloudAltitude, static_cast<float>(s.clouds), static_cast<float>(static_cast<int>(s.sky)), s.hdriIntensity, s.hdriRotation, s.gradientIntensity,
                     s.gradientZenith.x, s.gradientHorizon.x, s.gradientGround.x, toa.x, std::round(input.cameraPosition.y / 50.0f), moon.x, moon.y, s.moonIlluminance, static_cast<float>(s.moon)}) {
        mix(v);
    }
    if (hash != d.skyHash) {
        d.skyHash = hash;
        d.envDirty = true;
    }
    if (s.clouds && s.cloudSpeed != 0.0f) d.envDirty = true;
}

void Lighting::prepare(Diligent::IBuffer* sceneUniforms) {
    Data& d = *m_data;
    auto* ctx = d.context;
    Diligent::IBuffer* scene = sceneUniforms;
    {
        if (d.settings.hdriPath != d.hdriPath) {
            d.hdriPath = d.settings.hdriPath;
            d.hdriLoaded = 0;
            int w = 0, h = 0, n = 0;
            float* pixels = d.hdriPath.empty() ? nullptr : stbi_loadf(d.hdriPath.c_str(), &w, &h, &n, 4);
            if (pixels) {
                std::vector<uint16_t> half(static_cast<size_t>(w) * h * 4);
                for (size_t i = 0; i < half.size(); ++i) {
                    const float f = std::min(pixels[i], 65000.0f);
                    uint32_t bits;
                    std::memcpy(&bits, &f, 4);
                    const uint32_t sign = (bits >> 16) & 0x8000u;
                    int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
                    const uint32_t mantissa = (bits >> 13) & 0x3FFu;
                    half[i] = exponent <= 0 ? static_cast<uint16_t>(sign) : exponent >= 31 ? static_cast<uint16_t>(sign | 0x7BFFu) : static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | mantissa);
                }
                stbi_image_free(pixels);
                Diligent::TextureDesc desc;
                desc.Name = "HDRI";
                desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
                desc.Width = static_cast<uint32_t>(w);
                desc.Height = static_cast<uint32_t>(h);
                desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
                desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
                Diligent::TextureSubResData sub;
                sub.pData = half.data();
                sub.Stride = static_cast<Diligent::Uint64>(w) * 8;
                Diligent::TextureData data;
                data.NumSubresources = 1;
                data.pSubResources = &sub;
                d.hdri.Release();
                d.device->CreateTexture(desc, &data, &d.hdri);
                d.hdriLoaded = d.hdri ? 1 : 0;
                ++d.bindingVersion;
                Lit::Log::Info("Loaded HDRI {} ({}x{})", d.hdriPath, w, h);
            } else if (!d.hdriPath.empty()) {
                Lit::Log::Warn("Could not load HDRI {}", d.hdriPath);
            }
            d.envDirty = true;
        }
    }

    if (d.dfgDirty && d.dfgPso) {
        d.dfgSrb = litgpu::makeBinding(d.dfgPso, {{"u_output", litgpu::uav(d.dfg)}});
        litgpu::dispatch(ctx, d.dfgPso, d.dfgSrb, 16, 16);
        d.dfgDirty = false;
    }
    if (!d.transmittanceSrb || d.lutsDirty) {
        d.transmittanceSrb = litgpu::makeBinding(d.transmittancePso, {{"SceneData", scene}, {"u_output", litgpu::uav(d.transmittanceLut)}});
        d.multiScatterSrb = litgpu::makeBinding(d.multiScatterPso, {{"SceneData", scene}, {"u_output", litgpu::uav(d.multiScatterLut)}, {"u_transmittanceLut", litgpu::srv(d.transmittanceLut)}});
        d.skyViewSrb = litgpu::makeBinding(d.skyViewPso, {{"SceneData", scene}, {"u_output", litgpu::uav(d.skyViewLut)}, {"u_transmittanceLut", litgpu::srv(d.transmittanceLut)}, {"u_multiScatterLut", litgpu::srv(d.multiScatterLut)}});
        d.aerialSrb = litgpu::makeBinding(d.aerialPso, {{"SceneData", scene}, {"u_output", litgpu::uav(d.aerialLut)}, {"u_transmittanceLut", litgpu::srv(d.transmittanceLut)}, {"u_multiScatterLut", litgpu::srv(d.multiScatterLut)}});
    }
    uint64_t& atmosphereHash = d.atmosphereHash;
    const RenderSettings& s = d.settings;
    uint64_t ah = 0;
    for (float v : {s.rayleighScale, s.mieScale, s.ozoneScale, s.groundAlbedo, s.mieAnisotropy}) ah = ah * 1315423911ull + static_cast<uint64_t>(v * 100000.0f);
    if (ah != atmosphereHash || d.lutsDirty) {
        atmosphereHash = ah;
        litgpu::dispatch(ctx, d.transmittancePso, d.transmittanceSrb, 32, 8);
        litgpu::dispatch(ctx, d.multiScatterPso, d.multiScatterSrb, 32, 32);
        d.envDirty = true;
    }
    d.lutsDirty = false;
    litgpu::dispatch(ctx, d.skyViewPso, d.skyViewSrb, (kSkyViewWidth + 7) / 8, (kSkyViewHeight + 7) / 8);
    litgpu::dispatch(ctx, d.aerialPso, d.aerialSrb, 8, 8, 8);

    if (d.cloudNoiseDirty && d.cloudNoisePso && d.cloudNoiseDownPso) {
        std::vector<RefCntAutoPtr<Diligent::ITextureView>> mips;
        for (uint32_t mip = 0; mip < 8; ++mip) mips.push_back(litgpu::mipView(d.cloudNoise, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_3D));
        auto srb = litgpu::makeBinding(d.cloudNoisePso, {{"u_output", mips[0]}});
        litgpu::dispatch(ctx, d.cloudNoisePso, srb, 32, 32, 32);
        for (uint32_t mip = 1; mip < 8; ++mip) {
            litgpu::uavBarrier(ctx, d.cloudNoise);
            const uint32_t groups = std::max((128u >> mip) / 4u, 1u);
            auto down = litgpu::makeBinding(d.cloudNoiseDownPso, {{"u_source", mips[mip - 1]}, {"u_output", mips[mip]}});
            litgpu::dispatch(ctx, d.cloudNoiseDownPso, down, groups, groups, groups);
        }
        d.cloudNoiseDirty = false;
    }
    if (d.envDirty) {
        d.envDirty = false;
        if (!d.envCaptureSrb || d.envDownsampleSrbs.empty()) {
            d.envCaptureSrb = litgpu::makeBinding(d.envCapturePso, {{"SceneData", scene}, {"u_output", d.envCaptureMips[0]}, {"u_transmittanceLut", litgpu::srv(d.transmittanceLut)}, {"u_skyViewLut", litgpu::srv(d.skyViewLut)}, {"u_hdri", litgpu::srv(d.hdri)}, {"u_cloudNoise", litgpu::srv(d.cloudNoise)}});
            d.envDownsampleSrbs.clear();
            for (uint32_t mip = 1; mip < kEnvMips + 2; ++mip) d.envDownsampleSrbs.push_back(litgpu::makeBinding(d.envDownsamplePso, {{"u_source", d.envCaptureMips[mip - 1]}, {"u_dest", d.envCaptureMips[mip]}}));
            d.envPrefilterSrbs.clear();
            for (uint32_t mip = 0; mip < kEnvMips; ++mip) d.envPrefilterSrbs.push_back(litgpu::makeBinding(d.envPrefilterPso, {{"PrefilterConstants", d.prefilterConstants}, {"u_output", d.envPrefilteredMips[mip]}, {"u_environment", litgpu::srv(d.envCapture)}}));
            d.envShSrb = litgpu::makeBinding(d.envShPso, {{"u_environment", litgpu::srv(d.envCapture)}, {"IrradianceBuffer", d.irradiance}});
        }
        litgpu::dispatch(ctx, d.envCapturePso, d.envCaptureSrb, kEnvSize / 8, kEnvSize / 8, 6);
        for (uint32_t mip = 1; mip < kEnvMips + 2; ++mip) {
            litgpu::uavBarrier(ctx, d.envCapture);
            const uint32_t size = std::max(kEnvSize >> mip, 1u);
            litgpu::dispatch(ctx, d.envDownsamplePso, d.envDownsampleSrbs[mip - 1], (size + 7) / 8, (size + 7) / 8, 6);
        }
        for (uint32_t mip = 0; mip < kEnvMips; ++mip) {
            const glm::vec4 params(static_cast<float>(mip) / static_cast<float>(kEnvMips - 1), static_cast<float>(kEnvSize), static_cast<float>(kEnvMips + 2), 0.0f);
            ctx->UpdateBuffer(d.prefilterConstants, 0, sizeof(params), &params, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const uint32_t size = std::max(kEnvSize >> mip, 1u);
            litgpu::dispatch(ctx, d.envPrefilterPso, d.envPrefilterSrbs[mip], (size + 7) / 8, (size + 7) / 8, 6);
        }
        litgpu::dispatch(ctx, d.envShPso, d.envShSrb, 1);
    }

    cullLights(sceneUniforms);
}

void Lighting::cullLights(Diligent::IBuffer* sceneUniforms) {
    Data& d = *m_data;
    if (!d.lightCullSrb) d.lightCullSrb = litgpu::makeBinding(d.lightCullPso, {{"SceneData", sceneUniforms}, {"LightBuffer", d.lights}, {"ClusterBuffer", d.clusters}});
    litgpu::dispatch(d.context, d.lightCullPso, d.lightCullSrb, (kClusterCount + 63) / 64);
}

void Lighting::shade(Diligent::ITexture* depth, Diligent::IBuffer* sceneUniforms, int stage) {
    Data& d = *m_data;
    auto* ctx = d.context;
    Diligent::IBuffer* scene = sceneUniforms;
    if (d.shadeVersion != d.bindingVersion || d.shadeDepth != depth || !d.lightingSrb[0]) {
        d.shadeVersion = d.bindingVersion;
        d.shadeDepth = depth;
        Diligent::ITextureView* depthView = litgpu::srv(depth);
        d.cloudsSrb = litgpu::makeBinding(d.cloudsPso, {{"SceneData", scene}, {"u_output", litgpu::uav(d.clouds)}, {"u_transmittanceLut", litgpu::srv(d.transmittanceLut)}, {"u_skyViewLut", litgpu::srv(d.skyViewLut)}, {"u_hdri", litgpu::srv(d.hdri)}, {"u_cloudNoise", litgpu::srv(d.cloudNoise)}});
        d.gtaoDepthSrb = litgpu::makeBinding(d.gtaoDepthPso, {{"SceneData", scene}, {"u_depth", depthView}, {"u_output", litgpu::uav(d.aoDepth)}});
        d.gtaoSrb = litgpu::makeBinding(d.gtaoPso, {{"SceneData", scene}, {"u_depth", depthView}, {"u_linearDepth", litgpu::srv(d.aoDepth)}, {"u_gbuffer1", litgpu::srv(d.gbuffer[1])}, {"u_sceneColor", litgpu::srv(d.sceneColor)}, {"ExposureBuffer", d.exposure}, {"u_output", litgpu::uav(d.aoRaw)}});
        d.gtaoBlurSrb = litgpu::makeBinding(d.gtaoBlurPso, {{"SceneData", scene}, {"u_linearDepth", litgpu::srv(d.aoDepth)}, {"u_input", litgpu::srv(d.aoRaw)}, {"u_output", litgpu::uav(d.aoTemp)}});
        d.gtaoBlurVerticalSrb = litgpu::makeBinding(d.gtaoBlurVerticalPso, {{"SceneData", scene}, {"u_linearDepth", litgpu::srv(d.aoDepth)}, {"u_input", litgpu::srv(d.aoTemp)}, {"u_output", litgpu::uav(d.aoFiltered)}});
        d.ssrHizFirstSrb = litgpu::makeBinding(d.ssrHizFirstPso, {{"u_depth", depthView}, {"u_dest", d.ssrHizMips[0]}});
        d.ssrHizSrbs.clear();
        for (size_t mip = 1; mip < d.ssrHizMips.size(); ++mip) d.ssrHizSrbs.push_back(litgpu::makeBinding(d.ssrHizPso, {{"u_sourceMip", d.ssrHizMips[mip - 1]}, {"u_dest", d.ssrHizMips[mip]}}));
        for (int parity = 0; parity < 2; ++parity) {
        d.ssrSrb[parity] = litgpu::makeBinding(d.ssrPso, {{"SceneData", scene}, {"u_depth", depthView}, {"u_gbuffer1", litgpu::srv(d.gbuffer[1])}, {"u_previousColor", litgpu::srv(d.sceneColor)}, {"u_hiz", litgpu::srv(d.ssrHiz)}, {"u_output", litgpu::uav(d.ssr)}, {"ExposureBuffer", d.exposure}});
        d.lightingSrb[parity] = litgpu::makeBinding(d.lightingPso, {{"SceneData", scene},
                                                            {"u_depth", depthView},
                                                            {"u_gbuffer0", litgpu::srv(d.gbuffer[0])},
                                                            {"u_gbuffer1", litgpu::srv(d.gbuffer[1])},
                                                            {"u_gbuffer2", litgpu::srv(d.gbuffer[2])},
                                                            {"u_ao", litgpu::srv(d.aoFiltered)},
                                                            {"u_aoDepth", litgpu::srv(d.aoDepth)},
                                                            {"u_clouds", litgpu::srv(d.clouds)},
                                                            {"u_ssr", litgpu::srv(d.ssr)},
                                                            {"u_aerial", litgpu::srv(d.aerialLut)},
                                                            {"u_transmittanceLut", litgpu::srv(d.transmittanceLut)},
                                                            {"u_skyViewLut", litgpu::srv(d.skyViewLut)},
                                                            {"u_hdri", litgpu::srv(d.hdri)},
                                                            {"u_prefilteredEnv", litgpu::srv(d.envPrefiltered)},
                                                            {"u_dfgLut", litgpu::srv(d.dfg)},
                                                            {"u_shadowMap", litgpu::srv(d.shadowMap)},
                                                            {"u_shadowDepth", litgpu::srv(d.shadowMap)},
                                                            {"u_localShadowMap", litgpu::srv(d.localShadowMap)},
                                                            {"u_hdr", litgpu::uav(d.hdrTargets[parity])},
                                                            {"MaterialBuffer", d.materials},
                                                            {"LightBuffer", d.lights},
                                                            {"ClusterBuffer", d.clusters},
                                                            {"IrradianceBuffer", d.irradiance},
                                                            {"ExposureBuffer", d.exposure}});
        }
    }
    const int parity = d.hdrIndex;
    const uint32_t gx = (static_cast<uint32_t>(d.width) + 7) / 8;
    const uint32_t gy = (static_cast<uint32_t>(d.height) + 7) / 8;
    if (d.settings.ssao && (stage < 0 || stage == 0)) {
        litgpu::dispatch(ctx, d.gtaoDepthPso, d.gtaoDepthSrb, (static_cast<uint32_t>(d.width) / 2 + 7) / 8, (static_cast<uint32_t>(d.height) / 2 + 7) / 8);
        litgpu::dispatch(ctx, d.gtaoPso, d.gtaoSrb, (static_cast<uint32_t>(d.width) / 2 + 7) / 8, (static_cast<uint32_t>(d.height) / 2 + 7) / 8);
        litgpu::dispatch(ctx, d.gtaoBlurPso, d.gtaoBlurSrb, (static_cast<uint32_t>(d.width) / 2 + 7) / 8, (static_cast<uint32_t>(d.height) / 2 + 7) / 8);
        litgpu::dispatch(ctx, d.gtaoBlurVerticalPso, d.gtaoBlurVerticalSrb, (static_cast<uint32_t>(d.width) / 2 + 7) / 8, (static_cast<uint32_t>(d.height) / 2 + 7) / 8);
    }
    if (d.settings.ssr && (stage < 0 || stage == 1)) {
        const auto& hizDesc = d.ssrHiz->GetDesc();
        litgpu::dispatch(ctx, d.ssrHizFirstPso, d.ssrHizFirstSrb, (hizDesc.Width + 7) / 8, (hizDesc.Height + 7) / 8);
        for (size_t mip = 1; mip < d.ssrHizMips.size(); ++mip) {
            litgpu::uavBarrier(ctx, d.ssrHiz);
            const uint32_t mw = std::max(hizDesc.Width >> mip, 1u);
            const uint32_t mh = std::max(hizDesc.Height >> mip, 1u);
            litgpu::dispatch(ctx, d.ssrHizPso, d.ssrHizSrbs[mip - 1], (mw + 7) / 8, (mh + 7) / 8);
        }
        litgpu::dispatch(ctx, d.ssrPso, d.ssrSrb[parity], gx, gy);
    }
    if (stage < 0 || stage == 2) {
        const auto& cloudDesc = d.clouds->GetDesc();
        litgpu::dispatch(ctx, d.cloudsPso, d.cloudsSrb, (cloudDesc.Width + 7) / 8, (cloudDesc.Height + 7) / 8);
    }
    if (stage < 0 || stage == 2) litgpu::dispatch(ctx, d.lightingPso, d.lightingSrb[parity], gx, gy);
}

void Lighting::captureSceneColor() {
    Data& d = *m_data;
    if (!d.pyramidFirstPso || !d.pyramidPso || d.sceneColorMips.size() < kPyramidMips) return;
    if (d.pyramidVersion != d.bindingVersion || !d.pyramidFirstSrb[0]) {
        d.pyramidVersion = d.bindingVersion;
        for (int parity = 0; parity < 2; ++parity) d.pyramidFirstSrb[parity] = litgpu::makeBinding(d.pyramidFirstPso, {{"u_source", litgpu::srv(d.hdrTargets[parity])}, {"u_dest", d.sceneColorMips[0]}});
        d.pyramidSrbs.clear();
        for (uint32_t mip = 1; mip < kPyramidMips; ++mip) d.pyramidSrbs.push_back(litgpu::makeBinding(d.pyramidPso, {{"u_sourceMip", d.sceneColorMips[mip - 1]}, {"u_dest", d.sceneColorMips[mip]}}));
    }
    const auto& desc = d.sceneColor->GetDesc();
    litgpu::dispatch(d.context, d.pyramidFirstPso, d.pyramidFirstSrb[d.hdrIndex], (desc.Width + 7) / 8, (desc.Height + 7) / 8);
    for (uint32_t mip = 1; mip < kPyramidMips; ++mip) {
        litgpu::uavBarrier(d.context, d.sceneColor);
        const uint32_t mw = std::max(desc.Width >> mip, 1u);
        const uint32_t mh = std::max(desc.Height >> mip, 1u);
        litgpu::dispatch(d.context, d.pyramidPso, d.pyramidSrbs[mip - 1], (mw + 7) / 8, (mh + 7) / 8);
    }
    d.pyramidValid = true;
}

Diligent::ITextureView* Lighting::sceneColor() const { return litgpu::srv(m_data->sceneColor); }

void Lighting::finish(Diligent::IBuffer* sceneUniforms, Diligent::ITextureView* backBuffer, float deltaTime, const std::function<void(Diligent::ITexture*)>& afterTonemap) {
    Data& d = *m_data;
    auto* ctx = d.context;
    Diligent::IBuffer* scene = sceneUniforms;
    const RenderSettings& s = d.settings;
    if (d.finishVersion != d.bindingVersion || !d.tonemapSrb[0]) {
        d.finishVersion = d.bindingVersion;
        for (int parity = 0; parity < 2; ++parity) {
            d.histogramSrb[parity] = litgpu::makeBinding(d.histogramPso, {{"u_hdr", litgpu::srv(d.hdrTargets[parity])}, {"ExposureBuffer", d.exposure}, {"HistogramBuffer", d.histogram}, {"ExposureConstants", d.exposureConstants}});
            d.bloomSourceSrb[parity] = litgpu::makeBinding(d.bloomDownPso, {{"u_source", litgpu::srv(d.hdrTargets[parity])}, {"u_output", litgpu::uav(d.bloomDown[0])}, {"BloomConstants", d.bloomConstants}});
            d.tonemapSrb[parity] = litgpu::makeBinding(d.tonemapPso, {{"SceneData", scene}, {"u_hdr", litgpu::srv(d.hdrTargets[parity])}, {"u_bloom", litgpu::srv(d.bloomUp[0])}, {"ExposureBuffer", d.exposure}});
        }
        d.exposureSrb = litgpu::makeBinding(d.exposurePso, {{"ExposureBuffer", d.exposure}, {"HistogramBuffer", d.histogram}, {"ExposureConstants", d.exposureConstants}});
        d.commitSrb = litgpu::makeBinding(d.commitPso, {{"ExposureBuffer", d.exposure}});
        d.bloomDownSrbs.clear();
        d.bloomUpSrbs.clear();
        for (uint32_t i = 0; i < kBloomLevels; ++i) d.bloomDownSrbs.push_back(i == 0 ? RefCntAutoPtr<Diligent::IShaderResourceBinding>() : litgpu::makeBinding(d.bloomDownPso, {{"u_source", litgpu::srv(d.bloomDown[i - 1])}, {"u_output", litgpu::uav(d.bloomDown[i])}, {"BloomConstants", d.bloomConstants}}));
        for (uint32_t i = 0; i + 1 < kBloomLevels; ++i) {
            Diligent::ITexture* lower = i + 2 == kBloomLevels ? d.bloomDown[i + 1].RawPtr() : d.bloomUp[i + 1].RawPtr();
            d.bloomUpSrbs.push_back(litgpu::makeBinding(d.bloomUpPso, {{"u_lower", litgpu::srv(lower)}, {"u_current", litgpu::srv(d.bloomDown[i])}, {"u_output", litgpu::uav(d.bloomUp[i])}, {"BloomConstants", d.bloomConstants}}));
        }
        d.fxaaSrb = litgpu::makeBinding(d.fxaaPso, {{"SceneData", scene}, {"u_ldr", litgpu::srv(d.ldr)}});
    }

    const int parity = d.hdrIndex;

    if (s.godRays && s.godRayIntensity > 0.0f && s.sky == SkyMode::Physical && d.shadeDepth && d.godRayMaskPso && d.godRayBlurPso && d.godRayCompositePso) {
        if (d.godRayVersion != d.bindingVersion || d.godRayDepth != d.shadeDepth || !d.godRayMaskSrb) {
            d.godRayVersion = d.bindingVersion;
            d.godRayDepth = d.shadeDepth;
            d.godRayMaskSrb = litgpu::makeBinding(d.godRayMaskPso, {{"SceneData", scene}, {"GodRayConstants", d.godRayConstants}, {"u_depth", litgpu::srv(d.shadeDepth)}, {"u_clouds", litgpu::srv(d.clouds)}, {"u_output", litgpu::uav(d.godRays[0])}});
            for (int i = 0; i < 2; ++i) {
                d.godRayBlurSrb[i] = litgpu::makeBinding(d.godRayBlurPso, {{"SceneData", scene}, {"GodRayConstants", d.godRayConstants}, {"u_input", litgpu::srv(d.godRays[i])}, {"u_output", litgpu::uav(d.godRays[1 - i])}});
                d.godRayCompositeSrb[i] = litgpu::makeBinding(d.godRayCompositePso, {{"SceneData", scene}, {"GodRayConstants", d.godRayConstants}, {"u_shafts", litgpu::srv(d.godRays[1])}, {"u_hdr", litgpu::uav(d.hdrTargets[i])}, {"ExposureBuffer", d.exposure}});
            }
        }
        const auto& rayDesc = d.godRays[0]->GetDesc();
        const uint32_t rx = (rayDesc.Width + 7) / 8;
        const uint32_t ry = (rayDesc.Height + 7) / 8;
        litgpu::dispatch(ctx, d.godRayMaskPso, d.godRayMaskSrb, rx, ry);
        const glm::vec4 passes[3] = {glm::vec4(1.0f, 0.04f, 0.0f, 0.0f), glm::vec4(0.1f, 0.0f, 0.0f, 0.0f), glm::vec4(0.01f, 0.0f, 0.0f, 0.0f)};
        int source = 0;
        for (const glm::vec4& pass : passes) {
            ctx->UpdateBuffer(d.godRayConstants, 0, sizeof(pass), &pass, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            litgpu::dispatch(ctx, d.godRayBlurPso, d.godRayBlurSrb[source], rx, ry);
            source = 1 - source;
        }
        litgpu::dispatch(ctx, d.godRayCompositePso, d.godRayCompositeSrb[parity], (static_cast<uint32_t>(d.width) + 7) / 8, (static_cast<uint32_t>(d.height) + 7) / 8);
    }

    const float minLog = -10.0f;
    const float maxLog = 22.0f;
    const ExposureConstants constants{glm::vec4(minLog, 1.0f / (maxLog - minLog), s.minEV100, s.maxEV100), glm::vec4(s.autoExposure ? 1.0f : 0.0f, s.manualEV100, std::clamp(deltaTime, 0.0f, 0.25f), s.exposureCompensation - d.nightDarkening),
                                      glm::vec4(s.adaptationUp, s.adaptationDown, 0.5f, 0.98f)};
    ctx->UpdateBuffer(d.exposureConstants, 0, sizeof(constants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    litgpu::dispatch(ctx, d.histogramPso, d.histogramSrb[parity], (static_cast<uint32_t>(d.width) / 4 + 15) / 16, (static_cast<uint32_t>(d.height) / 4 + 15) / 16);
    litgpu::dispatch(ctx, d.exposurePso, d.exposureSrb, 1);

    if (s.bloom) {
        for (uint32_t i = 0; i < kBloomLevels; ++i) {
            const glm::vec4 params(i == 0 ? 1.0f : 0.0f, s.bloomRadius, 0.0f, 0.0f);
            ctx->UpdateBuffer(d.bloomConstants, 0, sizeof(params), &params, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const auto& desc = d.bloomDown[i]->GetDesc();
            litgpu::dispatch(ctx, d.bloomDownPso, i == 0 ? d.bloomSourceSrb[parity].RawPtr() : d.bloomDownSrbs[i].RawPtr(), (desc.Width + 7) / 8, (desc.Height + 7) / 8);
        }
        const glm::vec4 params(0.0f, s.bloomRadius, 0.0f, 0.0f);
        ctx->UpdateBuffer(d.bloomConstants, 0, sizeof(params), &params, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        for (int i = static_cast<int>(kBloomLevels) - 2; i >= 0; --i) {
            const auto& desc = d.bloomUp[i]->GetDesc();
            litgpu::dispatch(ctx, d.bloomUpPso, d.bloomUpSrbs[static_cast<size_t>(i)], (desc.Width + 7) / 8, (desc.Height + 7) / 8);
        }
    }

    Diligent::ITextureView* ldrTarget = litgpu::rtv(d.ldr);
    ctx->SetRenderTargets(1, &ldrTarget, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(d.width);
    viewport.Height = static_cast<float>(d.height);
    ctx->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(d.width), static_cast<Diligent::Uint32>(d.height));
    if (d.tonemapPso && d.tonemapSrb[parity]) {
        ctx->SetPipelineState(d.tonemapPso);
        ctx->CommitShaderResources(d.tonemapSrb[parity], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ctx->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_NONE});
    }
    litgpu::dispatch(ctx, d.commitPso, d.commitSrb, 1);
    if (afterTonemap) afterTonemap(d.ldr);
    ctx->SetRenderTargets(1, &backBuffer, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ctx->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(d.width), static_cast<Diligent::Uint32>(d.height));
    if (d.fxaaPso && d.fxaaSrb) {
        ctx->SetPipelineState(d.fxaaPso);
        ctx->CommitShaderResources(d.fxaaSrb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ctx->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_NONE});
    }
    d.hdrIndex = 1 - d.hdrIndex;
}
