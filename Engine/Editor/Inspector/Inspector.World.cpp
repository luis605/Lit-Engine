/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Core.hpp>
#include <Engine/Core/Engine.hpp>
#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/Inspector/Inspector.hpp>
#include <Engine/Lighting/Shaders.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <extras/IconsFontAwesome6.h>
#include <filesystem>

namespace fs = std::filesystem;

void WorldInspector() {
    const float inputWidth = 150.0f;

    // Header Title
    ImGui::Text("Inspecting World");

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);

    constexpr const char* postProcessing_cstr = ICON_FA_FILTER " Post Processing";

    if (ImGui::CollapsingHeader(postProcessing_cstr, false)) {
        ImGui::Indent();

        // Bloom Panel
        ImGui::SetNextItemWidth(-1);

        constexpr const char* bloom_cstr = ICON_FA_STAR " Bloom";

        if (ImGui::CollapsingHeader(bloom_cstr, false)) {
            ImGui::Indent();

            // Bloom Enabled Checkbox
            ImGui::Text("Enabled:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("##BloomToggle", &bloomEnabled);

            // Brightness Slider
            constexpr const char* threshold_cstr = "\uf042" " Threshold:"; // ADJUST

            ImGui::Text(threshold_cstr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##ThresholdControl", &bloomThreshold, 0.0f, 1.0f)) {
                const int shaderLocation = shaderManager.GetUniformLocation(shaderManager.m_upsamplerShader.id, "threshold");
                glUseProgram(shaderManager.m_upsamplerShader.id);
                glUniform1f(shaderLocation, bloomThreshold);
                glUseProgram(0);
            }

            constexpr const char* intensity_cstr = "\uf042" " Intensity:"; // ADJUST

            ImGui::Text(intensity_cstr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##IntensityControl", &bloomIntensity, 0.0f, 2.0f)) {
                const int shaderLocation = shaderManager.GetUniformLocation(shaderManager.m_upsamplerShader.id, "bloomIntensity");
                glUseProgram(shaderManager.m_upsamplerShader.id);
                glUniform1f(shaderLocation, bloomIntensity);
                glUseProgram(0);
            }

            // Samples Slider
            constexpr const char* kernelSize_cstr = ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER " Kernel Size:";

            ImGui::Text(kernelSize_cstr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##KernelSize", &kernelSize, 1.0f, 60.0f)) {
                int shaderLocation = shaderManager.GetUniformLocation(shaderManager.m_verticalBlurShader.id, "kernelSize");
                glUseProgram(shaderManager.m_verticalBlurShader.id);
                glUniform1i(shaderLocation, kernelSize);
                glUseProgram(0);

                shaderLocation = shaderManager.GetUniformLocation(shaderManager.m_horizontalBlurShader.id, "kernelSize");
                glUseProgram(shaderManager.m_horizontalBlurShader.id);
                glUniform1i(shaderLocation, kernelSize);
                glUseProgram(0);
            }

            ImGui::Unindent();
        }

        constexpr const char* vignette_cstr = ICON_FA_IMAGE " Vignette";

        if (ImGui::CollapsingHeader(vignette_cstr, false)) {
            ImGui::Text("Enabled:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("##VignetteToggle", &vignetteEnabled);

            constexpr const char* strength_cstr = "\uf042" " Strength:"; // ADJUST

            ImGui::Text(strength_cstr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##StrengthControl", &vignetteStrength, 0.0f, 1.0f)) {
                SetShaderValue(shaderManager.m_vignetteShader,
                               shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "strength"),
                               &vignetteStrength, SHADER_UNIFORM_FLOAT);
            }

            constexpr const char* radius_cstr = "\uf042" " Radius:"; // ADJUST

            ImGui::Text(radius_cstr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##RadiusControl", &vignetteRadius, 0.0f, 1.0f)) {
                SetShaderValue(
                    shaderManager.m_vignetteShader,
                    shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "radius"),
                    &vignetteRadius, SHADER_UNIFORM_FLOAT);
            }

            constexpr const char* vignetteColor_cstr = ICON_FA_PAINTBRUSH " Color:"; // BRUSH

            ImGui::Text(vignetteColor_cstr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);

            float colorValue[4] = { vignetteColor.x, vignetteColor.y, vignetteColor.z, vignetteColor.w };
            if (ImGui::ColorEdit4("##ColorValue", colorValue, ImGuiColorEditFlags_AlphaBar)) {
                vignetteColor = Vector4(colorValue[0], colorValue[1], colorValue[2], colorValue[3]);
                SetShaderValue(shaderManager.m_vignetteShader,
                    shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "color"),
                    &vignetteColor, SHADER_UNIFORM_VEC4);
    }
        }

        constexpr const char* aberration_cstr = ICON_FA_FILM " Chromatic Aberration";

        if (ImGui::CollapsingHeader(aberration_cstr, false)) {
            ImGui::Text("Enabled:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("##AberrationToggle", &aberrationEnabled);

            constexpr const char* offset_cstr = ICON_FA_SLIDERS " Offset:";

            ImGui::Text(offset_cstr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);

            float channelOffset[3] = {
                aberrationOffset.x,
                aberrationOffset.y,
                aberrationOffset.z
            };

            if (ImGui::SliderFloat3("##ChannelOffset", channelOffset, -0.1f, .1f)) {
                aberrationOffset = {
                    channelOffset[0],
                    channelOffset[1],
                    channelOffset[2]
                };

                SetShaderValue(shaderManager.m_chromaticAberration,
                               shaderManager.GetUniformLocation(shaderManager.m_chromaticAberration.id, "offset"),
                               &aberrationOffset, SHADER_UNIFORM_VEC3);
            }
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);

    constexpr const char* lighting_cstr = ICON_FA_LIGHTBULB " Lighting";

    if (ImGui::CollapsingHeader(lighting_cstr, false)) {
        ImGui::Indent();

        // Skybox Panel
        ImGui::SetNextItemWidth(-1);
        if (ImGui::CollapsingHeader(ICON_FA_CLOUD " Skybox", false)) {
            ImGui::Indent();

            // Skybox Color Picker
            ImGui::Text("Color:");
            ImGui::SameLine();

            ImVec4 lightColorImGUI = ImVec4(skybox.color.x, skybox.color.y,
                                            skybox.color.z, skybox.color.w);

            if (ImGui::ColorButton(ICON_FA_PALETTE " Skybox Color",
                                   lightColorImGUI)) {
                ImGui::OpenPopup("##SkyboxColorPicker");
            }

            if (ImGui::BeginPopupContextItem("##SkyboxColorPicker")) {
                ImGui::ColorPicker4("##SkyboxColor", (float*)&lightColorImGUI);
                skybox.color = {lightColorImGUI.x, lightColorImGUI.y,
                                lightColorImGUI.z, lightColorImGUI.w};
                SetShaderValue(
                    skybox.cubeModel.materials[0].shader,
                    shaderManager.GetUniformLocation(skybox.cubeModel.materials[0].shader.id,
                                       "skyboxColor"),
                    &skybox.color, SHADER_UNIFORM_VEC4);
                ImGui::EndPopup();
            }

            ImGui::Text("Texture:");

            ImGui::Indent();

            if (ImGui::ImageButton("skyboxTex", (ImTextureID)&skybox.cubeMap,
                                   ImVec2(200, 200))) {
                showSkyboxTexture = !showSkyboxTexture;
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD")) {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int payload_n = *(const int*)payload->Data;

                    fs::path path = dirPath / fileStruct[payload_n].name;

                    skybox.loadSkybox(path);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::Button("x##SkuboxEmptyButton", ImVec2(25, 25)))
                UnloadTexture(skybox.cubeMap);

            ImGui::Unindent();

            ImGui::Text("Objects:");
            if (ImGui::Button("+", ImVec2(32, 32))) {
                skybox.skyboxObjects.emplace_back(SkyboxObject{});
                skybox.skyboxObjects.back().name = "New Skybox Object";
                skybox.skyboxObjects.back().texture = windowIconTexture;
                skybox.skyboxObjects.back().scale = Vector2{10, 10};
            }

            for (int index = 0; index < skybox.skyboxObjects.size(); index++) {
                SkyboxObject& object = skybox.skyboxObjects[index];
                ImGui::PushID(index);
                if (ImGui::Button("-##SkyboxObject", ImVec2(32, 32))) {
                    // skybox.skyboxObjects.erase(std::remove(skybox.skyboxObjects.begin(),
                    // skybox.skyboxObjects.end(), object),
                    // skybox.skyboxObjects.end());
                }
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::Text(object.name.c_str());
            }

            ImGui::Unindent();
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);

    constexpr const char* physics_cstr = ICON_FA_GLOBE " Physics";

    if (ImGui::CollapsingHeader(physics_cstr, false)) {
        ImGui::Indent();

        ImGui::Text("Gravity:");

        ImGui::Indent();

        ImGui::Text("X:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (WorldGravityXInputMode) {
            if (ImGui::InputFloat("##GravityX", &physics.gravity.x, 0.0f, 0.0f,
                                  "%.2f",
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                physics.dynamicsWorld->setGravity(btVector3(
                    physics.gravity.x, physics.gravity.y, physics.gravity.z));
                WorldGravityXInputMode = false;
            }
        } else {
            if (ImGui::SliderFloat("##GravityX", &physics.gravity.x, -100, 100,
                                   "%.2f"))
                physics.dynamicsWorld->setGravity(btVector3(
                    physics.gravity.x, physics.gravity.y, physics.gravity.z));

            WorldGravityXInputMode =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }

        ImGui::Text("y:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        if (WorldGravityYInputMode) {
            if (ImGui::InputFloat("##GravityY", &physics.gravity.y, 0.0f, 0.0f,
                                  "%.2f",
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                physics.dynamicsWorld->setGravity(btVector3(
                    physics.gravity.x, physics.gravity.y, physics.gravity.z));
                WorldGravityYInputMode = false;
            }
        } else {
            if (ImGui::SliderFloat("##GravityY", &physics.gravity.y, -100, 100,
                                   "%.2f"))
                physics.dynamicsWorld->setGravity(btVector3(
                    physics.gravity.x, physics.gravity.y, physics.gravity.z));

            WorldGravityYInputMode =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }

        ImGui::Text("Z:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        if (WorldGravityZInputMode) {
            if (ImGui::InputFloat("##GravityZ", &physics.gravity.z, 0.0f, 0.0f,
                                  "%.2f",
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                physics.dynamicsWorld->setGravity(btVector3(
                    physics.gravity.x, physics.gravity.y, physics.gravity.z));
                WorldGravityZInputMode = false;
            }
        } else {
            if (ImGui::SliderFloat("##GravityZ", &physics.gravity.z, -100, 100,
                                   "%.2f"))
                physics.dynamicsWorld->setGravity(btVector3(
                    physics.gravity.x, physics.gravity.y, physics.gravity.z));

            WorldGravityZInputMode =
                ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }

        ImGui::Unindent();
        ImGui::Unindent();
    }
}