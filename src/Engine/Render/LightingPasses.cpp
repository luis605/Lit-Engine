module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

module Engine.Render.lighting;

import Engine.glm;
import Engine.Render.settings;
import Engine.Render.material;

#include "LightingInternal.hpp"

using namespace lighting_detail;

namespace {

constexpr uint32_t kDfgGroups = 16;
constexpr uint32_t kTransmittanceGroupsX = 32;
constexpr uint32_t kTransmittanceGroupsY = 8;
constexpr uint32_t kMultiScatterGroups = 32;
constexpr uint32_t kAerialGroups = 8;
constexpr uint32_t kCloudNoiseGroups = 32;
constexpr uint32_t kCloudNoiseResolution = 128;
constexpr uint32_t kCloudNoiseDownsampleThreads = 4;
constexpr uint32_t kCubeFaceCount = 6;
constexpr uint32_t kLightCullGroupSize = 64;
constexpr float kAtmosphereHashScale = 100000.0f;
constexpr uint64_t kAtmosphereHashMultiplier = 1315423911ull;

uint32_t groupCount(uint32_t size) { return (size + kThreadGroupSize - 1) / kThreadGroupSize; }

void generateDfg(Lighting::Data& state) {
    if (!state.dfgDirty || !state.dfgPso) return;
    state.dfgSrb = litgpu::makeBinding(state.dfgPso, {{"u_output", litgpu::uav(state.dfg)}});
    litgpu::dispatch(state.context, state.dfgPso, state.dfgSrb, kDfgGroups, kDfgGroups);
    state.dfgDirty = false;
}

void updateAtmosphere(Lighting::Data& state, Diligent::IBuffer* scene) {
    if (!state.transmittanceSrb || state.lutsDirty) {
        state.transmittanceSrb = litgpu::makeBinding(state.transmittancePso, {{"SceneData", scene}, {"u_output", litgpu::uav(state.transmittanceLut)}});
        state.multiScatterSrb = litgpu::makeBinding(state.multiScatterPso, {{"SceneData", scene}, {"u_output", litgpu::uav(state.multiScatterLut)}, {"u_transmittanceLut", litgpu::srv(state.transmittanceLut)}});
        state.skyViewSrb = litgpu::makeBinding(state.skyViewPso, {{"SceneData", scene}, {"u_output", litgpu::uav(state.skyViewLut)}, {"u_transmittanceLut", litgpu::srv(state.transmittanceLut)}, {"u_multiScatterLut", litgpu::srv(state.multiScatterLut)}});
        state.aerialSrb = litgpu::makeBinding(state.aerialPso, {{"SceneData", scene}, {"u_output", litgpu::uav(state.aerialLut)}, {"u_transmittanceLut", litgpu::srv(state.transmittanceLut)}, {"u_multiScatterLut", litgpu::srv(state.multiScatterLut)}});
    }
    const RenderSettings& settings = state.settings;
    uint64_t hash = 0;
    for (float value : {settings.rayleighScale, settings.mieScale, settings.ozoneScale, settings.groundAlbedo, settings.mieAnisotropy}) hash = hash * kAtmosphereHashMultiplier + static_cast<uint64_t>(value * kAtmosphereHashScale);
    if (hash != state.atmosphereHash || state.lutsDirty) {
        state.atmosphereHash = hash;
        litgpu::dispatch(state.context, state.transmittancePso, state.transmittanceSrb, kTransmittanceGroupsX, kTransmittanceGroupsY);
        litgpu::dispatch(state.context, state.multiScatterPso, state.multiScatterSrb, kMultiScatterGroups, kMultiScatterGroups);
        state.envDirty = true;
    }
    state.lutsDirty = false;
    litgpu::dispatch(state.context, state.skyViewPso, state.skyViewSrb, groupCount(kSkyViewWidth), groupCount(kSkyViewHeight));
    litgpu::dispatch(state.context, state.aerialPso, state.aerialSrb, kAerialGroups, kAerialGroups, kAerialGroups);
}

void generateCloudNoise(Lighting::Data& state) {
    if (!state.cloudNoiseDirty || !state.cloudNoisePso || !state.cloudNoiseDownPso) return;
    std::vector<RefCntAutoPtr<Diligent::ITextureView>> mips;
    mips.reserve(kCloudNoiseMips);
    for (uint32_t mip = 0; mip < kCloudNoiseMips; ++mip) mips.push_back(litgpu::mipView(state.cloudNoise, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_3D));
    auto baseBinding = litgpu::makeBinding(state.cloudNoisePso, {{"u_output", mips[0]}});
    litgpu::dispatch(state.context, state.cloudNoisePso, baseBinding, kCloudNoiseGroups, kCloudNoiseGroups, kCloudNoiseGroups);
    for (uint32_t mip = 1; mip < kCloudNoiseMips; ++mip) {
        litgpu::uavBarrier(state.context, state.cloudNoise);
        const uint32_t groups = std::max((kCloudNoiseResolution >> mip) / kCloudNoiseDownsampleThreads, 1u);
        auto downBinding = litgpu::makeBinding(state.cloudNoiseDownPso, {{"u_source", mips[mip - 1]}, {"u_output", mips[mip]}});
        litgpu::dispatch(state.context, state.cloudNoiseDownPso, downBinding, groups, groups, groups);
    }
    state.cloudNoiseDirty = false;
}

void createEnvironmentBindings(Lighting::Data& state, Diligent::IBuffer* scene) {
    state.envCaptureSrb = litgpu::makeBinding(state.envCapturePso, {{"SceneData", scene}, {"u_output", state.envCaptureMips[0]}, {"u_transmittanceLut", litgpu::srv(state.transmittanceLut)}, {"u_skyViewLut", litgpu::srv(state.skyViewLut)}, {"u_hdri", litgpu::srv(state.hdri)}, {"u_cloudNoise", litgpu::srv(state.cloudNoise)}});
    state.envDownsampleSrbs.clear();
    for (uint32_t mip = 1; mip < kEnvMips + 2; ++mip) state.envDownsampleSrbs.push_back(litgpu::makeBinding(state.envDownsamplePso, {{"u_source", state.envCaptureMips[mip - 1]}, {"u_dest", state.envCaptureMips[mip]}}));
    state.envPrefilterSrbs.clear();
    for (uint32_t mip = 0; mip < kEnvMips; ++mip) state.envPrefilterSrbs.push_back(litgpu::makeBinding(state.envPrefilterPso, {{"PrefilterConstants", state.prefilterConstants}, {"u_output", state.envPrefilteredMips[mip]}, {"u_environment", litgpu::srv(state.envCapture)}}));
    state.envShSrb = litgpu::makeBinding(state.envShPso, {{"u_environment", litgpu::srv(state.envCapture)}, {"IrradianceBuffer", state.irradiance}});
}

void rebuildEnvironment(Lighting::Data& state, Diligent::IBuffer* scene) {
    if (!state.envDirty) return;
    state.envDirty = false;
    state.cloudTravelSinceCapture = 0.0f;
    ++state.envVersion;
    if (!state.envCaptureSrb || state.envDownsampleSrbs.empty()) createEnvironmentBindings(state, scene);
    Diligent::IDeviceContext* context = state.context;
    litgpu::dispatch(context, state.envCapturePso, state.envCaptureSrb, kEnvSize / kThreadGroupSize, kEnvSize / kThreadGroupSize, kCubeFaceCount);
    for (uint32_t mip = 1; mip < kEnvMips + 2; ++mip) {
        litgpu::uavBarrier(context, state.envCapture);
        const uint32_t groups = groupCount(std::max(kEnvSize >> mip, 1u));
        litgpu::dispatch(context, state.envDownsamplePso, state.envDownsampleSrbs[mip - 1], groups, groups, kCubeFaceCount);
    }
    for (uint32_t mip = 0; mip < kEnvMips; ++mip) {
        const glm::vec4 params(static_cast<float>(mip) / static_cast<float>(kEnvMips - 1), static_cast<float>(kEnvSize), static_cast<float>(kEnvMips + 2), 0.0f);
        context->UpdateBuffer(state.prefilterConstants, 0, sizeof(params), &params, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const uint32_t groups = groupCount(std::max(kEnvSize >> mip, 1u));
        litgpu::dispatch(context, state.envPrefilterPso, state.envPrefilterSrbs[mip], groups, groups, kCubeFaceCount);
    }
    litgpu::dispatch(context, state.envShPso, state.envShSrb, 1);
}

Diligent::ITextureView* srvOf(const RefCntAutoPtr<Diligent::ITexture>& texture) { return litgpu::srv(texture); }
Diligent::IDeviceObject* uavOf(const RefCntAutoPtr<Diligent::ITexture>& texture) { return litgpu::uav(texture); }

void createScreenSpaceBindings(Lighting::Data& state, Diligent::IBuffer* scene, Diligent::ITextureView* depthView) {
    state.cloudsSrb = litgpu::makeBinding(state.cloudsPso, {{"SceneData", scene}, {"u_output", uavOf(state.clouds)}, {"u_transmittanceLut", srvOf(state.transmittanceLut)}, {"u_skyViewLut", srvOf(state.skyViewLut)}, {"u_hdri", srvOf(state.hdri)}, {"u_cloudNoise", srvOf(state.cloudNoise)}});
    state.gtaoDepthSrb = litgpu::makeBinding(state.gtaoDepthPso, {{"SceneData", scene}, {"u_depth", depthView}, {"u_output", uavOf(state.aoDepth)}});
    state.gtaoSrb = litgpu::makeBinding(state.gtaoPso, {{"SceneData", scene}, {"u_depth", depthView}, {"u_linearDepth", srvOf(state.aoDepth)}, {"u_gbuffer1", srvOf(state.gbuffer[1])}, {"u_gbuffer0", srvOf(state.gbuffer[0])}, {"u_output", uavOf(state.aoRaw)}, {"u_bent", uavOf(state.aoBentRaw)}});
    state.gtaoBlurSrb = litgpu::makeBinding(state.gtaoBlurPso, {{"SceneData", scene}, {"u_linearDepth", srvOf(state.aoDepth)}, {"u_input", srvOf(state.aoRaw)}, {"u_bentInput", srvOf(state.aoBentRaw)}, {"u_output", uavOf(state.aoTemp)}, {"u_bentOutput", uavOf(state.aoBentTemp)}});
    state.gtaoBlurVerticalSrb = litgpu::makeBinding(state.gtaoBlurVerticalPso, {{"SceneData", scene}, {"u_linearDepth", srvOf(state.aoDepth)}, {"u_input", srvOf(state.aoTemp)}, {"u_bentInput", srvOf(state.aoBentTemp)}, {"u_output", uavOf(state.aoFiltered)}, {"u_bentOutput", uavOf(state.aoBent)}});
    state.ssrHizFirstSrb = litgpu::makeBinding(state.ssrHizFirstPso, {{"u_depth", depthView}, {"u_dest", state.ssrHizMips[0]}});
    state.ssrHizSrbs.clear();
    for (size_t mip = 1; mip < state.ssrHizMips.size(); ++mip) state.ssrHizSrbs.push_back(litgpu::makeBinding(state.ssrHizPso, {{"u_sourceMip", state.ssrHizMips[mip - 1]}, {"u_dest", state.ssrHizMips[mip]}}));
}

void createParityBindings(Lighting::Data& state, Diligent::IBuffer* scene, Diligent::ITextureView* depthView, int parity) {
    state.ssrSrb[parity] = litgpu::makeBinding(state.ssrPso, {{"SceneData", scene}, {"u_depth", depthView}, {"u_gbuffer1", srvOf(state.gbuffer[1])}, {"u_sceneColor", srvOf(state.sceneColor)}, {"u_hiz", srvOf(state.ssrHiz)}, {"u_output", uavOf(state.ssr)}});
    state.lightingSrb[parity] = litgpu::makeBinding(state.lightingPso, {{"SceneData", scene},
                                                                        {"u_depth", depthView},
                                                                        {"u_gbuffer0", srvOf(state.gbuffer[0])},
                                                                        {"u_gbuffer1", srvOf(state.gbuffer[1])},
                                                                        {"u_gbuffer2", srvOf(state.gbuffer[2])},
                                                                        {"u_ao", srvOf(state.aoFiltered)},
                                                                        {"u_aoDepth", srvOf(state.aoDepth)},
                                                                        {"u_bent", srvOf(state.aoBent)},
                                                                        {"u_clouds", srvOf(state.clouds)},
                                                                        {"u_aerial", srvOf(state.aerialLut)},
                                                                        {"u_transmittanceLut", srvOf(state.transmittanceLut)},
                                                                        {"u_skyViewLut", srvOf(state.skyViewLut)},
                                                                        {"u_hdri", srvOf(state.hdri)},
                                                                        {"u_prefilteredEnv", srvOf(state.envPrefiltered)},
                                                                        {"u_dfgLut", srvOf(state.dfg)},
                                                                        {"u_shadowMap", srvOf(state.shadowMap)},
                                                                        {"u_shadowDepth", srvOf(state.shadowMap)},
                                                                        {"u_localShadowMap", srvOf(state.localShadowMap)},
                                                                        {"u_hdr", uavOf(state.hdrTargets[parity])},
                                                                        {"MaterialBuffer", state.materials},
                                                                        {"LightBuffer", state.lights},
                                                                        {"ClusterBuffer", state.clusters},
                                                                        {"ReflectionData", state.reflections.constants},
                                                                        {"u_skyVolume", srvOf(state.reflections.skyVolume)},
                                                                        {"IrradianceBuffer", state.irradiance},
                                                                        {"ExposureBuffer", state.exposure}});
}

void dispatchAmbientOcclusion(Lighting::Data& state) {
    const auto& desc = state.aoRaw->GetDesc();
    const uint32_t groupsX = groupCount(desc.Width);
    const uint32_t groupsY = groupCount(desc.Height);
    litgpu::dispatch(state.context, state.gtaoDepthPso, state.gtaoDepthSrb, groupsX, groupsY);
    litgpu::dispatch(state.context, state.gtaoPso, state.gtaoSrb, groupsX, groupsY);
    litgpu::dispatch(state.context, state.gtaoBlurPso, state.gtaoBlurSrb, groupsX, groupsY);
    litgpu::dispatch(state.context, state.gtaoBlurVerticalPso, state.gtaoBlurVerticalSrb, groupsX, groupsY);
}

void dispatchScreenSpaceReflections(Lighting::Data& state) {
    const auto& hizDesc = state.ssrHiz->GetDesc();
    litgpu::dispatch(state.context, state.ssrHizFirstPso, state.ssrHizFirstSrb, groupCount(hizDesc.Width), groupCount(hizDesc.Height));
    for (size_t mip = 1; mip < state.ssrHizMips.size(); ++mip) {
        litgpu::uavBarrier(state.context, state.ssrHiz);
        const uint32_t mipWidth = std::max(hizDesc.Width >> mip, 1u);
        const uint32_t mipHeight = std::max(hizDesc.Height >> mip, 1u);
        litgpu::dispatch(state.context, state.ssrHizPso, state.ssrHizSrbs[mip - 1], groupCount(mipWidth), groupCount(mipHeight));
    }
    const auto& ssrDesc = state.ssr->GetDesc();
    litgpu::dispatch(state.context, state.ssrPso, state.ssrSrb[state.hdrIndex], groupCount(ssrDesc.Width), groupCount(ssrDesc.Height));
}

}

void Lighting::prepare(Diligent::IBuffer* sceneUniforms) {
    Data& state = *m_data;
    if (state.settings.hdriPath != state.hdriPath) loadHdri(state);
    generateDfg(state);
    updateAtmosphere(state, sceneUniforms);
    generateCloudNoise(state);
    rebuildEnvironment(state, sceneUniforms);
    updateReflectionAssets(state, sceneUniforms);
    cullLights(sceneUniforms);
}

void Lighting::cullLights(Diligent::IBuffer* sceneUniforms) {
    Data& state = *m_data;
    if (!state.lightCullSrb) state.lightCullSrb = litgpu::makeBinding(state.lightCullPso, {{"SceneData", sceneUniforms}, {"LightBuffer", state.lights}, {"ClusterBuffer", state.clusters}});
    litgpu::dispatch(state.context, state.lightCullPso, state.lightCullSrb, (kClusterCount + kLightCullGroupSize - 1) / kLightCullGroupSize);
}

void Lighting::shade(Diligent::ITexture* depth, Diligent::IBuffer* sceneUniforms, int stage) {
    Data& state = *m_data;
    if (state.shadeVersion != state.bindingVersion || state.shadeDepth != depth || !state.lightingSrb[0]) {
        state.shadeVersion = state.bindingVersion;
        state.shadeDepth = depth;
        Diligent::ITextureView* depthView = litgpu::srv(depth);
        createScreenSpaceBindings(state, sceneUniforms, depthView);
        for (int parity = 0; parity < 2; ++parity) createParityBindings(state, sceneUniforms, depthView, parity);
    }
    const uint32_t groupsX = groupCount(static_cast<uint32_t>(state.width));
    const uint32_t groupsY = groupCount(static_cast<uint32_t>(state.height));
    const bool runAll = stage < 0;
    const bool reflectionsOn = reflectionPlan(state.settings).enabled();
    if (state.settings.ssao && (runAll || stage == 0)) dispatchAmbientOcclusion(state);
    if (runAll || stage == 1) {
        const auto& cloudDesc = state.clouds->GetDesc();
        litgpu::dispatch(state.context, state.cloudsPso, state.cloudsSrb, groupCount(cloudDesc.Width), groupCount(cloudDesc.Height));
        litgpu::dispatch(state.context, state.lightingPso, state.lightingSrb[state.hdrIndex], groupsX, groupsY);
    }
    if (reflectionsOn && (runAll || stage == 2)) {
        captureSceneColor();
        dispatchScreenSpaceReflections(state);
        runReflectionComposite(state, sceneUniforms, groupsX, groupsY);
    }
}

void Lighting::captureSceneColor() {
    Data& state = *m_data;
    if (!state.pyramidFirstPso || !state.pyramidPso || state.sceneColorMips.size() < kPyramidMips) return;
    if (state.pyramidVersion != state.bindingVersion || !state.pyramidFirstSrb[0]) {
        state.pyramidVersion = state.bindingVersion;
        for (int parity = 0; parity < 2; ++parity) state.pyramidFirstSrb[parity] = litgpu::makeBinding(state.pyramidFirstPso, {{"u_source", litgpu::srv(state.hdrTargets[parity])}, {"u_dest", state.sceneColorMips[0]}});
        state.pyramidSrbs.clear();
        for (uint32_t mip = 1; mip < kPyramidMips; ++mip) state.pyramidSrbs.push_back(litgpu::makeBinding(state.pyramidPso, {{"u_sourceMip", state.sceneColorMips[mip - 1]}, {"u_dest", state.sceneColorMips[mip]}}));
    }
    const auto& desc = state.sceneColor->GetDesc();
    litgpu::dispatch(state.context, state.pyramidFirstPso, state.pyramidFirstSrb[state.hdrIndex], groupCount(desc.Width), groupCount(desc.Height));
    for (uint32_t mip = 1; mip < kPyramidMips; ++mip) {
        litgpu::uavBarrier(state.context, state.sceneColor);
        const uint32_t mipWidth = std::max(desc.Width >> mip, 1u);
        const uint32_t mipHeight = std::max(desc.Height >> mip, 1u);
        litgpu::dispatch(state.context, state.pyramidPso, state.pyramidSrbs[mip - 1], groupCount(mipWidth), groupCount(mipHeight));
    }
    state.pyramidValid = true;
}
