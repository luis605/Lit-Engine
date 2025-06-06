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
    ImVec2 window_size = ImGui::GetWindowSize();
    const float spacingWidth = 200.0f;

    const float remainingWidth = window_size.x - spacingWidth;
    const float margin = 30.0f;
    const float inputWidth = remainingWidth - margin;

    ImGui::Text("Inspecting Camera");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Camera Properties")) {
        ImGui::Indent();

        bool isOrthographic = (sceneCamera.projection == 1) ? true : false;

        // Set the width for the text and combo box
        ImGui::Text("Camera Projection: ");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(inputWidth);
        if (ImGui::BeginCombo("##Projection", isOrthographic ? "Orthographic"
                                                             : "Perspective")) {
            if (ImGui::Selectable("Orthographic", isOrthographic)) {
                isOrthographic = true;
                sceneCamera.projection = 1;
            }
            if (ImGui::Selectable("Perspective", !isOrthographic)) {
                isOrthographic = false;
                sceneCamera.projection = 0;
            }
            ImGui::EndCombo();
        }

        // Set the width for the text and slider
        ImGui::Text("Fovy: ");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::SliderFloat("##FovySlider", &sceneCamera.fovy, 1.0f, 180.0f,
                           "%.1f");

        ImGui::Unindent();
    }
}
