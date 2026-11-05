module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

export module Engine.Render.settings;

import Engine.glm;

export enum class Tonemapper : int { AgX = 0, AgXPunchy = 1, ACES = 2, Neutral = 3, Reinhard = 4, Linear = 5 };
export enum class SkyMode : int { Physical = 0, Gradient = 1, Hdri = 2 };
export enum class ReflectionMode : int { Off = 0, SSR, SSRProbes, SSRPlanar, WorldSpace };
export enum class ReflectionQuality : int { Low = 0, Medium, High };
export inline constexpr int kReflectionModeCount = 5;
export inline constexpr int kReflectionQualityCount = 3;
export inline constexpr const char* kReflectionModeNames[kReflectionModeCount] = {"Off", "SSR", "SSR + Probes", "SSR + Planar", "World space"};
export inline constexpr const char* kReflectionQualityNames[kReflectionQualityCount] = {"Low", "Medium", "High"};
export enum class DebugView : int { Lit = 0, BaseColor, Normals, Roughness, Metallic, AmbientOcclusion, Shadows, Reflections, Lighting, Luminance, Clusters, Overdraw };

export struct RenderSettings {
    SkyMode sky = SkyMode::Physical;
    float timeOfDay = 15.5f;
    float sunAzimuth = 130.0f;
    bool animateTime = false;
    float timeSpeed = 0.1f;
    bool sunFollowsLight = true;
    float sunIlluminance = 110000.0f;
    float sunAngularDiameter = 0.545f;
    float rayleighScale = 1.0f;
    float mieScale = 1.0f;
    float ozoneScale = 1.0f;
    float mieAnisotropy = 0.8f;
    float groundAlbedo = 0.3f;
    float aerialDistanceScale = 3.0f;
    bool clouds = true;
    float cloudCoverage = 0.45f;
    float cloudDensity = 1.0f;
    float cloudSpeed = 12.0f;
    float cloudAltitude = 2200.0f;
    bool stars = true;
    bool moon = true;
    float moonIlluminance = 1.0f;
    bool godRays = true;
    float godRayIntensity = 1.0f;
    std::string hdriPath;
    float hdriIntensity = 20000.0f;
    float hdriRotation = 0.0f;
    glm::vec3 gradientZenith{0.20f, 0.36f, 0.72f};
    glm::vec3 gradientHorizon{0.78f, 0.84f, 0.92f};
    glm::vec3 gradientGround{0.28f, 0.26f, 0.24f};
    float gradientIntensity = 8000.0f;
    float environmentIntensity = 1.0f;
    float skyBrightness = 4.0f;

    bool autoExposure = true;
    float exposureCompensation = 0.0f;
    float manualEV100 = 14.0f;
    float adaptationUp = 3.0f;
    float adaptationDown = 1.2f;
    float minEV100 = -4.0f;
    float maxEV100 = 17.0f;
    Tonemapper tonemapper = Tonemapper::AgX;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float temperature = 6500.0f;
    float tint = 0.0f;
    float vignette = 0.2f;
    float vignetteWidth = 0.85f;
    float vignetteTightness = 0.7f;
    float maskContrast = 1.0f;
    float maskSheen = 0.0f;
    float maskSheenSpeed = 0.4f;
    float maskSheenWidth = 0.12f;
    float maskSheenAngle = 35.0f;
    glm::vec3 maskSheenColor{0.35f, 0.6f, 1.0f};
    float grain = 0.0f;

    bool bloom = true;
    float bloomIntensity = 0.035f;
    float bloomRadius = 0.85f;

    bool ssao = true;
    float aoRadius = 1.8f;
    float aoIntensity = 1.0f;
    int aoSlices = 2;
    int aoSteps = 6;
    bool ssgi = true;
    float ssgiIntensity = 2.5f;
    float ssgiThickness = 0.5f;
    bool colorBleed = true;
    float colorBleedIntensity = 1.0f;
    float colorBleedRadius = 2.5f;
    int colorBleedQuality = 1;

    ReflectionMode reflectionMode = ReflectionMode::SSRProbes;
    ReflectionQuality reflectionQuality = ReflectionQuality::Medium;
    float reflectionIntensity = 1.0f;
    float reflectionTraceDistance = 80.0f;
    float ssrMaxRoughness = 0.55f;
    float ssrThickness = 0.35f;
    int ssrSteps = 40;

    bool shadows = true;
    int shadowCascades = 4;
    float shadowDistance = 220.0f;
    int shadowResolution = 2048;
    float shadowSoftness = 1.0f;
    float shadowNormalBias = 1.5f;
    float shadowDepthBias = 0.6f;
    bool pcss = true;
    bool contactShadows = true;
    bool localShadows = true;
    int localShadowCount = 4;
    int localShadowResolution = 512;
    float localShadowSoftness = 1.0f;

    bool fog = true;
    float fogDensity = 0.0015f;
    float fogHeightFalloff = 0.05f;
    float fogHeight = 0.0f;

    float shadowCascadeBlend = 0.12f;
    float shadowContactLength = 0.6f;
    int aoDenoise = 2;
    bool bentNormals = true;
    bool areaLights = true;
    float cloudAerialStrength = 1.0f;

    float renderScale = 1.0f;
    float upscaleSharpness = 0.25f;
    bool qualityGovernor = false;
    float targetFrameMs = 16.6f;
    float governorMinScale = 0.6f;

    bool fxaa = true;
    bool specularAntiAliasing = true;
    bool multiScatter = true;
    DebugView debugView = DebugView::Lit;
    bool operator==(const RenderSettings&) const = default;
};

export template <typename Settings, typename Visitor>
void visitRenderSettings(Settings& settings, Visitor&& visit) {
    visit("sky", settings.sky);
    visit("timeOfDay", settings.timeOfDay);
    visit("sunAzimuth", settings.sunAzimuth);
    visit("animateTime", settings.animateTime);
    visit("timeSpeed", settings.timeSpeed);
    visit("sunFollowsLight", settings.sunFollowsLight);
    visit("sunIlluminance", settings.sunIlluminance);
    visit("sunAngularDiameter", settings.sunAngularDiameter);
    visit("rayleighScale", settings.rayleighScale);
    visit("mieScale", settings.mieScale);
    visit("ozoneScale", settings.ozoneScale);
    visit("mieAnisotropy", settings.mieAnisotropy);
    visit("groundAlbedo", settings.groundAlbedo);
    visit("aerialDistanceScale", settings.aerialDistanceScale);
    visit("clouds", settings.clouds);
    visit("cloudCoverage", settings.cloudCoverage);
    visit("cloudDensity", settings.cloudDensity);
    visit("cloudSpeed", settings.cloudSpeed);
    visit("cloudAltitude", settings.cloudAltitude);
    visit("stars", settings.stars);
    visit("moon", settings.moon);
    visit("moonIlluminance", settings.moonIlluminance);
    visit("godRays", settings.godRays);
    visit("godRayIntensity", settings.godRayIntensity);
    visit("hdriPath", settings.hdriPath);
    visit("hdriIntensity", settings.hdriIntensity);
    visit("hdriRotation", settings.hdriRotation);
    visit("gradientZenith", settings.gradientZenith);
    visit("gradientHorizon", settings.gradientHorizon);
    visit("gradientGround", settings.gradientGround);
    visit("gradientIntensity", settings.gradientIntensity);
    visit("environmentIntensity", settings.environmentIntensity);
    visit("skyBrightness", settings.skyBrightness);
    visit("autoExposure", settings.autoExposure);
    visit("exposureCompensation", settings.exposureCompensation);
    visit("manualEV100", settings.manualEV100);
    visit("adaptationUp", settings.adaptationUp);
    visit("adaptationDown", settings.adaptationDown);
    visit("minEV100", settings.minEV100);
    visit("maxEV100", settings.maxEV100);
    visit("tonemapper", settings.tonemapper);
    visit("contrast", settings.contrast);
    visit("saturation", settings.saturation);
    visit("temperature", settings.temperature);
    visit("tint", settings.tint);
    visit("vignette", settings.vignette);
    visit("vignetteWidth", settings.vignetteWidth);
    visit("vignetteTightness", settings.vignetteTightness);
    visit("maskContrast", settings.maskContrast);
    visit("maskSheen", settings.maskSheen);
    visit("maskSheenSpeed", settings.maskSheenSpeed);
    visit("maskSheenWidth", settings.maskSheenWidth);
    visit("maskSheenAngle", settings.maskSheenAngle);
    visit("maskSheenColor", settings.maskSheenColor);
    visit("grain", settings.grain);
    visit("bloom", settings.bloom);
    visit("bloomIntensity", settings.bloomIntensity);
    visit("bloomRadius", settings.bloomRadius);
    visit("ssao", settings.ssao);
    visit("aoRadius", settings.aoRadius);
    visit("aoIntensity", settings.aoIntensity);
    visit("aoSlices", settings.aoSlices);
    visit("aoSteps", settings.aoSteps);
    visit("ssgi", settings.ssgi);
    visit("ssgiIntensity", settings.ssgiIntensity);
    visit("ssgiThickness", settings.ssgiThickness);
    visit("colorBleed", settings.colorBleed);
    visit("colorBleedIntensity", settings.colorBleedIntensity);
    visit("colorBleedRadius", settings.colorBleedRadius);
    visit("colorBleedQuality", settings.colorBleedQuality);
    visit("reflectionMode", settings.reflectionMode);
    visit("reflectionQuality", settings.reflectionQuality);
    visit("reflectionIntensity", settings.reflectionIntensity);
    visit("reflectionTraceDistance", settings.reflectionTraceDistance);
    visit("ssrMaxRoughness", settings.ssrMaxRoughness);
    visit("ssrThickness", settings.ssrThickness);
    visit("ssrSteps", settings.ssrSteps);
    visit("shadows", settings.shadows);
    visit("shadowCascades", settings.shadowCascades);
    visit("shadowDistance", settings.shadowDistance);
    visit("shadowResolution", settings.shadowResolution);
    visit("shadowSoftness", settings.shadowSoftness);
    visit("shadowNormalBias", settings.shadowNormalBias);
    visit("shadowDepthBias", settings.shadowDepthBias);
    visit("pcss", settings.pcss);
    visit("contactShadows", settings.contactShadows);
    visit("localShadows", settings.localShadows);
    visit("localShadowCount", settings.localShadowCount);
    visit("localShadowResolution", settings.localShadowResolution);
    visit("localShadowSoftness", settings.localShadowSoftness);
    visit("fog", settings.fog);
    visit("fogDensity", settings.fogDensity);
    visit("fogHeightFalloff", settings.fogHeightFalloff);
    visit("fogHeight", settings.fogHeight);
    visit("shadowCascadeBlend", settings.shadowCascadeBlend);
    visit("shadowContactLength", settings.shadowContactLength);
    visit("aoDenoise", settings.aoDenoise);
    visit("bentNormals", settings.bentNormals);
    visit("areaLights", settings.areaLights);
    visit("cloudAerialStrength", settings.cloudAerialStrength);
    visit("renderScale", settings.renderScale);
    visit("upscaleSharpness", settings.upscaleSharpness);
    visit("qualityGovernor", settings.qualityGovernor);
    visit("targetFrameMs", settings.targetFrameMs);
    visit("governorMinScale", settings.governorMinScale);
    visit("fxaa", settings.fxaa);
    visit("specularAntiAliasing", settings.specularAntiAliasing);
    visit("multiScatter", settings.multiScatter);
    visit("debugView", settings.debugView);
}

export inline constexpr int kColorBleedQualityMax = 3;
export inline constexpr float kColorBleedRadiusMin = 0.1f;
export inline constexpr float kColorBleedRadiusMax = 12.0f;
export inline constexpr float kColorBleedIntensityMax = 4.0f;
inline constexpr int kColorBleedTapsByQuality[kColorBleedQualityMax + 1] = {4, 8, 14, 24};

export inline int colorBleedTapCount(int quality) {
    const int clamped = quality < 0 ? 0 : (quality > kColorBleedQualityMax ? kColorBleedQualityMax : quality);
    return kColorBleedTapsByQuality[clamped];
}

export inline bool colorBleedActive(const RenderSettings& settings) {
    return settings.ssao && settings.colorBleed && settings.colorBleedIntensity > 0.0f;
}

export inline glm::vec4 colorBleedUniform(const RenderSettings& settings) {
    if (!colorBleedActive(settings)) return glm::vec4(0.0f);
    const float intensity = settings.colorBleedIntensity > kColorBleedIntensityMax ? kColorBleedIntensityMax : settings.colorBleedIntensity;
    const float radius = settings.colorBleedRadius < kColorBleedRadiusMin ? kColorBleedRadiusMin : (settings.colorBleedRadius > kColorBleedRadiusMax ? kColorBleedRadiusMax : settings.colorBleedRadius);
    return glm::vec4(intensity, radius, static_cast<float>(colorBleedTapCount(settings.colorBleedQuality)), 0.0f);
}

namespace render_settings_detail {

inline bool parseFloat(std::string_view& text, float& out) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    if (error != std::errc{}) return false;
    text.remove_prefix(static_cast<size_t>(end - text.data()));
    return true;
}

template <typename T>
bool parseValue(std::string_view text, T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        value = std::string(text);
        return true;
    } else if constexpr (std::is_same_v<T, glm::vec3>) {
        glm::vec3 parsed;
        if (!parseFloat(text, parsed.x) || !parseFloat(text, parsed.y) || !parseFloat(text, parsed.z)) return false;
        value = parsed;
        return true;
    } else {
        float number = 0.0f;
        if (!parseFloat(text, number)) return false;
        if constexpr (std::is_same_v<T, bool>) {
            value = number > 0.5f;
        } else if constexpr (std::is_same_v<T, float>) {
            value = number;
        } else {
            value = static_cast<T>(static_cast<int>(number));
        }
        return true;
    }
}

template <typename T>
void writeValue(std::ostringstream& out, const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        out << value;
    } else if constexpr (std::is_same_v<T, glm::vec3>) {
        out << value.x << ' ' << value.y << ' ' << value.z;
    } else if constexpr (std::is_same_v<T, bool>) {
        out << (value ? 1 : 0);
    } else if constexpr (std::is_enum_v<T>) {
        out << static_cast<int>(value);
    } else {
        out << value;
    }
}

}

export inline void clampReflectionSettings(RenderSettings& settings) {
    settings.reflectionMode = static_cast<ReflectionMode>(std::clamp(static_cast<int>(settings.reflectionMode), 0, kReflectionModeCount - 1));
    settings.reflectionQuality = static_cast<ReflectionQuality>(std::clamp(static_cast<int>(settings.reflectionQuality), 0, kReflectionQualityCount - 1));
    settings.reflectionIntensity = std::clamp(settings.reflectionIntensity, 0.0f, 4.0f);
    settings.reflectionTraceDistance = std::clamp(settings.reflectionTraceDistance, 1.0f, 1000.0f);
}

export inline constexpr float kRenderScaleMin = 0.5f;
export inline constexpr float kRenderScaleMax = 1.0f;
export inline constexpr float kRenderScaleStep = 0.05f;
export inline constexpr float kTargetFrameMsMin = 4.0f;
export inline constexpr float kTargetFrameMsMax = 100.0f;
export inline constexpr int kAoDenoiseMax = 3;

export inline float quantizeRenderScale(float scale) {
    const float clamped = std::clamp(scale, kRenderScaleMin, kRenderScaleMax);
    return std::clamp(std::round(clamped / kRenderScaleStep) * kRenderScaleStep, kRenderScaleMin, kRenderScaleMax);
}

export inline void clampQualitySettings(RenderSettings& settings) {
    settings.renderScale = quantizeRenderScale(settings.renderScale);
    settings.upscaleSharpness = std::clamp(settings.upscaleSharpness, 0.0f, 1.0f);
    settings.targetFrameMs = std::clamp(settings.targetFrameMs, kTargetFrameMsMin, kTargetFrameMsMax);
    settings.governorMinScale = std::clamp(quantizeRenderScale(settings.governorMinScale), kRenderScaleMin, kRenderScaleMax);
    settings.shadowCascadeBlend = std::clamp(settings.shadowCascadeBlend, 0.0f, 0.5f);
    settings.shadowContactLength = std::clamp(settings.shadowContactLength, 0.0f, 4.0f);
    settings.aoDenoise = std::clamp(settings.aoDenoise, 0, kAoDenoiseMax);
    settings.cloudAerialStrength = std::clamp(settings.cloudAerialStrength, 0.0f, 2.0f);
}

export inline bool setRenderSetting(RenderSettings& settings, std::string_view key, std::string_view value) {
    if (key == "ssr") {
        float enabled = 0.0f;
        if (!render_settings_detail::parseFloat(value, enabled)) return false;
        settings.reflectionMode = enabled > 0.5f ? (settings.reflectionMode == ReflectionMode::Off ? ReflectionMode::SSR : settings.reflectionMode) : ReflectionMode::Off;
        return true;
    }
    bool found = false;
    visitRenderSettings(settings, [&](std::string_view name, auto& field) {
        if (!found && name == key) found = render_settings_detail::parseValue(value, field);
    });
    if (found) {
        clampReflectionSettings(settings);
        clampQualitySettings(settings);
    }
    return found;
}

export inline std::string renderSettingValue(const RenderSettings& settings, std::string_view key) {
    std::ostringstream out;
    visitRenderSettings(settings, [&](std::string_view name, const auto& field) {
        if (name == key) render_settings_detail::writeValue(out, field);
    });
    return out.str();
}

export inline void writeRenderSettings(const RenderSettings& settings, std::ostream& out, std::string_view prefix) {
    visitRenderSettings(settings, [&](std::string_view name, const auto& field) {
        std::ostringstream value;
        value.precision(9);
        render_settings_detail::writeValue(value, field);
        out << prefix << name << ' ' << value.str() << '\n';
    });
}
