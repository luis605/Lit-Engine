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

#define RAYMATH_IMPLEMENTATION
#include <raylib.h>
#include <raymath.h>

void LightInspector() {
    ImGui::Text("Inspecting Light");

    ImGui::Dummy(ImVec2(0.0f, 15.0f));

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Light Properties", false)) {
        ImGui::Indent();

        static const char* lights_types[] = {"Directional Light", "Point Light",
                                             "Spot Light"};
        static int currentItem = selectedLight->light.type;

        if (ImGui::BeginCombo("Light Types", lights_types[currentItem])) {
            for (int index = 0; index < IM_ARRAYSIZE(lights_types); index++) {
                const bool isSelected = (currentItem == index);
                if (ImGui::Selectable(lights_types[index], isSelected))
                    currentItem = index;
                selectedLight->light.type = currentItem;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        ImGui::Text("Position:");
        ImGui::Indent();

        float lightPos[3] = {selectedLight->light.position.x,
                             selectedLight->light.position.y,
                             selectedLight->light.position.z};
        if (ImGui::InputFloat3("##Position", lightPos))
            selectedLight->light.position =
                glm::vec3(lightPos[0], lightPos[1], lightPos[2]);

        ImGui::Unindent();

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        ImVec4 light_colorImGui =
            ImVec4(selectedLight->light.color.r, selectedLight->light.color.g,
                   selectedLight->light.color.b, selectedLight->light.color.a);

        ImGui::Text("Color: ");
        ImGui::ColorEdit4("##Change_Light_Color", (float*)&light_colorImGui,
                          ImGuiColorEditFlags_NoInputs);
        glm::vec4 light_color = {
            (float)(light_colorImGui.x), (float)(light_colorImGui.y),
            (float)(light_colorImGui.z), (float)(light_colorImGui.w)};

        selectedLight->light.color = light_color;

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        if (!AttenuationActiveInputMode) {
            ImGui::SliderFloat("Attenuation", &selectedLight->light.params.x,
                               0.0f, 0.3f);
            AttenuationActiveInputMode =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        } else {
            if (ImGui::InputFloat("Attenuation", &selectedLight->light.params.x,
                                  0.0f, 1.0f, "%.3f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                AttenuationActiveInputMode = false;
        }

        if (!IntensityActiveInputMode) {
            ImGui::SliderFloat("Intensity", &selectedLight->light.params.y, 0.0f,
                               50.0f);
            IntensityActiveInputMode =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        } else {
            if (ImGui::InputFloat("Intensity", &selectedLight->light.params.y,
                                  0.0f, 100000.0f, "%.3f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                IntensityActiveInputMode = false;
        }

        if (selectedLight->light.type == LIGHT_SPOT) {
            if (!InnerCutoffActiveInputMode) {
                ImGui::SliderFloat("Inner Cutoff",
                                &selectedLight->light.params.z, 0.0f, 1.0f);
                InnerCutoffActiveInputMode =
                    ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
            } else {
                if (ImGui::InputFloat("Inner Cutoff",
                                    &selectedLight->light.params.z, 0.0f, 1.0f,
                                    "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                    InnerCutoffActiveInputMode = false;
            }
        }

        if (selectedLight->light.type == LIGHT_SPOT) {
            if (!OuterCutoffActiveInputMode) {
                ImGui::SliderFloat("Outer Cutoff", &selectedLight->light.params.w, 0.0f, 10.0f);
                OuterCutoffActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
            } else {
                if (ImGui::InputFloat("Outer Cutoff", &selectedLight->light.params.w, 0.0f,
                                    1000.0f, "%.3f",
                                    ImGuiInputTextFlags_EnterReturnsTrue))
                    OuterCutoffActiveInputMode = false;
            }
        } else {
            if (!RadiusActiveInputMode) {
                ImGui::SliderFloat("Radius", &selectedLight->light.params.w, 0.0f,
                                100.0f);
                RadiusActiveInputMode =
                    ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
            } else {
                if (ImGui::InputFloat("Radius", &selectedLight->light.params.w, 0.0f,
                                    100000.0f, "%.3f",
                                    ImGuiInputTextFlags_EnterReturnsTrue))
                    RadiusActiveInputMode = false;
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        Vector3 direction = Vector3Multiply(
            glm3ToVec3(selectedLight->light.direction), {360, 360, 360});

        ImGui::SliderFloat("X", &direction.x, -360.0f, 360.0f);
        ImGui::SliderFloat("Y", &direction.y, -360.0f, 360.0f);
        ImGui::SliderFloat("Z", &direction.z, -360.0f, 360.0f);

        selectedLight->light.direction =
            vec3ToGlm3([](Vector3& vec) -> Vector3& {
                static Vector3 result;
                result = Vector3Divide(vec, {360, 360, 360});
                return result;
            }(direction));

        ImGui::Unindent();
    }
}
