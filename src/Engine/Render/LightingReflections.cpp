module;

#include "GpuHelpers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

constexpr uint32_t kGroupSize = 8;
constexpr uint32_t kPlaceholderSize = 1;

uint32_t groupCount(uint32_t size) { return (size + kGroupSize - 1) / kGroupSize; }

litgpu::NamedSampler linearSampler(const char* name) { return litgpu::NamedSampler{name, litgpu::linearClamp()}; }
litgpu::NamedSampler pointSampler(const char* name) { return litgpu::NamedSampler{name, litgpu::pointClamp()}; }

void createPlanarTextures(Lighting::Data& state, uint32_t width, uint32_t height) {
    using litgpu::TextureSpec;
    ReflectionGpu& reflections = state.reflections;
    reflections.planarDepth = litgpu::texture(state.device, TextureSpec{"Planar Reflection Key", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, Diligent::TEX_FORMAT_R32_UINT});
    reflections.planarColor = litgpu::texture(state.device, TextureSpec{"Planar Reflection Color", Diligent::RESOURCE_DIM_TEX_2D, width, height, 1, 1, kHdrFormat});
    reflections.planarWidth = static_cast<int>(width);
    reflections.planarHeight = static_cast<int>(height);
    reflections.bindingVersion = 0;
}

void uploadVoxels(Lighting::Data& state) {
    ReflectionGpu& reflections = state.reflections;
    const ReflectionVoxelGrid& grid = reflections.grid;
    if (!reflections.voxels || !grid.valid()) return;
    const auto resolution = static_cast<Diligent::Uint32>(grid.resolution);
    Diligent::TextureSubResData subresource;
    subresource.pData = grid.cells.data();
    subresource.Stride = static_cast<Diligent::Uint64>(resolution) * sizeof(uint32_t);
    subresource.DepthStride = static_cast<Diligent::Uint64>(resolution) * resolution * sizeof(uint32_t);
    const Diligent::Box box(0, resolution, 0, resolution, 0, resolution);
    state.context->UpdateTexture(reflections.voxels, 0, 0, box, subresource, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    reflections.gridDirty = false;
    reflections.skyVolumeDirty = true;
}

void buildSkyVolume(Lighting::Data& state, Diligent::IBuffer* scene) {
    ReflectionGpu& reflections = state.reflections;
    if (!reflections.skyVolumePso || !reflections.skyVolume || !reflections.grid.valid()) return;
    reflections.skyVolumeSrb = litgpu::makeBinding(reflections.skyVolumePso, {{"SceneData", scene}, {"ReflectionData", reflections.constants}, {"u_voxels", litgpu::srv(reflections.voxels)}, {"u_skyVolume", litgpu::uav(reflections.skyVolume)}}, false);
    const uint32_t groups = (static_cast<uint32_t>(kReflectionVoxelResolution) + kSkyVolumeGroupSize - 1) / kSkyVolumeGroupSize;
    litgpu::dispatch(state.context, reflections.skyVolumePso, reflections.skyVolumeSrb, groups, groups, groups);
    reflections.skyVolumeDirty = false;
}

ReflectionConstants buildConstants(const Lighting::Data& state, const ReflectionPlan& plan, int probeCount) {
    const ReflectionGpu& reflections = state.reflections;
    const RenderSettings& settings = state.settings;
    ReflectionConstants constants{};
    constants.mode = glm::vec4(static_cast<float>(plan.features), settings.reflectionIntensity, settings.ssrMaxRoughness * plan.tier.roughnessScale, static_cast<float>(plan.tier.voxelStepCap));
    const bool voxelsReady = reflections.grid.valid() && !reflections.gridDirty;
    const bool planar = plan.has(kReflectionModePlanar) && reflections.planeIndex >= 0 && reflections.planarWidth > static_cast<int>(kPlaceholderSize);
    constants.counts = glm::vec4(static_cast<float>(probeCount), planar ? 1.0f : 0.0f, voxelsReady ? 1.0f : 0.0f, static_cast<float>(plan.tier.planarDivisor));
    if (planar) {
        const ReflectionPlane& plane = reflections.scene.planes[static_cast<size_t>(reflections.planeIndex)];
        constants.planeCenter = glm::vec4(plane.center, plane.halfSize.x);
        constants.planeNormal = glm::vec4(plane.normal, plane.halfSize.y);
        constants.planeAxisU = glm::vec4(plane.axisU, 0.0f);
    }
    constants.voxelMin = glm::vec4(reflections.grid.minCorner, settings.reflectionTraceDistance);
    constants.voxelSize = glm::vec4(reflections.grid.size, static_cast<float>(reflections.grid.resolution));
    for (int index = 0; index < probeCount; ++index) {
        const ReflectionProbe& probe = reflections.scene.probes[static_cast<size_t>(index)];
        constants.probeCenter[index] = glm::vec4(probe.center, probe.intensity);
        constants.probeExtent[index] = glm::vec4(probe.halfExtents, probe.blendDistance);
    }
    return constants;
}

void createProbeBindings(Lighting::Data& state, Diligent::IBuffer* scene) {
    ReflectionGpu& reflections = state.reflections;
    reflections.probeBakeSrb = litgpu::makeBinding(reflections.probeBakePso, {{"SceneData", scene}, {"ReflectionData", reflections.constants}, {"u_envCapture", litgpu::srv(state.envCapture)}, {"u_voxels", litgpu::srv(reflections.voxels)}, {"u_output", reflections.probeMips[0]}}, false);
    reflections.probeDownSrbs.clear();
    for (uint32_t mip = 1; mip < kProbeMips; ++mip) reflections.probeDownSrbs.push_back(litgpu::makeBinding(state.envDownsamplePso, {{"u_source", reflections.probeMips[mip - 1]}, {"u_dest", reflections.probeMips[mip]}}, false));
}

void runProbeBake(Lighting::Data& state, int probeCount) {
    ReflectionGpu& reflections = state.reflections;
    const uint32_t resolution = reflections.probeResolution;
    const uint32_t slices = static_cast<uint32_t>(probeCount) * kProbeCubeFaces;
    litgpu::dispatch(state.context, reflections.probeBakePso, reflections.probeBakeSrb, groupCount(resolution), groupCount(resolution), slices);
    for (uint32_t mip = 1; mip < kProbeMips; ++mip) {
        litgpu::uavBarrier(state.context, reflections.probeCubes);
        const uint32_t groups = groupCount(std::max(resolution >> mip, 1u));
        litgpu::dispatch(state.context, state.envDownsamplePso, reflections.probeDownSrbs[mip - 1], groups, groups, slices);
    }
    reflections.bake = ProbeBakeState{false, probeCount, resolution};
}

void createProbeTexture(Lighting::Data& state, uint32_t resolution) {
    using litgpu::TextureSpec;
    ReflectionGpu& reflections = state.reflections;
    reflections.probeResolution = resolution;
    reflections.probeCubes = litgpu::texture(state.device, TextureSpec{"Reflection Probes", Diligent::RESOURCE_DIM_TEX_CUBE_ARRAY, resolution, resolution, kProbeSlices, kProbeMips, kHdrFormat});
    for (uint32_t mip = 0; mip < kProbeMips; ++mip) reflections.probeMips[mip] = litgpu::mipView(reflections.probeCubes, Diligent::TEXTURE_VIEW_UNORDERED_ACCESS, mip, Diligent::RESOURCE_DIM_TEX_2D_ARRAY, 0, kProbeSlices);
    reflections.bindingVersion = 0;
    reflections.compositeSrb[0].Release();
    reflections.compositeSrb[1].Release();
    reflections.bake = ProbeBakeState{};
}

void ensureCompositeBindings(Lighting::Data& state, Diligent::IBuffer* scene) {
    ReflectionGpu& reflections = state.reflections;
    if (reflections.bindingVersion == state.bindingVersion && reflections.boundDepth == state.shadeDepth && reflections.compositeSrb[0]) return;
    reflections.bindingVersion = state.bindingVersion;
    reflections.boundDepth = state.shadeDepth;
    Diligent::ITextureView* depthView = litgpu::srv(state.shadeDepth);
    createProbeBindings(state, scene);
    for (int parity = 0; parity < 2; ++parity) {
        reflections.compositeSrb[parity] = litgpu::makeBinding(reflections.compositePso,
                                                               {{"SceneData", scene},
                                                                {"ReflectionData", reflections.constants},
                                                                {"u_depth", depthView},
                                                                {"u_gbuffer0", litgpu::srv(state.gbuffer[0])},
                                                                {"u_gbuffer1", litgpu::srv(state.gbuffer[1])},
                                                                {"u_gbuffer2", litgpu::srv(state.gbuffer[2])},
                                                                {"u_ao", litgpu::srv(state.aoFiltered)},
                                                                {"u_ssr", litgpu::srv(state.ssr)},
                                                                {"u_dfgLut", litgpu::srv(state.dfg)},
                                                                {"u_prefilteredEnv", litgpu::srv(state.envPrefiltered)},
                                                                {"u_probes", litgpu::srv(reflections.probeCubes)},
                                                                {"u_voxels", litgpu::srv(reflections.voxels)},
                                                                {"u_skyVolume", litgpu::srv(reflections.skyVolume)},
                                                                {"u_planarDepth", litgpu::srv(reflections.planarDepth)},
                                                                {"u_planarColor", litgpu::srv(reflections.planarColor)},
                                                                {"u_hdr", litgpu::uav(state.hdrTargets[parity])},
                                                                {"MaterialBuffer", state.materials},
                                                                {"ExposureBuffer", state.exposure}},
                                                               false);
    }
    const std::initializer_list<litgpu::Named> planarResources = {{"SceneData", scene}, {"ReflectionData", reflections.constants}, {"u_depth", depthView}, {"u_sceneColor", litgpu::srv(state.sceneColor)}, {"u_planarDepth", litgpu::uav(reflections.planarDepth)}, {"u_planarColor", litgpu::uav(reflections.planarColor)}};
    reflections.planarClearSrb = litgpu::makeBinding(reflections.planarClearPso, planarResources, false);
    reflections.planarDepthSrb = litgpu::makeBinding(reflections.planarDepthPso, planarResources, false);
    reflections.planarColorSrb = litgpu::makeBinding(reflections.planarColorPso, planarResources, false);
}

void dispatchPlanar(Lighting::Data& state) {
    ReflectionGpu& reflections = state.reflections;
    const uint32_t groupsX = groupCount(static_cast<uint32_t>(reflections.planarWidth));
    const uint32_t groupsY = groupCount(static_cast<uint32_t>(reflections.planarHeight));
    litgpu::dispatch(state.context, reflections.planarClearPso, reflections.planarClearSrb, groupsX, groupsY);
    litgpu::uavBarrier(state.context, reflections.planarDepth);
    litgpu::dispatch(state.context, reflections.planarDepthPso, reflections.planarDepthSrb, groupsX, groupsY);
    litgpu::uavBarrier(state.context, reflections.planarDepth);
    litgpu::dispatch(state.context, reflections.planarColorPso, reflections.planarColorSrb, groupsX, groupsY);
}

}

void Lighting::setReflectionScene(const ReflectionScene& scene) {
    if (!m_data) m_data = new Data();
    ReflectionGpu& reflections = m_data->reflections;
    reflections.scene = scene;
}

void Lighting::bakeProbes() {
    if (!m_data) m_data = new Data();
    m_data->reflections.bake.requested = true;
}

namespace lighting_detail {

void createReflectionResources(Lighting::Data& state) {
    using litgpu::TextureSpec;
    if (!state.device) return;
    ReflectionGpu& reflections = state.reflections;
    reflections.constants = litgpu::uniformBuffer(state.device, "Reflection Data", sizeof(ReflectionConstants));
    const auto resolution = static_cast<uint32_t>(kReflectionVoxelResolution);
    reflections.voxels = litgpu::texture(state.device, TextureSpec{"Reflection Voxels", Diligent::RESOURCE_DIM_TEX_3D, resolution, resolution, resolution, 1, Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::BIND_SHADER_RESOURCE});
    reflections.skyVolume = litgpu::texture(state.device, TextureSpec{"Sky Visibility Volume", Diligent::RESOURCE_DIM_TEX_3D, resolution, resolution, resolution, 1, Diligent::TEX_FORMAT_R8_UNORM, Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS});
    createProbeTexture(state, reflections.probeResolution);
    createPlanarTextures(state, kPlaceholderSize, kPlaceholderSize);
    reflections.gridKey = 0;
    reflections.gridDirty = false;
}

void loadReflectionPipelines(Lighting::Data& state) {
    if (!state.device) return;
    ReflectionGpu& reflections = state.reflections;
    Diligent::IRenderDevice* device = state.device;
    reflections.compositePso = litgpu::computePipeline(device, "resources/shaders/lighting/reflection_composite.comp",
                                                       {pointSampler("u_depth"), pointSampler("u_gbuffer0"), pointSampler("u_gbuffer1"), pointSampler("u_gbuffer2"), linearSampler("u_ao"), linearSampler("u_ssr"), linearSampler("u_dfgLut"), linearSampler("u_prefilteredEnv"), linearSampler("u_probes"),
                                                        pointSampler("u_planarDepth"), pointSampler("u_planarColor"), linearSampler("u_voxels"), linearSampler("u_skyVolume")});
    reflections.probeBakePso = litgpu::computePipeline(device, "resources/shaders/lighting/probe_bake.comp", {linearSampler("u_envCapture"), pointSampler("u_voxels")});
    const std::string planar = "resources/shaders/lighting/planar.comp";
    reflections.skyVolumePso = litgpu::computePipeline(device, "resources/shaders/lighting/sky_volume.comp", {pointSampler("u_voxels")});
    reflections.skyVolumeDirty = true;
    reflections.planarClearPso = litgpu::computePipeline(device, planar, {pointSampler("u_depth"), linearSampler("u_sceneColor")}, {{"PASS_CLEAR", "1"}});
    reflections.planarDepthPso = litgpu::computePipeline(device, planar, {pointSampler("u_depth"), linearSampler("u_sceneColor")}, {{"PASS_DEPTH", "1"}});
    reflections.planarColorPso = litgpu::computePipeline(device, planar, {pointSampler("u_depth"), linearSampler("u_sceneColor")}, {{"PASS_COLOR", "1"}});
    reflections.bindingVersion = 0;
    reflections.bake = ProbeBakeState{};
    reflections.compositeSrb[0].Release();
    reflections.compositeSrb[1].Release();
}

void updateReflectionAssets(Lighting::Data& state, Diligent::IBuffer* scene) {
    ReflectionGpu& reflections = state.reflections;
    const ReflectionPlan plan = reflectionPlan(state.settings);
    if (!state.device || !plan.enabled() || !reflections.constants || !reflections.compositePso) return;
    const bool wantProbes = plan.has(kReflectionModeProbes) && reflections.probeBakePso;
    const bool wantVoxels = plan.has(kReflectionModeProbes) || plan.has(kReflectionModeWorld);
    const uint64_t boxKey = hashReflectionBoxes(reflections.scene.boxes);
    if (wantVoxels && boxKey != reflections.gridKey) {
        reflections.grid = buildReflectionVoxels(reflections.scene.boxes, kReflectionVoxelResolution);
        reflections.gridKey = boxKey;
        reflections.gridDirty = true;
    }
    if (wantVoxels && reflections.gridDirty) uploadVoxels(state);
    reflections.planeIndex = plan.has(kReflectionModePlanar) ? selectPlanarPlane(reflections.scene.planes, reflections.cameraPosition) : -1;
    if (reflections.planeIndex >= 0) {
        const auto divisor = static_cast<uint32_t>(std::max(plan.tier.planarDivisor, 1));
        const uint32_t width = std::max(static_cast<uint32_t>(state.width) / divisor, 1u);
        const uint32_t height = std::max(static_cast<uint32_t>(state.height) / divisor, 1u);
        if (reflections.planarWidth != static_cast<int>(width) || reflections.planarHeight != static_cast<int>(height)) createPlanarTextures(state, width, height);
    }
    const int probeCount = wantProbes ? static_cast<int>(std::min<size_t>(reflections.scene.probes.size(), kMaxReflectionProbes)) : 0;
    ReflectionConstants constants = buildConstants(state, plan, probeCount);
    state.context->UpdateBuffer(reflections.constants, 0, sizeof(constants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (reflections.skyVolumeDirty) buildSkyVolume(state, scene);
    const uint32_t wantedResolution = sceneProbeResolution(reflections.scene.probes);
    if (probeCount > 0 && wantedResolution != reflections.probeResolution) createProbeTexture(state, wantedResolution);
    ensureCompositeBindings(state, scene);
    if (probeBakeNeeded(reflections.bake, probeCount, reflections.probeResolution)) runProbeBake(state, probeCount);
}

void runReflectionComposite(Lighting::Data& state, Diligent::IBuffer* scene, uint32_t groupsX, uint32_t groupsY) {
    ReflectionGpu& reflections = state.reflections;
    if (!reflections.compositePso || !reflections.constants) return;
    ensureCompositeBindings(state, scene);
    if (reflections.planeIndex >= 0 && reflections.planarWidth > static_cast<int>(kPlaceholderSize)) dispatchPlanar(state);
    litgpu::dispatch(state.context, reflections.compositePso, reflections.compositeSrb[state.hdrIndex], groupsX, groupsY);
}

}
