#include "../../include_all.h"

void WorldInspector()
{
    const float inputWidth = 200.0f;

    ImGui::PushFont(s_Fonts["ImGui Default"]);

    // Header Title
    ImGui::Text("Inspecting World");

    ImGui::Spacing();


    if (ImGui::CollapsingHeader((std::string(ICON_FA_FILTER) + " Post Processing").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(20.0f);

        // Bloom Panel
        if (ImGui::CollapsingHeader(ICON_FA_SUN " Bloom", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(20.0f);

            // Bloom Enabled Checkbox
            ImGui::Text("Enabled:");
            ImGui::SameLine(inputWidth);
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("##BloomToggle", &bloomEnabled);

            // Brightness Slider
            ImGui::Text(ICON_FA_ADJUST " Brightness:");
            ImGui::SameLine(inputWidth);
            if (ImGui::SliderFloat("##BrightnessControl", &bloomBrightness, -2.0f, 2.0f))
            {
                SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "bloomBrightness"), &bloomBrightness, SHADER_ATTRIB_FLOAT);
            }

            // Samples Slider
            ImGui::Text(ICON_FA_CUBE " Samples:");
            ImGui::SameLine(inputWidth);
            if (ImGui::SliderFloat("##SamplesControl", &bloomSamples, 1.0f, 8.0f, "%1.f"))
            {
                int shaderLocation = glGetUniformLocation(downsamplerShader.id, "samples");

                glUseProgram(downsamplerShader.id);
                glUniform1i(shaderLocation, bloomSamples);
                glUseProgram(0);
            }

            ImGui::Unindent(20.0f);
        }

        ImGui::Unindent(20.0f);
    }

    ImGui::Spacing();

    // Lighting Panel
    if (ImGui::CollapsingHeader(ICON_FA_LIGHTBULB " Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(20.0f);

        // Ambient Light Panel
        if (ImGui::CollapsingHeader(ICON_FA_SUN " Ambient Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(20.0f);

            // Ambient Light Color Picker
            ImVec4 light_colorImGui = ImVec4(ambientLight.x, ambientLight.y, ambientLight.z, ambientLight.w);
            if (ImGui::ColorButton(ICON_FA_PALETTE " Ambient Light Color", light_colorImGui))
            {
                ImGui::OpenPopup("##AmbientLightColorPicker");
            }

            if (ImGui::BeginPopupContextItem("##AmbientLightColorPicker"))
            {
                ImGui::ColorPicker4("##AmbientLightColor", (float*)&light_colorImGui);
                ambientLight = { light_colorImGui.x, light_colorImGui.y, light_colorImGui.z, light_colorImGui.w };
                SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);
                ImGui::EndPopup();
            }


            ambientLight.x = light_colorImGui.x;
            ambientLight.y = light_colorImGui.y;
            ambientLight.z = light_colorImGui.z;
            ambientLight.w = light_colorImGui.w;

            SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);

            ImGui::Unindent(20.0f);
        }

        // Skybox Panel
        if (ImGui::CollapsingHeader(ICON_FA_MOUNTAIN_SUN " Skybox", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(20.0f);

            // Skybox Color Picker
            ImVec4 light_colorImGui = ImVec4(skyboxColor.x, skyboxColor.y, skyboxColor.z, skyboxColor.w);

            if (ImGui::ColorButton(ICON_FA_PALETTE " Skybox Color", light_colorImGui))
            {
                ImGui::OpenPopup("##SkyboxColorPicker");
            }

            if (ImGui::BeginPopupContextItem("##SkyboxColorPicker"))
            {
                ImGui::ColorPicker4("##SkyboxColor", (float*)&light_colorImGui);
                skyboxColor = { light_colorImGui.x, light_colorImGui.y, light_colorImGui.z, light_colorImGui.w };
                SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "skyboxColor"), &skyboxColor, SHADER_UNIFORM_VEC4);
                ImGui::EndPopup();
            }

            ImGui::Unindent(20.0f);
        }

        ImGui::Unindent(20.0f);
    }

    ImGui::PopFont();
}