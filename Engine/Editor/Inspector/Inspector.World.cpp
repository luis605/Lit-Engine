#include "../../include_all.h"

void WorldInspector()
{
    const float inputWidth = 200.0f;

    ImGui::Text("Inspecting World");

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader("Post Processing"))
    {
        ImGui::Indent(30.0f);
        if (ImGui::CollapsingHeader("Bloom"))
        {
            ImGui::Indent(30.0f);
            ImGui::Text("Enabled:");
            ImGui::SameLine(inputWidth);
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("##BloomToggle", &bloomEnabled);

            ImGui::Text("Brightness:");
            ImGui::SameLine(inputWidth);
            ImGui::SetNextItemWidth(-1);
            
            if (ImGui::SliderFloat("##BrightnessControl", &bloomBrightness, -2.0f, 2.0f))
            {
                SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "bloomBrightness"), &bloomBrightness, SHADER_ATTRIB_FLOAT);
            }
            ImGui::Unindent(30.0f);
        }
        ImGui::Unindent(30.0f);

    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (ImGui::CollapsingHeader("Lighting"))
    {
        ImGui::Indent(30.0f);
        if (ImGui::CollapsingHeader("Ambient Light"))
        {
            ImGui::Indent(30.0f);

            ImVec4 light_colorImGui = ImVec4(
                ambientLight.x,
                ambientLight.y,
                ambientLight.z,
                ambientLight.w
            );

            ImGui::Text("Ambient Light Color: ");
            ImGui::SameLine(inputWidth + 40);
            if (ImGui::ColorButton("##AmbientLightColorButton", light_colorImGui))
            {
                ImGui::OpenPopup("##AmbientLightColorPicker");
            }

            if (ImGui::BeginPopup("##AmbientLightColorPicker"))
            {
                ImGui::ColorPicker4("##AmbientLightColor", (float*)&light_colorImGui);
                ImGui::EndPopup();
            }

            ambientLight.x = light_colorImGui.x;
            ambientLight.y = light_colorImGui.y;
            ambientLight.z = light_colorImGui.z;
            ambientLight.w = light_colorImGui.w;

            SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);

            ImGui::Unindent(30.0f);
        }
        ImGui::Unindent(30.0f);
    }
}