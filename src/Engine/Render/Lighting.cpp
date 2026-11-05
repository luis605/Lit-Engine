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

#include "LightingInternal.hpp"

using namespace lighting_detail;

namespace {

constexpr uint32_t kMaxHizMips = 9;
constexpr uint32_t kHistogramBins = 256;
constexpr uint32_t kPlaceholderSize = 4;
constexpr uint32_t kInitialShadowResolution = 16;
constexpr uint32_t kShCoefficients = 9;
constexpr float kMaxHalfFloat = 65000.0f;
constexpr float kInitialExposureEv = 12.0f;

RefCntAutoPtr<Diligent::ITexture> placeholderArray(Diligent::IRenderDevice* device, const char* name, Diligent::TEXTURE_FORMAT format, uint32_t rgba) {
    Diligent::TextureDesc desc;
    desc.Name = name;
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D_ARRAY;
    desc.Width = kPlaceholderSize;
    desc.Height = kPlaceholderSize;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    std::vector<uint32_t> pixels(kPlaceholderSize * kPlaceholderSize, rgba);
    Diligent::TextureSubResData subresource;
    subresource.pData = pixels.data();
    subresource.Stride = kPlaceholderSize * sizeof(uint32_t);
    Diligent::TextureData data;
    data.NumSubresources = 1;
    data.pSubResources = &subresource;
    RefCntAutoPtr<Diligent::ITexture> texture;
    device->CreateTexture(desc, &data, &texture);
    return texture;
}

uint16_t floatToHalf(float value) {
    const float clamped = std::min(value, kMaxHalfFloat);
    uint32_t bits;
    std::memcpy(&bits, &clamped, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mantissa = (bits >> 13) & 0x3FFu;
    if (exponent <= 0) return static_cast<uint16_t>(sign);
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | mantissa);
}

void createStaticBuffers(Lighting::Data& state) {
    Diligent::IRenderDevice* device = state.device;
    const std::vector<MaterialGpu> defaultMaterials(kMaxMaterials, packMaterial(Material{}));
    state.materials = litgpu::structuredBuffer(device, "Material Buffer", sizeof(MaterialGpu), kMaxMaterials, defaultMaterials.data());
    const std::vector<GpuLight> noLights(kMaxLights, GpuLight{});
    state.lights = litgpu::structuredBuffer(device, "Light Buffer", sizeof(GpuLight), kMaxLights, noLights.data());
    const std::vector<uint32_t> emptyClusters(static_cast<size_t>(kClusterCount) * kClusterStride, 0u);
    state.clusters = litgpu::structuredBuffer(device, "Cluster Buffer", sizeof(uint32_t), kClusterCount * kClusterStride, emptyClusters.data());
    const float initialExposure = 1.0f / (1.2f * std::exp2(kInitialExposureEv));
    const glm::vec4 exposureInit(initialExposure, initialExposure, kInitialExposureEv, 0.0f);
    state.exposure = litgpu::structuredBuffer(device, "Exposure Buffer", sizeof(glm::vec4), 1, &exposureInit);
    const std::vector<uint32_t> emptyHistogram(kHistogramBins, 0u);
    state.histogram = litgpu::structuredBuffer(device, "Histogram Buffer", sizeof(uint32_t), kHistogramBins, emptyHistogram.data());
    std::vector<glm::vec4> irradianceCoefficients(kShCoefficients, glm::vec4(0.0f));
    irradianceCoefficients[0] = glm::vec4(0.5f);
    state.irradiance = litgpu::structuredBuffer(device, "Irradiance SH", sizeof(glm::vec4), kShCoefficients, irradianceCoefficients.data());
    state.exposureConstants = litgpu::uniformBuffer(device, "Exposure Constants", sizeof(ExposureConstants));
    state.prefilterConstants = litgpu::uniformBuffer(device, "Prefilter Constants", sizeof(glm::vec4));
    state.bloomConstants = litgpu::uniformBuffer(device, "Bloom Constants", sizeof(glm::vec4));
    state.godRayConstants = litgpu::uniformBuffer(device, "GodRayConstants", sizeof(glm::vec4));
}

void createStaticTextures(Lighting::Data& state) {
    using litgpu::TextureSpec;
    Diligent::IRenderDevice* device = state.device;
    constexpr uint32_t envMipCount = kEnvMips + 2;
    state.transmittanceLut = litgpu::texture(device, TextureSpec{"Transmittance LUT", Diligent::RESOURCE_DIM_TEX_2D, 256, 64});
    state.multiScatterLut = litgpu::texture(device, TextureSpec{"Multi Scattering LUT", Diligent::RESOURCE_DIM_TEX_2D, 32, 32});
    state.skyViewLut = litgpu::texture(device, TextureSpec{"Sky View LUT", Diligent::RESOURCE_DIM_TEX_2D, kSkyViewWidth, kSkyViewHeight});
    state.aerialLut = litgpu::texture(device, TextureSpec{"Aerial Perspective LUT", Diligent::RESOURCE_DIM_TEX_3D, 32, 32, 32});
    state.envCapture = litgpu::texture(device, TextureSpec{"Environment Capture", Diligent::RESOURCE_DIM_TEX_CUBE, kEnvSize, kEnvSize, 6, envMipCount});
    for (uint32_t mip = 0; mip < envMipCount; ++mip) state.envCaptureMips[mip] = litgpu::mipView(state.envCapture, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D_ARRAY, 0, 6);
    state.envPrefiltered = litgpu::texture(device, TextureSpec{"Prefiltered Environment", Diligent::RESOURCE_DIM_TEX_CUBE, kEnvSize, kEnvSize, 6, kEnvMips});
    for (uint32_t mip = 0; mip < kEnvMips; ++mip) state.envPrefilteredMips[mip] = litgpu::mipView(state.envPrefiltered, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D_ARRAY, 0, 6);
    state.dfg = litgpu::texture(device, TextureSpec{"DFG LUT", Diligent::RESOURCE_DIM_TEX_2D, 128, 128});
    state.cloudNoise = litgpu::texture(device, TextureSpec{"Cloud Noise", Diligent::RESOURCE_DIM_TEX_3D, 128, 128, 128, kCloudNoiseMips, Diligent::TEX_FORMAT_RGBA8_UNORM});
    state.hdri = litgpu::texture(device, TextureSpec{"HDRI", Diligent::RESOURCE_DIM_TEX_2D, 4, 2, 1, 1, Diligent::TEX_FORMAT_RGBA16_FLOAT, Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS});
    state.colorTextures = placeholderArray(device, "Color Textures", Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB, 0xFFFFFFFFu);
    state.dataTextures = placeholderArray(device, "Data Textures", Diligent::TEX_FORMAT_RGBA8_UNORM, 0xFFFF8080u);
    createShadowArray(device, "Shadow Cascades", kInitialShadowResolution, kShadowLayers, state.shadowMap, state.shadowTargets);
    state.shadowResolution = kInitialShadowResolution;
    for (int cascadeIndex = 0; cascadeIndex < kCascadeSlots; ++cascadeIndex) state.cascades[cascadeIndex].layer = state.cascades[cascadeIndex].renderLayer = cascadeIndex;
    createShadowArray(device, "Local Shadows", kInitialShadowResolution, kMaxLocalShadowViews, state.localShadowMap, state.localShadowTargets);
    state.localShadowResolution = kInitialShadowResolution;
}

void createSceneTextures(Lighting::Data& state) {
    using litgpu::TextureSpec;
    Diligent::IRenderDevice* device = state.device;
    const auto width = static_cast<uint32_t>(state.width);
    const auto height = static_cast<uint32_t>(state.height);
    const uint32_t halfWidth = std::max(width / 2, 1u);
    const uint32_t halfHeight = std::max(height / 2, 1u);
    const Diligent::BIND_FLAGS renderTarget = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    state.gbuffer[0] = litgpu::texture(device, TextureSpec{"GBuffer Base Color", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB, renderTarget});
    state.gbuffer[1] = litgpu::texture(device, TextureSpec{"GBuffer Normal Roughness", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, Diligent::TEX_FORMAT_RGB10A2_UNORM, renderTarget});
    state.gbuffer[2] = litgpu::texture(device, TextureSpec{"GBuffer Material", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, Diligent::TEX_FORMAT_RGBA8_UINT, renderTarget});
    state.hdrTargets[0] = litgpu::texture(device, TextureSpec{"HDR Color A", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, kHdrFormat, renderTarget | Diligent::BIND_UNORDERED_ACCESS});
    state.hdrTargets[1] = litgpu::texture(device, TextureSpec{"HDR Color B", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, kHdrFormat, renderTarget | Diligent::BIND_UNORDERED_ACCESS});
    state.ldr = litgpu::texture(device, TextureSpec{"LDR Color", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, kLdrFormat, renderTarget});
    for (auto& upscaled : state.upscaled) upscaled.Release();
    if (upscaleActive(state)) {
        const auto outputWidth = static_cast<uint32_t>(state.outputWidth);
        const auto outputHeight = static_cast<uint32_t>(state.outputHeight);
        state.upscaled[0] = litgpu::texture(device, TextureSpec{"Upscaled Color", Diligent::RESOURCE_DIM_TEX_2D, outputWidth, outputHeight, 1, 1, kLdrFormat});
        state.upscaled[1] = litgpu::texture(device, TextureSpec{"Sharpened Color", Diligent::RESOURCE_DIM_TEX_2D, outputWidth, outputHeight, 1, 1, kLdrFormat});
    }
    state.aoRaw = litgpu::texture(device, TextureSpec{"GTAO Raw", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, kHdrFormat});
    state.aoBentRaw = litgpu::texture(device, TextureSpec{"GTAO Bent Raw", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, Diligent::TEX_FORMAT_RGBA8_UNORM});
    state.aoBentTemp = litgpu::texture(device, TextureSpec{"GTAO Bent Blur", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, Diligent::TEX_FORMAT_RGBA8_UNORM});
    state.aoBent = litgpu::texture(device, TextureSpec{"GTAO Bent Normal", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, Diligent::TEX_FORMAT_RGBA8_UNORM});
    state.aoTemp = litgpu::texture(device, TextureSpec{"GTAO Blur", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, kHdrFormat});
    state.clouds = litgpu::texture(device, TextureSpec{"Clouds", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, Diligent::TEX_FORMAT_RGBA16_FLOAT});
    for (auto& rays : state.godRays) rays = litgpu::texture(device, TextureSpec{"God Rays", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, Diligent::TEX_FORMAT_R16_FLOAT});
    state.aoDepth = litgpu::texture(device, TextureSpec{"GTAO Depth", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, Diligent::TEX_FORMAT_R32_FLOAT});
    state.aoFiltered = litgpu::texture(device, TextureSpec{"GTAO", Diligent::RESOURCE_DIM_TEX_2D, halfWidth, halfHeight, 1, 1, kHdrFormat});
    state.sceneColor = litgpu::texture(device, TextureSpec{"Scene Color Pyramid", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, kPyramidMips, kHdrFormat});
    state.sceneColorMips.clear();
    for (uint32_t mip = 0; mip < kPyramidMips; ++mip) state.sceneColorMips.push_back(litgpu::mipView(state.sceneColor, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D));
    state.pyramidValid = false;
    const uint32_t hizMips = std::min<uint32_t>(kMaxHizMips, static_cast<uint32_t>(std::bit_width(std::max(width, height))));
    state.ssrHiz = litgpu::texture(device, TextureSpec{"SSR Hi-Z", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, hizMips, Diligent::TEX_FORMAT_R32_FLOAT});
    state.ssrHizMips.clear();
    for (uint32_t mip = 0; mip < hizMips; ++mip) state.ssrHizMips.push_back(litgpu::mipView(state.ssrHiz, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D));
    state.ssr = litgpu::texture(device, TextureSpec{"SSR", Diligent::RESOURCE_DIM_TEX_2D, (width + 1) / 2, (height + 1) / 2, 1, 1, kHdrFormat});
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        const uint32_t bloomWidth = std::max(width >> (level + 1), 1u);
        const uint32_t bloomHeight = std::max(height >> (level + 1), 1u);
        state.bloomDown[level] = litgpu::texture(device, TextureSpec{"Bloom Down", Diligent::RESOURCE_DIM_TEX_2D, bloomWidth, bloomHeight, 1, 1, kHdrFormat});
        state.bloomUp[level] = litgpu::texture(device, TextureSpec{"Bloom Up", Diligent::RESOURCE_DIM_TEX_2D, bloomWidth, bloomHeight, 1, 1, kHdrFormat});
    }
}

}

namespace lighting_detail {

void loadHdri(Lighting::Data& state) {
    state.hdriPath = state.settings.hdriPath;
    state.hdriLoaded = 0;
    state.envCaptureSrb.Release();
    int width = 0;
    int height = 0;
    int channels = 0;
    float* pixels = state.hdriPath.empty() ? nullptr : stbi_loadf(state.hdriPath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        if (!state.hdriPath.empty()) Lit::Log::Warn("Could not load HDRI {}", state.hdriPath);
        state.envDirty = true;
        return;
    }
    std::vector<uint16_t> half(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (size_t index = 0; index < half.size(); ++index) half[index] = floatToHalf(pixels[index]);
    stbi_image_free(pixels);
    Diligent::TextureDesc desc;
    desc.Name = "HDRI";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<uint32_t>(width);
    desc.Height = static_cast<uint32_t>(height);
    desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    Diligent::TextureSubResData subresource;
    subresource.pData = half.data();
    subresource.Stride = static_cast<Diligent::Uint64>(width) * 8;
    Diligent::TextureData data;
    data.NumSubresources = 1;
    data.pSubResources = &subresource;
    state.hdri.Release();
    state.device->CreateTexture(desc, &data, &state.hdri);
    state.hdriLoaded = state.hdri ? 1 : 0;
    ++state.bindingVersion;
    Lit::Log::Info("Loaded HDRI {} ({}x{})", state.hdriPath, width, height);
    state.envDirty = true;
}

}

MaterialGpu packMaterial(const Material& material) {
    MaterialGpu packed{};
    packed.baseColorOpacity = glm::vec4(material.baseColor, std::clamp(material.opacity, 0.0f, 1.0f));
    packed.emissive = glm::vec4(material.emissive * std::max(material.emissiveIntensity, 0.0f), material.normalStrength);
    packed.pbr = glm::vec4(std::clamp(material.metallic, 0.0f, 1.0f), std::clamp(material.roughness, 0.0f, 1.0f), std::clamp(material.reflectance, 0.0f, 1.0f), std::clamp(material.occlusion, 0.0f, 1.0f));
    packed.coat = glm::vec4(std::clamp(material.clearcoat, 0.0f, 1.0f), std::clamp(material.clearcoatRoughness, 0.0f, 1.0f), std::clamp(material.anisotropy, -1.0f, 1.0f), material.anisotropyRotation);
    packed.sheen = glm::vec4(material.sheenColor, material.sheenRoughness);
    packed.subsurface = glm::vec4(material.subsurfaceColor, std::clamp(material.subsurface, 0.0f, 1.0f));
    packed.transmission = glm::vec4(std::clamp(material.transmission, 0.0f, 1.0f), material.ior, std::clamp(material.thickness, 0.0f, 1.0f), std::max(material.uvScale, 1e-4f));
    packed.textures[0] = material.baseColorTexture;
    packed.textures[1] = material.normalTexture;
    packed.textures[2] = material.ormTexture;
    packed.textures[3] = material.emissiveTexture;
    packed.flags = glm::vec4(material.triplanar ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    packed.custom0 = material.custom0;
    packed.custom1 = material.custom1;
    return packed;
}

Lighting::Lighting() = default;
Lighting::~Lighting() { shutdown(); }

RenderSettings& Lighting::settings() {
    if (!m_data) m_data = new Data();
    return m_data->settings;
}
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

bool Lighting::init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::TEXTURE_FORMAT backBufferFormat, int width, int height) {
    if (!m_data) m_data = new Data();
    Data& state = *m_data;
    state.device = device;
    state.context = context;
    state.backBufferFormat = backBufferFormat;
    createStaticBuffers(state);
    createStaticTextures(state);
    createReflectionResources(state);
    resize(width, height);
    reloadShaders();
    return state.lightingPso != nullptr;
}

void Lighting::shutdown() {
    delete m_data;
    m_data = nullptr;
}

void Lighting::resize(int width, int height, int outputWidth, int outputHeight) {
    Data& state = *m_data;
    state.width = std::max(width, 1);
    state.height = std::max(height, 1);
    state.outputWidth = std::max(outputWidth > 0 ? outputWidth : width, 1);
    state.outputHeight = std::max(outputHeight > 0 ? outputHeight : height, 1);
    createSceneTextures(state);
    ++state.bindingVersion;
    if (state.lightingPso) reloadShaders();
}

void Lighting::setMaterial(uint32_t id, const Material& material) {
    if (!m_data || id >= kMaxMaterials) return;
    const MaterialGpu packed = packMaterial(material);
    m_data->context->UpdateBuffer(m_data->materials, static_cast<Diligent::Uint64>(id) * sizeof(MaterialGpu), sizeof(MaterialGpu), &packed, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Lighting::setLights(const std::vector<GpuLight>& lights) {
    Data& state = *m_data;
    state.lightCount = static_cast<uint32_t>(std::min<size_t>(lights.size(), kMaxLights));
    state.cpuLights.assign(lights.begin(), lights.begin() + state.lightCount);
}

void Lighting::setDirectionalLight(bool present, const glm::vec3& direction, const glm::vec3& illuminance) {
    Data& state = *m_data;
    state.hasDirectional = present;
    if (!present) return;
    state.directionalDirection = glm::normalize(direction);
    state.directionalIlluminance = illuminance;
}

int Lighting::loadTexture(const std::string& path, bool srgb) {
    Data& state = *m_data;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        Lit::Log::Warn("Could not load texture {}", path);
        return -1;
    }
    RefCntAutoPtr<Diligent::ITexture>& target = srgb ? state.colorTextures : state.dataTextures;
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
        RefCntAutoPtr<Diligent::ITexture> textureArray;
        state.device->CreateTexture(desc, nullptr, &textureArray);
        if (!textureArray) {
            stbi_image_free(pixels);
            return -1;
        }
        target = textureArray;
        ++state.bindingVersion;
    }
    int& layers = srgb ? state.colorLayers : state.dataLayers;
    if (layers >= static_cast<int>(kTextureCapacity)) {
        stbi_image_free(pixels);
        Lit::Log::Warn("Texture array is full; {} not loaded", path);
        return -1;
    }
    std::vector<uint32_t> resized(static_cast<size_t>(kTextureSize) * kTextureSize);
    const auto* source = reinterpret_cast<const uint32_t*>(pixels);
    for (uint32_t row = 0; row < kTextureSize; ++row) {
        const int sourceRow = std::min(static_cast<int>(static_cast<uint64_t>(row) * height / kTextureSize), height - 1);
        for (uint32_t column = 0; column < kTextureSize; ++column) {
            const int sourceColumn = std::min(static_cast<int>(static_cast<uint64_t>(column) * width / kTextureSize), width - 1);
            resized[static_cast<size_t>(row) * kTextureSize + column] = source[static_cast<size_t>(sourceRow) * width + sourceColumn];
        }
    }
    stbi_image_free(pixels);
    const Diligent::Box box(0, kTextureSize, 0, kTextureSize);
    Diligent::TextureSubResData subresource;
    subresource.pData = resized.data();
    subresource.Stride = kTextureSize * 4;
    state.context->UpdateTexture(target, 0, static_cast<Diligent::Uint32>(layers), box, subresource, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    state.context->GenerateMips(litgpu::srv(target));
    return layers++;
}
