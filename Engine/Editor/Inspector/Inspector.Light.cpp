/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Core.hpp>
#include <Engine/Core/Engine.hpp>
#include <Engine/Editor/Inspector/Inspector.hpp>
#include <extras/IconsFontAwesome6.h>
#include <glm/glm.hpp>

namespace DefaultLight {
    const glm::vec4 Color       = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    const float     Intensity   = 3.0f;
    const float     Attenuation = 0.001f;
    const float     Radius      = 100.0f;
    const float     InnerAngle  = 12.5f;
    const float     OuterAngle  = 17.5f;
}

bool ResetButton(const char* id, const char* tooltip) {
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0));
    ImGui::SameLine();

    bool clicked = ImGui::Button((std::string(ICON_FA_ROTATE_LEFT) + "##" + id).c_str());

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tooltip);
    }

    return clicked;
}

void LightProperty_Type(Light& light) {
    ImGui::PushID("LIGHT-TYPE-ROW");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Type");

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);

    static const char* light_types[] = {"Directional", "Point", "Spot"};
    int currentItem = light.type;

    if (ImGui::BeginCombo("##LightType", light_types[currentItem])) {
        for (int i = 0; i < IM_ARRAYSIZE(light_types); i++) {
            if (ImGui::Selectable(light_types[i], currentItem == i)) {
                light.type = i;
            }
            if (currentItem == i) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

void LightProperty_Position(glm::vec3& position) {
    ImGui::PushID("LIGHT-POSITION-ROW");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Position");

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat3("##Position", &position.x, 0.1f);
    ImGui::PopID();
}

void LightProperty_Direction(glm::vec3& direction) {
    ImGui::PushID("LIGHT-DIRECTION-ROW");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ImGui::TextUnformatted("Direction");

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);

    float lightDirection[3] = { selectedLight->light.direction.x * 180.0f, selectedLight->light.direction.y * 180.0f, selectedLight->light.direction.z * 180.0f };
    constexpr float INV_180 = 1.0f / 180.0f;

    ImGui::DragFloat3("##Direction", lightDirection, 0.1f, -180.0f, 180.0f, "%.1f°");

    selectedLight->light.direction = glm::vec3(
        lightDirection[0] * INV_180,
        lightDirection[1] * INV_180,
        lightDirection[2] * INV_180
    );

    ImGui::PopID();
}

void LightProperty_Color(glm::vec4& color) {
    ImGui::PushID("LIGHT-COLOR-ROW");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Color");
    if (color != DefaultLight::Color) {
        if (ResetButton("##ResetColor", "Reset light's color to white")) color = DefaultLight::Color;
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::ColorEdit4("##Color", &color.r, ImGuiColorEditFlags_Float);
    ImGui::PopID();
}

void LightProperty_Intensity(float& intensity) {
    ImGui::PushID("LIGHT-INTENSITY-ROW");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ImGui::TextUnformatted("Intensity");

    if (intensity != DefaultLight::Intensity) {
        if (ResetButton("##ResetColor", "Reset light's intensity to the default value")) intensity = DefaultLight::Intensity;
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);

    ImGui::DragFloat("##Intensity", &intensity, 0.1f, 0.0f, 1000.0f, "%.2f");

    ImGui::PopID();
}

void LightProperty_Attenuation(float& attenuation) {
    ImGui::PushID("LIGHT-ATTENUATION-ROW");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ImGui::TextUnformatted("Attenuation");

    if (attenuation != DefaultLight::Attenuation) {
        if (ResetButton("##ResetAttenuation", "Reset light's attenuation to the default value")) attenuation = DefaultLight::Attenuation;
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);

    ImGui::DragFloat("##Attenuation", &attenuation, 0.001f, 0.0f, 1.0f, "%.3f");

    ImGui::PopID();
}

void LightProperty_SpotAngles(float& innerAngle, float& outerAngle) {
    ImGui::PushID("LIGHT-INNER-ANGLE-ROW");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Inner Angle");
    if (innerAngle != DefaultLight::InnerAngle) {
        if (ResetButton("##ResetInner", "Reset spot light's inner angle to the default value")) innerAngle = DefaultLight::InnerAngle;
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);

    ImGui::SliderFloat("##InnerAngle", &innerAngle, 0.0f, 90.0f, "%.1f°");

    ImGui::PopID();

    ImGui::PushID("LIGHT-OUTER-ANGLE-ROW");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Outer Angle");
    if (outerAngle != DefaultLight::OuterAngle) {
        if (ResetButton("##ResetOuter", "Reset spot light's outer angle to the default value")) innerAngle = DefaultLight::OuterAngle;
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);

    ImGui::SliderFloat("##OuterAngle", &outerAngle, 0.0f, 90.0f, "%.1f°");

    ImGui::PopID();
}

void LightProperty_Radius(float& radius) {
    ImGui::PushID("LIGHT-RADIUS-ROW");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Radius");
    if (radius != DefaultLight::Radius) {
        if (ResetButton("##ResetRadius", "Reset point light's radius to the default value")) radius = DefaultLight::Radius;
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat("##Radius", &radius, 0.1f, 0.0f, 1000.0f);
    ImGui::PopID();
}


void LightInspector() {
    if (selectedLight == nullptr) {
        ImGui::Text("No light selected.");
        return;
    }

    ImGui::Text("Inspecting Light");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    constexpr const char* light_props_cstr = ICON_FA_LIGHTBULB " Light Properties";
    if (ImGui::CollapsingHeader(light_props_cstr)) {
        ImGui::Indent(20.0f);

        if (ImGui::BeginTable("Light", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::Indent(10.0f);

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            LightProperty_Type(selectedLight->light);
            LightProperty_Color(selectedLight->light.color);
            LightProperty_Intensity(selectedLight->light.params.y);
            LightProperty_Attenuation(selectedLight->light.params.x);

            switch (selectedLight->light.type) {
                case LIGHT_DIRECTIONAL:
                    LightProperty_Direction(selectedLight->light.direction);
                    break;
                case LIGHT_POINT:
                    LightProperty_Position(selectedLight->light.position);
                    LightProperty_Radius(selectedLight->light.params.w);
                    break;
                case LIGHT_SPOT:
                    LightProperty_Position(selectedLight->light.position);
                    LightProperty_Direction(selectedLight->light.direction);
                    LightProperty_SpotAngles(selectedLight->light.params.z, selectedLight->light.params.w);
                    break;
            }

            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
            ImGui::EndTable();
        }
        ImGui::Unindent(20.0f);
    }
}