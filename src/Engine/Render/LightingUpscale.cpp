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

namespace lighting_detail {

namespace {

uint32_t groupsFor(int size) { return (static_cast<uint32_t>(size) + kThreadGroupSize - 1) / kThreadGroupSize; }

}

bool upscaleActive(const Lighting::Data& state) { return state.outputWidth != state.width || state.outputHeight != state.height; }

bool upscaleReady(const Lighting::Data& state) { return state.upscaleFxaaSrb && state.easuSrb && state.rcasSrb && state.presentSrb; }

void createUpscaleBindings(Lighting::Data& state, Diligent::IBuffer* scene) {
    state.upscaleFxaaSrb.Release();
    state.easuSrb.Release();
    state.rcasSrb.Release();
    state.presentSrb.Release();
    if (!upscaleActive(state) || !state.upscaleSource || !state.upscaled[0] || !state.upscaled[1] || !state.upscaleFxaaPso || !state.easuPso || !state.rcasPso || !state.presentPso) return;
    state.upscaleFxaaSrb = litgpu::makeBinding(state.upscaleFxaaPso, {{"SceneData", scene}, {"u_ldr", litgpu::srv(state.ldr)}});
    state.presentSrb = litgpu::makeBinding(state.presentPso, {{"u_ldr", litgpu::srv(state.upscaled[1])}});
    state.easuSrb = litgpu::makeBinding(state.easuPso, {{"u_source", litgpu::srv(state.upscaleSource)}, {"u_output", litgpu::uav(state.upscaled[0])}});
    state.rcasSrb = litgpu::makeBinding(state.rcasPso, {{"SceneData", scene}, {"u_source", litgpu::srv(state.upscaled[0])}, {"u_output", litgpu::uav(state.upscaled[1])}});
}

void runUpscale(Lighting::Data& state) {
    litgpu::dispatch(state.context, state.easuPso, state.easuSrb, groupsFor(state.outputWidth), groupsFor(state.outputHeight));
    litgpu::dispatch(state.context, state.rcasPso, state.rcasSrb, groupsFor(state.outputWidth), groupsFor(state.outputHeight));
}

}
