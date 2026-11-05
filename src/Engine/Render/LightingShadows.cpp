module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"

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

constexpr float kCascadeSplitLambda = 0.82f;
constexpr float kCascadeRadiusQuantum = 16.0f;
constexpr float kCasterExtension = 1200.0f;
constexpr float kMinNearPlane = 1e-4f;
constexpr int kMinShadowResolution = 512;
constexpr int kMaxShadowResolution = 4096;
constexpr int kMinLocalShadowResolution = 128;
constexpr int kMaxLocalShadowResolution = 2048;
constexpr float kLocalShadowBorderTexels = 8.0f;
constexpr float kPreviousShadowBonus = 1.5f;
constexpr float kMinLightDistance = 0.5f;
constexpr float kMinSpotCosine = 0.0872f;
constexpr float kMaxSpotCosine = 0.9999f;
constexpr float kMaxSpotTanHalf = 11.4f;
constexpr float kSqrtTwo = 1.4142f;
constexpr int kCubeFaces = 6;

glm::mat4 orthographicZeroToOne(float halfWidth, float nearZ, float farZ) {
    glm::mat4 matrix(1.0f);
    matrix[0][0] = 1.0f / halfWidth;
    matrix[1][1] = 1.0f / halfWidth;
    matrix[2][2] = -1.0f / (farZ - nearZ);
    matrix[3][2] = -nearZ / (farZ - nearZ);
    return matrix;
}

bool sphereInFrustum(const glm::vec4* planes, const glm::vec3& center, float radius) {
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) {
        if (glm::dot(glm::vec3(planes[planeIndex]), center) + planes[planeIndex].w < -radius) return false;
    }
    return true;
}

bool pyramidInFrustum(const glm::vec4* planes, const glm::vec3& apex, const glm::vec3& forward, const glm::vec3& right, const glm::vec3& up, float tanHalf, float range) {
    const glm::vec3 points[5] = {apex, apex + range * (forward + tanHalf * (right + up)), apex + range * (forward + tanHalf * (right - up)), apex + range * (forward - tanHalf * (right + up)), apex + range * (forward - tanHalf * (right - up))};
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) {
        const bool outside = std::none_of(std::begin(points), std::end(points), [&](const glm::vec3& point) { return glm::dot(glm::vec3(planes[planeIndex]), point) + planes[planeIndex].w >= 0.0f; });
        if (outside) return false;
    }
    return true;
}

float cascadeSplitDistance(float nearZ, float farZ, float fraction) {
    return kCascadeSplitLambda * nearZ * std::pow(farZ / nearZ, fraction) + (1.0f - kCascadeSplitLambda) * (nearZ + (farZ - nearZ) * fraction);
}

float sliceRadius(const FrameInput& input, float nearDistance, float farDistance, float centerDistance) {
    const float tanY = std::tan(glm::radians(input.fovY) * 0.5f);
    const float tanX = tanY * input.aspect;
    const bool orthographic = input.projection[3][3] > 0.5f;
    const glm::vec3 centerView(0.0f, 0.0f, -centerDistance);
    float radius = 0.0f;
    for (float distance : {nearDistance, farDistance}) {
        for (float signX : {-1.0f, 1.0f}) {
            for (float signY : {-1.0f, 1.0f}) {
                const float cornerX = signX * (orthographic ? 1.0f / input.projection[0][0] : tanX * distance);
                const float cornerY = signY * (orthographic ? 1.0f / input.projection[1][1] : tanY * distance);
                radius = std::max(radius, glm::length(glm::vec3(cornerX, cornerY, -distance) - centerView));
            }
        }
    }
    return std::max(std::ceil(radius * kCascadeRadiusQuantum) / kCascadeRadiusQuantum, 1.0f / kCascadeRadiusQuantum);
}

void fitCascade(Lighting::Data& state, int cascadeIndex, int cascadeCount, const FrameInput& input, const glm::mat4& invView, const glm::vec3& toSun) {
    Cascade& cascade = state.cascades[cascadeIndex];
    const float nearZ = std::max(input.nearPlane, kMinNearPlane);
    const float farZ = std::min(state.settings.shadowDistance, input.farPlane);
    const float divisor = static_cast<float>(std::max(cascadeCount, 1));
    const float nearDistance = cascadeIndex == 0 ? nearZ : cascadeSplitDistance(nearZ, farZ, static_cast<float>(cascadeIndex) / divisor);
    const float farDistance = cascadeSplitDistance(nearZ, farZ, static_cast<float>(cascadeIndex + 1) / divisor);
    cascade.split = farDistance;
    cascade.due = cascadeIndex < cascadeCount;
    if (cascadeIndex >= cascadeCount) {
        cascade.valid = false;
        return;
    }
    const float centerDistance = 0.5f * (nearDistance + farDistance);
    const float radius = sliceRadius(input, nearDistance, farDistance, centerDistance);
    glm::vec3 center = glm::vec3(invView * glm::vec4(0.0f, 0.0f, -centerDistance, 1.0f));
    const glm::vec3 up = std::abs(toSun.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const float texelWorld = 2.0f * radius / static_cast<float>(state.shadowResolution);
    glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -toSun, up);
    glm::vec3 lightSpace = glm::vec3(lightView * glm::vec4(center, 1.0f));
    lightSpace.x = std::floor(lightSpace.x / texelWorld) * texelWorld;
    lightSpace.y = std::floor(lightSpace.y / texelWorld) * texelWorld;
    lightSpace.z = std::floor(lightSpace.z / texelWorld) * texelWorld;
    center = glm::vec3(glm::inverse(lightView) * glm::vec4(lightSpace, 1.0f));
    const glm::vec3 eye = center + toSun * (radius + kCasterExtension);
    lightView = glm::lookAt(eye, center, up);
    const float depthRange = 2.0f * radius + kCasterExtension;
    cascade.viewProjection = orthographicZeroToOne(radius, 0.0f, depthRange) * lightView;
    cascade.renderViewProjection = cascade.viewProjection;
    lighting_detail::extractPlanes(cascade.renderViewProjection, cascade.planes);
    cascade.texelsPerUnit = static_cast<float>(state.shadowResolution) / (2.0f * radius);
    cascade.texelWorld = texelWorld;
    cascade.depthRange = depthRange;
    cascade.layer = cascade.renderLayer = cascadeIndex;
    cascade.viewport[0] = 0;
    cascade.viewport[1] = 0;
    cascade.viewport[2] = state.shadowResolution;
    cascade.viewport[3] = state.shadowResolution;
    cascade.clear = true;
    cascade.valid = true;
}

struct ShadowCandidate {
    uint32_t index;
    float score;
};

std::vector<ShadowCandidate> rankShadowCandidates(Lighting::Data& state, const SceneUniforms& uniforms, const glm::vec3& cameraPosition, int maxLights) {
    std::vector<ShadowCandidate> candidates;
    candidates.reserve(state.cpuLights.size());
    for (uint32_t lightIndex = 0; lightIndex < state.cpuLights.size(); ++lightIndex) {
        GpuLight& light = state.cpuLights[lightIndex];
        light.params.w = -1.0f;
        if (maxLights == 0) continue;
        const glm::vec3 position(light.positionRange);
        const float range = light.positionRange.w;
        if (range <= 0.0f || !sphereInFrustum(uniforms.frustumPlanes, position, range)) continue;
        const float luminance = glm::dot(glm::vec3(light.colorType), glm::vec3(0.2126f, 0.7152f, 0.0722f));
        if (luminance <= 0.0f) continue;
        const float distance = std::max(glm::length(position - cameraPosition) - range * 0.25f, kMinLightDistance);
        const bool previouslyShadowed = std::find(state.shadowedLights.begin(), state.shadowedLights.end(), lightIndex) != state.shadowedLights.end();
        candidates.push_back({lightIndex, luminance / (distance * distance) * (previouslyShadowed ? kPreviousShadowBonus : 1.0f)});
    }
    std::sort(candidates.begin(), candidates.end(), [](const ShadowCandidate& first, const ShadowCandidate& second) { return first.score > second.score; });
    return candidates;
}

struct LocalViewContext {
    Lighting::Data& state;
    SceneUniforms& uniforms;
    LocalShadowCaster& caster;
    glm::vec3 position;
    float range;
    float nearZ;
    float resolution;
    int faceBase;
};

void addLocalView(const LocalViewContext& context, int face, const glm::vec3& forward, const glm::vec3& upHint, float tanHalf) {
    LocalShadowView& view = context.state.localViews[static_cast<size_t>(context.faceBase + face)];
    const glm::mat4 lightView = glm::lookAt(context.position, context.position + forward, upHint);
    const glm::mat4 projection = glm::perspectiveRH_ZO(2.0f * std::atan(tanHalf), 1.0f, context.nearZ, context.range);
    view.viewProjection = projection * lightView;
    view.origin = context.position;
    view.texelsAtUnitDistance = context.resolution / (2.0f * tanHalf);
    view.projectionScale = 1.0f / tanHalf;
    const glm::vec3 right = glm::normalize(glm::cross(forward, upHint));
    const glm::vec3 up = glm::cross(right, forward);
    view.active = pyramidInFrustum(context.uniforms.frustumPlanes, context.position, forward, right, up, tanHalf, context.range);
    if (view.active) context.caster.faceMask |= 1u << face;
    context.uniforms.localShadowMatrices[context.faceBase + face] = view.viewProjection;
}

void addSpotViews(const LocalViewContext& context, const GpuLight& light, float margin) {
    const glm::vec3 direction = glm::normalize(glm::vec3(light.directionCos));
    const float cosOuter = std::clamp(light.directionCos.w, kMinSpotCosine, kMaxSpotCosine);
    const float tanHalf = std::min(std::sqrt(1.0f - cosOuter * cosOuter) / cosOuter, kMaxSpotTanHalf) * margin;
    const glm::vec3 upHint = std::abs(direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    addLocalView(context, 0, direction, upHint, tanHalf);
    const float cosWide = std::cos(std::atan(tanHalf * kSqrtTwo));
    context.caster.cone = glm::vec4(direction, cosWide);
}

void addPointViews(const LocalViewContext& context, float margin) {
    const glm::vec3 axes[kCubeFaces] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const glm::vec3 ups[kCubeFaces] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    for (int face = 0; face < kCubeFaces; ++face) addLocalView(context, face, axes[face], ups[face], margin);
    context.caster.cone = glm::vec4(0.0f, 0.0f, 0.0f, margin);
}

}

namespace lighting_detail {

void extractPlanes(const glm::mat4& viewProjection, glm::vec4* planes) {
    const glm::mat4& vp = viewProjection;
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    planes[4] = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) planes[planeIndex] /= std::max(glm::length(glm::vec3(planes[planeIndex])), 1e-8f);
}

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

}

void Lighting::selectLocalShadows(SceneUniforms& uniforms, const glm::vec3& cameraPosition) {
    Data& state = *m_data;
    const RenderSettings& settings = state.settings;
    const int resolution = std::clamp(settings.localShadowResolution, kMinLocalShadowResolution, kMaxLocalShadowResolution);
    const int maxLights = settings.localShadows ? std::clamp(settings.localShadowCount, 0, kMaxLocalShadowLights) : 0;
    if (maxLights > 0 && resolution != state.localShadowResolution) {
        createShadowArray(state.device, "Local Shadows", static_cast<uint32_t>(resolution), kMaxLocalShadowViews, state.localShadowMap, state.localShadowTargets);
        state.localShadowResolution = resolution;
        ++state.bindingVersion;
    }
    const float resolutionF = static_cast<float>(state.localShadowResolution);
    const float margin = resolutionF / std::max(resolutionF - kLocalShadowBorderTexels, 1.0f);
    state.localTanHalf = margin;
    const std::vector<ShadowCandidate> candidates = rankShadowCandidates(state, uniforms, cameraPosition, maxLights);
    state.shadowedLights.clear();
    state.localCasterCount = 0;
    for (LocalShadowView& view : state.localViews) view.active = false;
    int faces = 0;
    for (const ShadowCandidate& candidate : candidates) {
        if (state.localCasterCount >= maxLights) break;
        GpuLight& light = state.cpuLights[candidate.index];
        const bool spot = light.colorType.w > 1.5f;
        const int needed = spot ? 1 : kCubeFaces;
        if (faces + needed > kMaxLocalShadowViews) continue;
        const float range = light.positionRange.w;
        LocalShadowCaster& caster = state.localCasters[static_cast<size_t>(state.localCasterCount)];
        caster.sphere = glm::vec4(glm::vec3(light.positionRange), range);
        caster.faceBase = static_cast<uint32_t>(faces);
        caster.spot = spot ? 1u : 0u;
        caster.faceMask = 0u;
        const LocalViewContext context{state, uniforms, caster, glm::vec3(light.positionRange), range, std::clamp(range * 0.004f, 0.02f, 0.5f), resolutionF, faces};
        if (spot) addSpotViews(context, light, margin);
        else addPointViews(context, margin);
        light.params.w = static_cast<float>(faces);
        faces += needed;
        ++state.localCasterCount;
        state.shadowedLights.push_back(candidate.index);
    }
    uniforms.localShadowParams = glm::vec4(static_cast<float>(faces), 1.0f / resolutionF, settings.localShadowSoftness, settings.shadowNormalBias);
    uniforms.localShadowParams2 = glm::vec4(settings.shadowDepthBias, 0.0f, 0.0f, 0.0f);
    if (!state.cpuLights.empty()) state.context->UpdateBuffer(state.lights, 0, sizeof(GpuLight) * state.cpuLights.size(), state.cpuLights.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Lighting::fitShadows(const FrameInput& input, SceneUniforms& uniforms) {
    Data& state = *m_data;
    const RenderSettings& settings = state.settings;
    const int count = cascadeCount();
    const int resolution = std::clamp(settings.shadowResolution, kMinShadowResolution, kMaxShadowResolution);
    if (count > 0 && resolution != state.shadowResolution) {
        createShadowArray(state.device, "Shadow Cascades", static_cast<uint32_t>(resolution), kShadowLayers, state.shadowMap, state.shadowTargets);
        state.shadowResolution = resolution;
        for (Cascade& cascade : state.cascades) cascade.valid = false;
        ++state.bindingVersion;
    }
    const glm::vec3 toSun = glm::normalize(state.sun);
    for (int cascadeIndex = 0; cascadeIndex < kCascadeSlots; ++cascadeIndex) fitCascade(state, cascadeIndex, count, input, uniforms.invView, toSun);
    for (int cascadeIndex = 0; cascadeIndex < kCascadeSlots; ++cascadeIndex) {
        const Cascade& cascade = state.cascades[cascadeIndex];
        uniforms.cascadeViewProj[cascadeIndex] = cascade.viewProjection;
        uniforms.cascadeInfo[cascadeIndex] = glm::vec4(cascade.texelWorld, cascade.depthRange, static_cast<float>(cascade.layer), 0.0f);
    }
    uniforms.cascadeSplits = glm::vec4(state.cascades[0].split, state.cascades[1].split, state.cascades[2].split, state.cascades[3].split);
    selectLocalShadows(uniforms, input.cameraPosition);
    uniforms.shadowParams = glm::vec4(count > 0 ? 1.0f : 0.0f, 1.0f / static_cast<float>(std::max(state.shadowResolution, 1)), settings.shadowSoftness, settings.shadowNormalBias);
    uniforms.shadowParams2 = glm::vec4(static_cast<float>(count), settings.shadowDepthBias, 0.0f, settings.pcss ? 1.0f : 0.0f);
}
