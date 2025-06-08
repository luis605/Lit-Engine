/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include <extras/IconsFontAwesome6.h>

void CameraInspector() {
    ImGui::Text("Inspecting Camera");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    constexpr const char* camera_props_cstr = ICON_FA_VIDEO " Properties";
    if (ImGui::CollapsingHeader(camera_props_cstr)) {
        ImGui::Indent(20.0f);

        if (ImGui::BeginTable("Camera", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::Indent(10.0f);
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            {
                ImGui::PushID("CAMERA-PROJECTION-ROW");
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Projection");

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);

                const char* currentProjectionName = (sceneCamera.projection == 1) ? "Orthographic" : "Perspective";
                if (ImGui::BeginCombo("##Projection", currentProjectionName)) {
                    if (ImGui::Selectable("Perspective", sceneCamera.projection == 0)) {
                        sceneCamera.projection = 0;
                    }
                    if (ImGui::Selectable("Orthographic", sceneCamera.projection == 1)) {
                        sceneCamera.projection = 1;
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }

            {
                ImGui::PushID("CAMERA-FOVY-ROW");
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Field of View");

                constexpr float DEFAULT_FOVY = 60.0f;
                if (sceneCamera.fovy != DEFAULT_FOVY) {
                    ImGui::SameLine();
                    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0));
                    ImGui::SameLine();

                    if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetFOVY")) {
                        sceneCamera.fovy = DEFAULT_FOVY;
                    }

                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("Reset Field of View to 60°");
                    }
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderFloat("##FovySlider", &sceneCamera.fovy, 1.0f, 179.0f, "%.1f°");

                ImGui::PopID();
            }

            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
            ImGui::EndTable();
        }

        ImGui::Unindent();
    }
}