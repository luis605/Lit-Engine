#include "../../../include_all.h"

void CameraInspector()
{
    ImVec2 window_size = ImGui::GetWindowSize();
    const float inputWidth = 200.0f;

    ImGui::Text("Inspecting Camera");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::BeginChild("MainContent", window_size);

    if (ImGui::CollapsingHeader("Camera Properties"))
    {
        ImGui::Indent(10.0f);

        bool isOrthographic = (scene_camera.projection == 1) ? true : false;

        ImGui::Text("Camera Projection: ");
        ImGui::SameLine(inputWidth);
        if (ImGui::BeginCombo("Projection", isOrthographic ? "Orthographic" : "Perspective")) {
            if (ImGui::Selectable("Orthographic", isOrthographic)) {
                isOrthographic = true;
                scene_camera.projection = 1;
            }
            if (ImGui::Selectable("Perspective", !isOrthographic)) {
                isOrthographic = false;
                scene_camera.projection = 0;
            }
            ImGui::EndCombo();
        }


        ImGui::Text("Fovy: ");
        ImGui::SameLine(inputWidth);
        ImGui::SliderFloat("##FovySlider", &scene_camera.fovy, 1.0f, 180.0f, ".1f");

        ImGui::Unindent(10.0f);
    }

    ImGui::EndChild();
}