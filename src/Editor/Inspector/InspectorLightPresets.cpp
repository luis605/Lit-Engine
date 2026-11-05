module;

#include <imgui.h>

#include <array>
#include <format>

module Editor.inspector;

import Editor.context;
import Engine.World;

namespace {

struct IntensityPreset {
    const char* name;
    float lumens;
};

constexpr std::array<IntensityPreset, 5> kPointPresets = {{{"Candle", 1.0e5f}, {"Bulb", 1.0e6f}, {"Floodlight", 3.0e6f}, {"Street lamp", 5.0e6f}, {"Sun-ish", 5.0e7f}}};
constexpr std::array<IntensityPreset, 4> kDirectionalPresets = {{{"Moonlight", 1.0f}, {"Overcast", 20000.0f}, {"Sun", 100000.0f}, {"Midday", 120000.0f}}};
constexpr float kDimLumensThreshold = 1000.0f;
constexpr ImVec4 kWarningColor{1.0f, 0.75f, 0.35f, 1.0f};

}

void Inspector::drawIntensityPresets(EditorContext&, LightComponent& light, bool& committed) {
    const bool directional = light.type == LightComponent::Type::Directional;
    if (ImGui::BeginCombo("Preset", "choose...")) {
        const auto drawPresets = [&](const auto& presets) {
            for (const auto& preset : presets) {
                if (!ImGui::Selectable(std::format("{}  ({:.3g})", preset.name, preset.lumens).c_str())) continue;
                light.intensity = preset.lumens;
                committed = true;
            }
        };
        if (directional) drawPresets(kDirectionalPresets);
        else drawPresets(kPointPresets);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Photometric units. Exposure matches a 100 klx sun, so point lights need roughly 1e6 to 1e7 lumens to show in daylight.");
    if (directional) {
        ImGui::TextDisabled("Unit: lux (illuminance)");
        return;
    }
    ImGui::TextDisabled("Unit: lumens (candela = lumens / 4 pi)");
    if (light.intensity < kDimLumensThreshold) ImGui::TextColored(kWarningColor, "Very dim for photometric units; pick a preset");
}
