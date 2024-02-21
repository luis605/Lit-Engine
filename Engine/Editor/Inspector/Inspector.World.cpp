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



            ImGui::Text("Skybox Texture: ");
            
            if (ImGui::ImageButton((ImTextureID)&skyboxPanorama, ImVec2(200, 200)))
            {
                show_skybox_texture = !show_skybox_texture;
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
                {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int payload_n = *(const int*)payload->Data;

                    string path = dir_path.string();
                    path += "/" + files_texture_struct[payload_n].name;

                    InitSkybox(path.c_str());
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::Button("x##SkuboxEmptyButton", ImVec2(25, 25)))
            {
                InitSkybox();
            }



            ImGui::Unindent(20.0f);
        }

        ImGui::Unindent(20.0f);
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader(ICON_FA_GLOBE " Physics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(20.0f);

        ImGui::Text("Gravity:");

        if (WorldGravityXInputMode)
        {
            if (ImGui::InputFloat("##GravityX", &physics.gravity[0], 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            {
                physics.dynamicsWorld->setGravity(btVector3(physics.gravity[0], physics.gravity[1], physics.gravity[2]));
                WorldGravityXInputMode = false;
            }
        }
        else
        {
            if (ImGui::SliderFloat("##GravityX", &physics.gravity[0], -100, 100, "%.2f"))
                physics.dynamicsWorld->setGravity(btVector3(physics.gravity[0], physics.gravity[1], physics.gravity[2]));
                
            WorldGravityXInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }

        if (WorldGravityYInputMode)
        {
            if (ImGui::InputFloat("##GravityY", &physics.gravity[1], 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            {
                physics.dynamicsWorld->setGravity(btVector3(physics.gravity[0], physics.gravity[1], physics.gravity[2]));
                WorldGravityYInputMode = false;
            }
        }
        else
        {
            if (ImGui::SliderFloat("##GravityY", &physics.gravity[1], -100, 100, "%.2f"))
                physics.dynamicsWorld->setGravity(btVector3(physics.gravity[0], physics.gravity[1], physics.gravity[2]));
                
            WorldGravityYInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }

        if (WorldGravityZInputMode)
        {
            if (ImGui::InputFloat("##GravityZ", &physics.gravity[2], 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            {
                physics.dynamicsWorld->setGravity(btVector3(physics.gravity[0], physics.gravity[1], physics.gravity[2]));
                WorldGravityZInputMode = false;
            }
        }
        else
        {
            if (ImGui::SliderFloat("##GravityZ", &physics.gravity[2], -100, 100, "%.2f"))
                physics.dynamicsWorld->setGravity(btVector3(physics.gravity[0], physics.gravity[1], physics.gravity[2]));
                
            WorldGravityZInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }


        ImGui::Unindent(20.0f);
    }

    ImGui::PopFont();
}