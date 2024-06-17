#include "../../../include_all.h"

void CameraInspector()
{
    ImVec2 window_size = ImGui::GetWindowSize();
    const float spacingWidth = 200.0f;

    const float remainingWidth = window_size.x - spacingWidth;
    const float margin = 30.0f;
    const float inputWidth = remainingWidth - margin;

    ImGui::Text("Inspecting Camera");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Camera Properties"))
    {
        ImGui::Indent(30.0f);

        bool isOrthographic = (sceneCamera.projection == 1) ? true : false;

        // Set the width for the text and combo box
        ImGui::Text("Camera Projection: ");
        ImGui::SameLine(spacingWidth);
        ImGui::SetNextItemWidth(inputWidth);
        if (ImGui::BeginCombo("##Projection", isOrthographic ? "Orthographic" : "Perspective")) {
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
        ImGui::SameLine(spacingWidth);
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::SliderFloat("##FovySlider", &sceneCamera.fovy, 1.0f, 180.0f, "%.1f");

        ImGui::Unindent(30.0f);
    }
}
