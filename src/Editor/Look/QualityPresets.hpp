#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace quality {

struct Setting {
    std::string_view key;
    std::string_view value;
};

inline constexpr std::size_t kSettingsPerPreset = 14;

struct Preset {
    std::string_view name;
    std::array<Setting, kSettingsPerPreset> settings;
};

inline constexpr std::array<Preset, 4> kPresets = {{
    {"low", {{{"reflectionMode", "1"}, {"reflectionQuality", "0"}, {"colorBleed", "0"}, {"colorBleedQuality", "0"}, {"contactShadows", "0"}, {"bentNormals", "0"}, {"areaLights", "0"}, {"renderScale", "0.7"}, {"qualityGovernor", "1"}, {"aoDenoise", "1"}, {"shadowResolution", "1024"}, {"shadowCascades", "2"}, {"ssrSteps", "24"}, {"ssao", "1"}}}},
    {"medium", {{{"reflectionMode", "2"}, {"reflectionQuality", "1"}, {"colorBleed", "0"}, {"colorBleedQuality", "1"}, {"contactShadows", "1"}, {"bentNormals", "0"}, {"areaLights", "1"}, {"renderScale", "0.85"}, {"qualityGovernor", "1"}, {"aoDenoise", "2"}, {"shadowResolution", "2048"}, {"shadowCascades", "3"}, {"ssrSteps", "48"}, {"ssao", "1"}}}},
    {"high", {{{"reflectionMode", "3"}, {"reflectionQuality", "2"}, {"colorBleed", "1"}, {"colorBleedQuality", "2"}, {"contactShadows", "1"}, {"bentNormals", "1"}, {"areaLights", "1"}, {"renderScale", "1"}, {"qualityGovernor", "0"}, {"aoDenoise", "2"}, {"shadowResolution", "2048"}, {"shadowCascades", "4"}, {"ssrSteps", "64"}, {"ssao", "1"}}}},
    {"ultra", {{{"reflectionMode", "4"}, {"reflectionQuality", "2"}, {"colorBleed", "1"}, {"colorBleedQuality", "3"}, {"contactShadows", "1"}, {"bentNormals", "1"}, {"areaLights", "1"}, {"renderScale", "1"}, {"qualityGovernor", "0"}, {"aoDenoise", "3"}, {"shadowResolution", "4096"}, {"shadowCascades", "4"}, {"ssrSteps", "128"}, {"ssao", "1"}}}},
}};

inline const Preset* findPreset(std::string_view name) {
    for (const Preset& preset : kPresets) {
        if (preset.name == name) return &preset;
    }
    return nullptr;
}

template <typename Setter>
inline bool applyPresetWith(std::string_view name, Setter&& setter) {
    const Preset* preset = findPreset(name);
    if (!preset) return false;
    bool allApplied = true;
    for (const Setting& setting : preset->settings) allApplied = setter(setting.key, setting.value) && allApplied;
    return allApplied;
}

}
