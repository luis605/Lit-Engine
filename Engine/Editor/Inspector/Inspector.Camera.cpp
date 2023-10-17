#include "../../../include_all.h"

void CameraInspector()
{

    ImVec2 window_size = ImGui::GetWindowSize();

    ImGui::Text("Inspecting Camera");
    ImGui::SameLine();

    ImGui::BeginChild("MainContent", window_size);

    if (ImGui::CollapsingHeader("Camera Properties"))
    {
        bool isOrthographic = (scene_camera.projection == 1) ? true : false;

        
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

    }

    ImGui::EndChild();
}