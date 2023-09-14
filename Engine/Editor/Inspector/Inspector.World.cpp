#include "../../include_all.h"

void WorldInspector()
{
    ImGui::Text("World Inspector");
    
    // Input field width (adjust as needed)
    const float inputWidth = 200.0f;

    if (ImGui::CollapsingHeader("Post Processing"))
    {
        ImGui::Text("Bloom:");
        ImGui::SameLine(inputWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::Checkbox("##BloomToggle", &bloomEnabled);
    }

}