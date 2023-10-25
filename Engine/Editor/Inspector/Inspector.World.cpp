#include "../../include_all.h"

void WorldInspector()
{
    ImGui::Text("World Inspector");
    
    // Input field width (adjust as needed)
    const float inputWidth = 200.0f;

    if (ImGui::CollapsingHeader("Post Processing"))
    {
        ImGui::Indent(15.0f);
        if (ImGui::CollapsingHeader("Bloom"))
        {
            ImGui::Indent(15.0f);
            ImGui::Text("Enabled:");
            ImGui::SameLine(inputWidth);
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("##BloomToggle", &bloomEnabled);

            ImGui::Text("Brightness:");
            ImGui::SameLine(inputWidth);
            ImGui::SetNextItemWidth(-1);
            
            if (ImGui::SliderFloat("##BrightnessControl", &bloomBrightness, -5.0f, 15.0f))
            {
                SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "bloomBrightness"), &bloomBrightness, SHADER_ATTRIB_FLOAT);
            }
            ImGui::Unindent(15.0f);
        }
        ImGui::Unindent(15.0f);

    }

}