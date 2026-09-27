module;

#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

export module Engine.Render.settings;

import Engine.glm;

export enum class Tonemapper : int { AgX = 0, AgXPunchy = 1, ACES = 2, Neutral = 3, Reinhard = 4, Linear = 5 };
export enum class SkyMode : int { Physical = 0, Gradient = 1, Hdri = 2 };
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
    std::string hdriPath;
    float hdriIntensity = 20000.0f;
    float hdriRotation = 0.0f;
    glm::vec3 gradientZenith{0.20f, 0.36f, 0.72f};
    glm::vec3 gradientHorizon{0.78f, 0.84f, 0.92f};
    glm::vec3 gradientGround{0.28f, 0.26f, 0.24f};
    float gradientIntensity = 8000.0f;
    float environmentIntensity = 1.0f;
    float skyBrightness = 1.0f;

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
    float grain = 0.0f;

    bool bloom = true;
    float bloomIntensity = 0.035f;
    float bloomRadius = 0.85f;

    bool ssao = true;
    float aoRadius = 1.2f;
    float aoIntensity = 1.0f;
    int aoSlices = 2;
    int aoSteps = 6;
    bool ssgi = true;
    float ssgiIntensity = 1.0f;
    float ssgiThickness = 0.5f;

    bool ssr = true;
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
    bool staggerCascades = true;
    bool localShadows = true;
    int localShadowCount = 4;
    int localShadowResolution = 512;
    float localShadowSoftness = 1.0f;

    bool fog = true;
    float fogDensity = 0.0015f;
    float fogHeightFalloff = 0.05f;
    float fogHeight = 0.0f;

    bool fxaa = true;
    bool specularAntiAliasing = true;
    bool multiScatter = true;
    DebugView debugView = DebugView::Lit;
    bool operator==(const RenderSettings&) const = default;
};

export template <typename Settings, typename Visitor>
void visitRenderSettings(Settings& s, Visitor&& visit) {
    visit("sky", s.sky);
    visit("timeOfDay", s.timeOfDay);
    visit("sunAzimuth", s.sunAzimuth);
    visit("animateTime", s.animateTime);
    visit("timeSpeed", s.timeSpeed);
    visit("sunFollowsLight", s.sunFollowsLight);
    visit("sunIlluminance", s.sunIlluminance);
    visit("sunAngularDiameter", s.sunAngularDiameter);
    visit("rayleighScale", s.rayleighScale);
    visit("mieScale", s.mieScale);
    visit("ozoneScale", s.ozoneScale);
    visit("mieAnisotropy", s.mieAnisotropy);
    visit("groundAlbedo", s.groundAlbedo);
    visit("aerialDistanceScale", s.aerialDistanceScale);
    visit("clouds", s.clouds);
    visit("cloudCoverage", s.cloudCoverage);
    visit("cloudDensity", s.cloudDensity);
    visit("cloudSpeed", s.cloudSpeed);
    visit("cloudAltitude", s.cloudAltitude);
    visit("stars", s.stars);
    visit("hdriPath", s.hdriPath);
    visit("hdriIntensity", s.hdriIntensity);
    visit("hdriRotation", s.hdriRotation);
    visit("gradientZenith", s.gradientZenith);
    visit("gradientHorizon", s.gradientHorizon);
    visit("gradientGround", s.gradientGround);
    visit("gradientIntensity", s.gradientIntensity);
    visit("environmentIntensity", s.environmentIntensity);
    visit("skyBrightness", s.skyBrightness);
    visit("autoExposure", s.autoExposure);
    visit("exposureCompensation", s.exposureCompensation);
    visit("manualEV100", s.manualEV100);
    visit("adaptationUp", s.adaptationUp);
    visit("adaptationDown", s.adaptationDown);
    visit("minEV100", s.minEV100);
    visit("maxEV100", s.maxEV100);
    visit("tonemapper", s.tonemapper);
    visit("contrast", s.contrast);
    visit("saturation", s.saturation);
    visit("temperature", s.temperature);
    visit("tint", s.tint);
    visit("vignette", s.vignette);
    visit("grain", s.grain);
    visit("bloom", s.bloom);
    visit("bloomIntensity", s.bloomIntensity);
    visit("bloomRadius", s.bloomRadius);
    visit("ssao", s.ssao);
    visit("aoRadius", s.aoRadius);
    visit("aoIntensity", s.aoIntensity);
    visit("aoSlices", s.aoSlices);
    visit("aoSteps", s.aoSteps);
    visit("ssgi", s.ssgi);
    visit("ssgiIntensity", s.ssgiIntensity);
    visit("ssgiThickness", s.ssgiThickness);
    visit("ssr", s.ssr);
    visit("ssrMaxRoughness", s.ssrMaxRoughness);
    visit("ssrThickness", s.ssrThickness);
    visit("ssrSteps", s.ssrSteps);
    visit("shadows", s.shadows);
    visit("shadowCascades", s.shadowCascades);
    visit("shadowDistance", s.shadowDistance);
    visit("shadowResolution", s.shadowResolution);
    visit("shadowSoftness", s.shadowSoftness);
    visit("shadowNormalBias", s.shadowNormalBias);
    visit("shadowDepthBias", s.shadowDepthBias);
    visit("pcss", s.pcss);
    visit("contactShadows", s.contactShadows);
    visit("staggerCascades", s.staggerCascades);
    visit("localShadows", s.localShadows);
    visit("localShadowCount", s.localShadowCount);
    visit("localShadowResolution", s.localShadowResolution);
    visit("localShadowSoftness", s.localShadowSoftness);
    visit("fog", s.fog);
    visit("fogDensity", s.fogDensity);
    visit("fogHeightFalloff", s.fogHeightFalloff);
    visit("fogHeight", s.fogHeight);
    visit("fxaa", s.fxaa);
    visit("specularAntiAliasing", s.specularAntiAliasing);
    visit("multiScatter", s.multiScatter);
    visit("debugView", s.debugView);
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
        glm::vec3 v;
        if (!parseFloat(text, v.x) || !parseFloat(text, v.y) || !parseFloat(text, v.z)) return false;
        value = v;
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

}  // namespace render_settings_detail

export inline bool setRenderSetting(RenderSettings& settings, std::string_view key, std::string_view value) {
    bool found = false;
    visitRenderSettings(settings, [&](std::string_view name, auto& field) {
        if (!found && name == key) found = render_settings_detail::parseValue(value, field);
    });
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
