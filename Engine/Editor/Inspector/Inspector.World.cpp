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


    if (ImGui::CollapsingHeader("Lighting"))
    {
        ImGui::Indent(15.0f);
        if (ImGui::CollapsingHeader("Ambient Light"))
        {
            ImGui::Indent(15.0f);
            // ImGui::Text("Enabled:");
            // ImGui::SameLine(inputWidth);
            // ImGui::SetNextItemWidth(-1);
            // ImGui::Checkbox("##AmbientLightToggle", &bloomEnabled);


            ImVec4 light_colorImGui = ImVec4(
                ambientLight.x,
                ambientLight.y,
                ambientLight.z,
                ambientLight.w
            );

            ImGui::ColorPicker4("Color", (float*)&light_colorImGui);

            ambientLight.x = light_colorImGui.x;
            ambientLight.y = light_colorImGui.y;
            ambientLight.z = light_colorImGui.z;
            ambientLight.w = light_colorImGui.w;

            SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);


            ImGui::Unindent(15.0f);
        }
        ImGui::Unindent(15.0f);

    }

}