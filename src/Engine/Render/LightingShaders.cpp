module;

#include "GpuHelpers.hpp"

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

NamedSampler linearSampler(const char* name) { return NamedSampler{name, litgpu::linearClamp()}; }
NamedSampler pointSampler(const char* name) { return NamedSampler{name, litgpu::pointClamp()}; }

void loadSkyPipelines(Lighting::Data& state) {
    Diligent::IRenderDevice* device = state.device;
    const NamedSampler transmittanceSampler = linearSampler("u_transmittanceLut");
    const NamedSampler wrapHdri{"u_hdri", litgpu::linearWrap()};
    const NamedSampler wrapCloudNoise{"u_cloudNoise", litgpu::linearWrap()};
    state.transmittancePso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_transmittance.comp");
    state.multiScatterPso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_multiscatter.comp", {transmittanceSampler});
    state.skyViewPso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_view.comp", {transmittanceSampler, linearSampler("u_multiScatterLut")});
    state.aerialPso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_aerial.comp", {transmittanceSampler, linearSampler("u_multiScatterLut")});
    state.envCapturePso = litgpu::computePipeline(device, "resources/shaders/lighting/env_capture.comp", {transmittanceSampler, linearSampler("u_skyViewLut"), wrapHdri, wrapCloudNoise});
    state.cloudNoisePso = litgpu::computePipeline(device, "resources/shaders/lighting/cloud_noise.comp");
    state.cloudNoiseDownPso = litgpu::computePipeline(device, "resources/shaders/lighting/cloud_noise.comp", {}, {{"DOWNSAMPLE", "1"}});
    state.cloudNoiseDirty = true;
    state.envDownsamplePso = litgpu::computePipeline(device, "resources/shaders/lighting/env_downsample.comp");
    state.envPrefilterPso = litgpu::computePipeline(device, "resources/shaders/lighting/env_prefilter.comp", {linearSampler("u_environment")});
    state.envShPso = litgpu::computePipeline(device, "resources/shaders/lighting/env_sh.comp", {linearSampler("u_environment")});
    state.dfgPso = litgpu::computePipeline(device, "resources/shaders/lighting/dfg_lut.comp");
    state.lightCullPso = litgpu::computePipeline(device, "resources/shaders/lighting/light_cull.comp");
    state.cloudsPso = litgpu::computePipeline(device, "resources/shaders/lighting/clouds.comp", {transmittanceSampler, linearSampler("u_skyViewLut"), wrapHdri, wrapCloudNoise});
}

void loadGodRayPipelines(Lighting::Data& state) {
    Diligent::IRenderDevice* device = state.device;
    state.godRayMaskPso = litgpu::computePipeline(device, "resources/shaders/post/godrays.comp", {pointSampler("u_depth"), linearSampler("u_clouds")}, {{"MASK", "1"}});
    state.godRayBlurPso = litgpu::computePipeline(device, "resources/shaders/post/godrays.comp", {linearSampler("u_input")}, {{"BLUR", "1"}});
    state.godRayCompositePso = litgpu::computePipeline(device, "resources/shaders/post/godrays.comp", {linearSampler("u_shafts")});
    state.godRayVersion = 0;
}

void loadScreenSpacePipelines(Lighting::Data& state) {
    Diligent::IRenderDevice* device = state.device;
    state.gtaoDepthPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao_depth.comp", {pointSampler("u_depth")});
    state.gtaoPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao.comp", {pointSampler("u_depth"), pointSampler("u_linearDepth"), pointSampler("u_gbuffer1"), linearSampler("u_gbuffer0")});
    state.gtaoBlurPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao_blur.comp", {pointSampler("u_linearDepth"), pointSampler("u_input"), pointSampler("u_bentInput")});
    state.gtaoBlurVerticalPso = litgpu::computePipeline(device, "resources/shaders/lighting/gtao_blur.comp", {pointSampler("u_linearDepth"), pointSampler("u_input"), pointSampler("u_bentInput")}, {{"VERTICAL", "1"}});
    state.pyramidFirstPso = litgpu::computePipeline(device, "resources/shaders/lighting/color_pyramid.comp", {linearSampler("u_source")}, {{"FIRST_LEVEL", "1"}});
    state.pyramidPso = litgpu::computePipeline(device, "resources/shaders/lighting/color_pyramid.comp");
    state.ssrHizFirstPso = litgpu::computePipeline(device, "resources/shaders/lighting/ssr_hiz.comp", {pointSampler("u_depth")}, {{"FIRST_LEVEL", "1"}});
    state.ssrHizPso = litgpu::computePipeline(device, "resources/shaders/lighting/ssr_hiz.comp");
    state.ssrPso = litgpu::computePipeline(device, "resources/shaders/lighting/ssr.comp", {pointSampler("u_depth"), pointSampler("u_gbuffer1"), linearSampler("u_sceneColor"), pointSampler("u_hiz")});
}

void loadShadingPipeline(Lighting::Data& state) {
    state.lightingPso = litgpu::computePipeline(state.device, "resources/shaders/lighting/lighting.comp",
                                                {pointSampler("u_depth"),
                                                 pointSampler("u_gbuffer0"),
                                                 pointSampler("u_gbuffer1"),
                                                 pointSampler("u_gbuffer2"),
                                                 pointSampler("u_ao"),
                                                 linearSampler("u_aerial"),
                                                 linearSampler("u_transmittanceLut"),
                                                 linearSampler("u_skyViewLut"),
                                                 {"u_hdri", litgpu::linearWrap()},
                                                 linearSampler("u_prefilteredEnv"),
                                                 linearSampler("u_dfgLut"),
                                                 {"u_shadowMap", litgpu::shadowCompare()},
                                                 pointSampler("u_shadowDepth"),
                                                 {"u_localShadowMap", litgpu::shadowCompare()},
                                                 pointSampler("u_aoDepth"),
                                                 pointSampler("u_bent"),
                                                 linearSampler("u_clouds"),
                                                 linearSampler("u_skyVolume")});
}

void loadPostPipelines(Lighting::Data& state) {
    Diligent::IRenderDevice* device = state.device;
    state.histogramPso = litgpu::computePipeline(device, "resources/shaders/post/histogram.comp", {pointSampler("u_hdr")});
    state.exposurePso = litgpu::computePipeline(device, "resources/shaders/post/exposure.comp");
    state.commitPso = litgpu::computePipeline(device, "resources/shaders/post/exposure_commit.comp");
    state.bloomDownPso = litgpu::computePipeline(device, "resources/shaders/post/bloom_down.comp", {linearSampler("u_source")});
    state.bloomUpPso = litgpu::computePipeline(device, "resources/shaders/post/bloom_up.comp", {linearSampler("u_lower"), linearSampler("u_current")});
    state.tonemapPso = litgpu::fullscreenPipeline(device, "Tonemap", "resources/shaders/post/tonemap.frag", kLdrFormat, {pointSampler("u_hdr"), linearSampler("u_bloom"), pointSampler("u_depth"), pointSampler("u_gbuffer2")});
    state.easuPso = litgpu::computePipeline(device, "resources/shaders/post/easu.comp", {pointSampler("u_source")});
    state.rcasPso = litgpu::computePipeline(device, "resources/shaders/post/rcas.comp", {pointSampler("u_source")});
    state.fxaaPso = litgpu::fullscreenPipeline(device, "FXAA", "resources/shaders/post/fxaa.frag", state.backBufferFormat, {linearSampler("u_ldr")});
}

void releaseStaleBindings(Lighting::Data& state) {
    state.envCaptureSrb.Release();
    state.envShSrb.Release();
    state.lightCullSrb.Release();
    state.envDownsampleSrbs.clear();
    state.envPrefilterSrbs.clear();
}

}

void Lighting::reloadShaders() {
    Data& state = *m_data;
    loadSkyPipelines(state);
    loadGodRayPipelines(state);
    loadScreenSpacePipelines(state);
    loadShadingPipeline(state);
    loadPostPipelines(state);
    loadReflectionPipelines(state);
    releaseStaleBindings(state);
    state.lutsDirty = true;
    state.dfgDirty = true;
    state.envDirty = true;
    ++state.bindingVersion;
}
