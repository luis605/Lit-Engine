module;

#include "GpuHelpers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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

constexpr int kTransmittanceSteps = 48;
constexpr float kOzoneCenterHeight = 25.0f;
constexpr float kOzoneHalfWidth = 15.0f;
constexpr float kRayleighScaleHeight = 8.0f;
constexpr float kMieScaleHeight = 1.2f;
constexpr float kMoonSunThreshold = -0.16f;
constexpr float kMoonDiskRadius = 0.0075f;
constexpr float kMaxCloudOffset = 1.26e6f;
constexpr float kCloudTimeWrap = 10000.0f;
constexpr float kCloudEnvironmentRefreshTravel = 7.5f;
constexpr float kCloudTravelPerSpeedSecond = 2.5f;
constexpr uint32_t kTemporalFrameWrap = 4096u;
constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr float kSkyAltitudeQuantum = 50.0f;
constexpr float kMinDimension = 1.0f;
constexpr float kMinClipDistance = 1e-4f;

struct SunState {
    glm::vec3 illuminance;
    glm::vec3 trueSun;
    glm::vec3 moon;
    bool moonLit;
};

glm::vec3 sunFromTime(float hours, float azimuthDegrees) {
    const float dayPhase = (hours - 6.0f) / 12.0f * kPi;
    const float elevation = std::sin(dayPhase) * glm::radians(62.0f);
    const float azimuth = glm::radians(azimuthDegrees) + (hours - 12.0f) / 12.0f * kPi * 0.5f;
    return glm::normalize(glm::vec3(std::cos(elevation) * std::sin(azimuth), std::sin(elevation), std::cos(elevation) * std::cos(azimuth)));
}

SunState resolveSun(Lighting::Data& state) {
    const RenderSettings& settings = state.settings;
    SunState sun{};
    sun.moon = sunFromTime(std::fmod(settings.timeOfDay + 13.2f, 24.0f), settings.sunAzimuth + 25.0f);
    const bool followsLight = state.hasDirectional && settings.sunFollowsLight;
    if (followsLight) {
        state.sun = state.directionalDirection;
        sun.illuminance = state.directionalIlluminance;
    } else {
        state.sun = sunFromTime(settings.timeOfDay, settings.sunAzimuth);
        sun.illuminance = glm::vec3(settings.sunIlluminance);
    }
    sun.trueSun = state.sun;
    const bool physical = settings.sky == SkyMode::Physical;
    state.nightDarkening = physical ? 3.0f * std::clamp((-sun.trueSun.y - 0.02f) / 0.2f, 0.0f, 1.0f) : 0.0f;
    if (settings.moon && physical && !followsLight && state.sun.y < kMoonSunThreshold && sun.moon.y > 0.0f) {
        sun.moonLit = true;
        state.sun = sun.moon;
        sun.illuminance = glm::vec3(0.82f, 0.9f, 1.0f) * settings.moonIlluminance * std::clamp((-sun.trueSun.y - 0.16f) / 0.14f, 0.0f, 1.0f);
    }
    return sun;
}

void advanceTimeOfDay(Lighting::Data& state, float deltaTime) {
    RenderSettings& settings = state.settings;
    if (settings.animateTime) {
        settings.timeOfDay = std::fmod(settings.timeOfDay + deltaTime * settings.timeSpeed, 24.0f);
        if (settings.timeOfDay < 0.0f) settings.timeOfDay += 24.0f;
    }
}

void advanceClouds(Lighting::Data& state, float deltaTime) {
    const RenderSettings& settings = state.settings;
    state.cloudTime += deltaTime;
    const glm::vec2 travel = glm::vec2(1.0f, 0.35f) * (settings.cloudSpeed * kCloudTravelPerSpeedSecond * deltaTime);
    state.cloudOffset += travel;
    if (settings.clouds) state.cloudTravelSinceCapture += glm::length(travel);
    state.cloudOffset = glm::vec2(std::fmod(state.cloudOffset.x, kMaxCloudOffset), std::fmod(state.cloudOffset.y, kMaxCloudOffset));
}

void fillCameraUniforms(Lighting::Data& state, const FrameInput& input, SceneUniforms& uniforms) {
    const glm::mat4 viewProjection = input.projection * input.view;
    const float width = std::max(static_cast<float>(input.width), kMinDimension);
    const float height = std::max(static_cast<float>(input.height), kMinDimension);
    uniforms.projection = input.projection;
    uniforms.view = input.view;
    uniforms.viewProjection = viewProjection;
    uniforms.invView = glm::inverse(input.view);
    uniforms.invProjection = glm::inverse(input.projection);
    uniforms.invViewProjection = glm::inverse(viewProjection);
    uniforms.prevViewProjection = viewProjection;
    state.reflections.cameraPosition = input.cameraPosition;
    extractPlanes(viewProjection, uniforms.frustumPlanes);
    uniforms.viewPos = input.cameraPosition;
    uniforms.nearPlane = input.nearPlane;
    uniforms.screenParams = glm::vec4(width, height, 1.0f / width, 1.0f / height);
    uniforms.timeParams = glm::vec4(input.time, input.deltaTime, static_cast<float>(input.frame % kTemporalFrameWrap), input.farPlane);
    const float depthRatio = input.farPlane / std::max(input.nearPlane, kMinClipDistance);
    uniforms.lightParams = glm::vec4(static_cast<float>(state.lightCount), 16.0f, 9.0f, 24.0f);
    uniforms.clusterParams = glm::vec4(24.0f / std::log(std::max(depthRatio, 1.0001f)), 0.0f, width / 16.0f, height / 9.0f);
}

void fillSunUniforms(const Lighting::Data& state, const SunState& sun, const glm::vec3& transmittance, SceneUniforms& uniforms) {
    const RenderSettings& settings = state.settings;
    const float sunUp = glm::length(transmittance) > 1e-6f ? 1.0f : 0.0f;
    uniforms.sunDirection = glm::vec4(state.sun, glm::radians(settings.sunAngularDiameter) * 0.5f);
    uniforms.sunIlluminance = glm::vec4(sun.illuminance * transmittance, sunUp);
    uniforms.exposureParams = glm::vec4(settings.skyBrightness, 0.0f, settings.environmentIntensity, static_cast<float>(static_cast<int>(settings.debugView)));
    uniforms.moonDirection = glm::vec4(sun.moon, kMoonDiskRadius);
    uniforms.celestialParams = glm::vec4(sun.trueSun, sun.moonLit ? 1.0f : 0.0f);
}

void fillSkyUniforms(const Lighting::Data& state, const SunState& sun, SceneUniforms& uniforms) {
    const RenderSettings& settings = state.settings;
    const float skyIntensity = settings.sky == SkyMode::Gradient ? settings.gradientIntensity : settings.hdriIntensity;
    const SkyMode effectiveSky = settings.sky == SkyMode::Hdri && state.hdriLoaded == 0 ? SkyMode::Gradient : settings.sky;
    const glm::vec3& illuminance = sun.illuminance;
    uniforms.atmosphereParams = glm::vec4(settings.rayleighScale, settings.mieScale, settings.ozoneScale, settings.mieAnisotropy);
    uniforms.skyParams = glm::vec4(static_cast<float>(static_cast<int>(effectiveSky)), skyIntensity, settings.hdriRotation, settings.clouds ? settings.cloudCoverage : 0.0f);
    uniforms.skyParams2 = glm::vec4(settings.cloudSpeed, settings.stars ? 1.0f : 0.0f, settings.cloudDensity, settings.groundAlbedo);
    uniforms.skyGradient[0] = glm::vec4(settings.gradientZenith, settings.cloudAltitude);
    uniforms.skyGradient[1] = glm::vec4(settings.gradientHorizon, settings.environmentIntensity);
    uniforms.skyGradient[2] = glm::vec4(settings.gradientGround, glm::length(illuminance) > 0.0f ? (illuminance.x + illuminance.y + illuminance.z) / 3.0f : 0.0f);
    uniforms.cloudParams = glm::vec4(state.cloudOffset, std::fmod(state.cloudTime, kCloudTimeWrap), settings.cloudSpeed);
    uniforms.skyParams3 = glm::vec4(settings.moonIlluminance, settings.stars ? 1.0f : 0.0f, settings.godRays ? settings.godRayIntensity : 0.0f, settings.moon && settings.sky == SkyMode::Physical ? 1.0f : 0.0f);
}

void fillRenderUniforms(const Lighting::Data& state, SceneUniforms& uniforms) {
    const RenderSettings& settings = state.settings;
    uniforms.renderFlags = glm::vec4(settings.ssao ? 1.0f : 0.0f, reflectionPlan(settings).enabled() ? 1.0f : 0.0f, settings.multiScatter ? 1.0f : 0.0f, settings.specularAntiAliasing ? 1.0f : 0.0f);
    uniforms.aoParams = glm::vec4(settings.aoRadius, settings.aoIntensity, static_cast<float>(std::clamp(settings.aoSlices, 1, 8)), static_cast<float>(std::clamp(settings.aoSteps, 2, 16)));
    const ReflectionTier tier = reflectionPlan(settings).tier;
    uniforms.ssrParams = glm::vec4(settings.ssrMaxRoughness * tier.roughnessScale, settings.ssrThickness, static_cast<float>(std::min(std::clamp(settings.ssrSteps, 8, 128), tier.ssrStepCap)), settings.fxaa ? 1.0f : 0.0f);
    uniforms.fogParams = glm::vec4(settings.fog ? settings.fogDensity : 0.0f, settings.fogHeightFalloff, settings.fogHeight, settings.fog ? settings.aerialDistanceScale : 0.0f);
    uniforms.postParams = glm::vec4(settings.bloom ? settings.bloomIntensity : 0.0f, settings.vignette, settings.grain, static_cast<float>(static_cast<int>(settings.tonemapper)));
    uniforms.postParams2 = glm::vec4(settings.contrast, settings.saturation, settings.temperature, settings.tint);
    uniforms.maskParams = glm::vec4(settings.maskContrast, settings.maskSheen, settings.maskSheenSpeed, settings.maskSheenWidth);
    uniforms.maskColor = glm::vec4(settings.maskSheenColor, glm::radians(settings.maskSheenAngle));
    uniforms.vignetteParams = glm::vec4(settings.vignetteWidth, settings.vignetteTightness, 0.0f, 0.0f);
    uniforms.giParams = glm::vec4(settings.ssao && settings.ssgi ? settings.ssgiIntensity : 0.0f, settings.ssgiThickness, state.pyramidValid ? 1.0f : 0.0f, static_cast<float>(kPyramidMips));
    uniforms.bleedParams = colorBleedUniform(settings);
    uniforms.qualityParams = glm::vec4(settings.shadowCascadeBlend, settings.contactShadows ? settings.shadowContactLength : 0.0f, settings.areaLights ? 1.0f : 0.0f, settings.cloudAerialStrength);
    uniforms.qualityParams2 = glm::vec4(static_cast<float>(settings.aoDenoise), settings.bentNormals ? 1.0f : 0.0f, settings.upscaleSharpness, settings.renderScale);
}

void updateSkyHash(Lighting::Data& state, const FrameInput& input, const SunState& sun) {
    const RenderSettings& settings = state.settings;
    uint64_t hash = kFnvOffsetBasis;
    auto mix = [&hash](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        hash = (hash ^ bits) * kFnvPrime;
    };
    for (float value : {state.sun.x, state.sun.y, state.sun.z, settings.rayleighScale, settings.mieScale, settings.ozoneScale, settings.mieAnisotropy, settings.groundAlbedo, settings.cloudCoverage, settings.cloudDensity, settings.cloudAltitude,
                        static_cast<float>(settings.clouds), static_cast<float>(static_cast<int>(settings.sky)), settings.hdriIntensity, settings.hdriRotation, settings.gradientIntensity, settings.gradientZenith.x, settings.gradientHorizon.x,
                        settings.gradientGround.x, sun.illuminance.x, std::round(input.cameraPosition.y / kSkyAltitudeQuantum), sun.moon.x, sun.moon.y, settings.moonIlluminance, static_cast<float>(settings.moon)}) {
        mix(value);
    }
    if (hash != state.skyHash) {
        state.skyHash = hash;
        state.envDirty = true;
    }
    if (state.cloudTravelSinceCapture > kCloudEnvironmentRefreshTravel) state.envDirty = true;
}

}

namespace lighting_detail {

glm::vec3 cpuTransmittance(float sunZenithCos, const RenderSettings& settings) {
    const float radius = kBottomRadius + kCameraAltitude;
    const glm::vec3 position(0.0f, radius, 0.0f);
    const glm::vec3 direction(std::sqrt(std::max(0.0f, 1.0f - sunZenithCos * sunZenithCos)), sunZenithCos, 0.0f);
    const float projected = glm::dot(position, direction);
    const float topOffset = glm::dot(position, position) - kTopRadius * kTopRadius;
    const float bottomOffset = glm::dot(position, position) - kBottomRadius * kBottomRadius;
    const float bottomDiscriminant = projected * projected - bottomOffset;
    if (bottomDiscriminant >= 0.0f && -projected - std::sqrt(bottomDiscriminant) > 0.0f) return glm::vec3(0.0f);
    const float maxDistance = -projected + std::sqrt(std::max(projected * projected - topOffset, 0.0f));
    const glm::vec3 rayleigh = glm::vec3(5.802f, 13.558f, 33.1f) * 1e-3f * settings.rayleighScale;
    const glm::vec3 ozone = glm::vec3(0.650f, 1.881f, 0.085f) * 1e-3f * settings.ozoneScale;
    const float mieExtinction = 4.440e-3f * settings.mieScale;
    glm::vec3 opticalDepth(0.0f);
    const float stepLength = maxDistance / kTransmittanceSteps;
    for (int step = 0; step < kTransmittanceSteps; ++step) {
        const glm::vec3 samplePoint = position + direction * ((static_cast<float>(step) + 0.5f) * stepLength);
        const float height = std::max(glm::length(samplePoint) - kBottomRadius, 0.0f);
        const float ozoneDensity = std::max(0.0f, 1.0f - std::abs(height - kOzoneCenterHeight) / kOzoneHalfWidth);
        opticalDepth += (rayleigh * std::exp(-height / kRayleighScaleHeight) + glm::vec3(mieExtinction * std::exp(-height / kMieScaleHeight)) + ozone * ozoneDensity) * stepLength;
    }
    return glm::vec3(std::exp(-opticalDepth.x), std::exp(-opticalDepth.y), std::exp(-opticalDepth.z));
}

}

void Lighting::beginFrame(const FrameInput& input, SceneUniforms& uniforms) {
    Data& state = *m_data;
    ++state.frame;
    advanceTimeOfDay(state, input.deltaTime);
    const SunState sun = resolveSun(state);
    advanceClouds(state, input.deltaTime);
    const RenderSettings& settings = state.settings;
    const glm::vec3 transmittance = settings.sky == SkyMode::Physical ? cpuTransmittance(state.sun.y, settings) : glm::vec3(state.sun.y > -0.02f ? 1.0f : 0.0f);
    fillCameraUniforms(state, input, uniforms);
    fillSunUniforms(state, sun, transmittance, uniforms);
    fitShadows(input, uniforms);
    fillSkyUniforms(state, sun, uniforms);
    fillRenderUniforms(state, uniforms);
    updateSkyHash(state, input, sun);
}
