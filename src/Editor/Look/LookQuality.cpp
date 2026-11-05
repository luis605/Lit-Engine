module;

#include <imgui.h>

#include <format>
#include <string>
#include <string_view>

#include "Editor/Look/QualityPresets.hpp"

module Editor.look;

import Editor.context;
import Engine.engine;
import Engine.Render.settings;

namespace {

constexpr const char* kCustomLabel = "custom";
constexpr ImVec4 kActiveColor{0.55f, 0.85f, 0.55f, 1.0f};

std::string yesNo(bool enabled) { return enabled ? "on" : "off"; }

std::string capitalized(std::string_view name) {
    std::string label(name);
    if (!label.empty()) label.front() = static_cast<char>(label.front() - 'a' + 'A');
    return label;
}

void summaryRow(const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted(value.c_str());
}

}

bool applyQualityPreset(RenderSettings& settings, std::string_view name) {
    return quality::applyPresetWith(name, [&](std::string_view key, std::string_view value) { return setRenderSetting(settings, key, value); });
}

std::string activeQualityPreset(const RenderSettings& settings) {
    for (const quality::Preset& preset : quality::kPresets) {
        RenderSettings candidate = settings;
        applyQualityPreset(candidate, preset.name);
        bool matches = true;
        for (const quality::Setting& setting : preset.settings) {
            matches = matches && renderSettingValue(candidate, setting.key) == renderSettingValue(settings, setting.key);
        }
        if (matches) return std::string(preset.name);
    }
    return kCustomLabel;
}

void LookPanel::drawQuality(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Quality", ImGuiTreeNodeFlags_DefaultOpen)) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    const std::string active = activeQualityPreset(settings);
    for (const quality::Preset& preset : quality::kPresets) {
        const bool isActive = active == preset.name;
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Text, kActiveColor);
        if (ImGui::Button(capitalized(preset.name).c_str())) {
            applyQualityPreset(settings, preset.name);
            ctx.toast(std::format("Quality: {}", capitalized(preset.name)));
        }
        if (isActive) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::TextDisabled("(%s)", active.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("One-click settings bundles. Palette: quality.low, quality.medium, quality.high, quality.ultra");
    summaryRow("Reflections", std::format("{}, {}", kReflectionModeNames[static_cast<int>(settings.reflectionMode)], kReflectionQualityNames[static_cast<int>(settings.reflectionQuality)]));
    summaryRow("Colour bleed", settings.colorBleed ? std::format("on, quality {}", settings.colorBleedQuality) : yesNo(false));
    summaryRow("Contact shadows", yesNo(settings.contactShadows));
    summaryRow("Bent normals", yesNo(settings.bentNormals));
    summaryRow("Area lights", yesNo(settings.areaLights));
    summaryRow("Render scale", std::format("{:.2f}", settings.renderScale));
    summaryRow("Governor", settings.qualityGovernor ? std::format("on, {:.1f} ms target", settings.targetFrameMs) : yesNo(false));
}
