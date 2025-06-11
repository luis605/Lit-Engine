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
#include <Engine/Editor/Styles/ImGuiExtras.hpp>
#include <extras/IconsFontAwesome6.h>
#include <filesystem>

namespace fs = std::filesystem;

inline void PostProcessing() {
    constexpr const char* postProcessing_cstr = ICON_FA_FILTER " Post Processing";

    if (ImGui::CollapsingHeader(postProcessing_cstr)) {
        ImGui::Indent(20.0f);
        constexpr const char* bloom_cstr      = ICON_FA_STAR  " Bloom";
        constexpr const char* vignette_cstr   = ICON_FA_IMAGE " Vignette";
        constexpr const char* aberration_cstr = ICON_FA_FILM  " Chromatic Aberration";

        if (ImGui::CollapsingHeader(bloom_cstr)) {
            ImGui::Indent(20.0f);

            if (ImGui::BeginTable("Bloom", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                ImGui::Indent(10.0f);

                ImVec2 spacing = ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0);

                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

                {
                    ImGui::PushID("BLOOM-ENABLED-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Enabled");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    ImGui::ToggleButton("##BloomToggle", bloomEnabled);

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("BLOOM-THRESHOLD-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Threshold");

                    constexpr float DEFAULT_THRESHOLD = 0.2f;
                    if (bloomThreshold != DEFAULT_THRESHOLD) {
                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0));
                        ImGui::SameLine();

                        if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetThreshold")) {
                            bloomThreshold = DEFAULT_THRESHOLD;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Reset Bloom's Threshold to the default value.");
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    if (ImGui::DragFloat("##ThresholdControl", &bloomThreshold, 0.001f, 0.0f, 1.0f)) {
                        const int shaderLocation = shaderManager.GetUniformLocation(shaderManager.m_upsamplerShader.id, "threshold");
                        glUseProgram(shaderManager.m_upsamplerShader.id);
                        glUniform1f(shaderLocation, bloomThreshold);
                        glUseProgram(0);
                    }

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("BLOOM-INTENSITY-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Intensity");

                    constexpr float DEFAULT_INTENSITY = 0.5f;
                    if (bloomIntensity != DEFAULT_INTENSITY) {
                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0));
                        ImGui::SameLine();


                        if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetBloomIntensity")) {
                            bloomIntensity = DEFAULT_INTENSITY;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Reset Bloom's Intensity to the default value.");
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    if (ImGui::DragFloat("##IntensityControl", &bloomIntensity, 0.001f, 0.0f, 2.0f)) {
                        const int shaderLocation = shaderManager.GetUniformLocation(shaderManager.m_upsamplerShader.id, "bloomIntensity");
                        glUseProgram(shaderManager.m_upsamplerShader.id);
                        glUniform1f(shaderLocation, bloomIntensity);
                        glUseProgram(0);
                    }

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("BLOOM-KERNEL-SIZE-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Kernel Size");

                    ImGui::SameLine();
                    ImGui::BeginDisabled();
                    ImGui::Button("(?)");
                    ImGui::EndDisabled();

                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("Controls the blur radius for the bloom effect.\nHigher values create a softer bloom but are more GPU-intensive.");
                    }

                    constexpr int DEFAULT_KERNEL_SIZE = 1;
                    if (kernelSize != DEFAULT_KERNEL_SIZE) {
                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0));
                        ImGui::SameLine();


                        if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetBloomKernelSize")) {
                            kernelSize = DEFAULT_KERNEL_SIZE;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Reset Bloom's Kernel Size to the default value.");
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

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

                    ImGui::PopID();
                }

                ImGui::PopStyleVar(1);
                ImGui::Unindent(10.0f);
                ImGui::EndTable();
            }

            ImGui::Unindent(20.0f);
        }

        if (ImGui::CollapsingHeader(vignette_cstr)) {
            ImGui::Indent(20.0f);

            if (ImGui::BeginTable("Vignette", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                ImGui::Indent(10.0f);

                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

                {
                    ImGui::PushID("VIGNETTE-ENABLED-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Enabled");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    ImGui::ToggleButton("##VignetteToggle", vignetteEnabled);

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("VIGNETTE-STRENGTH-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Strength");

                    constexpr float DEFAULT_VIGNETTE_STRENGTH = 0.5f;
                    if (vignetteStrength != DEFAULT_VIGNETTE_STRENGTH) {
                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0));
                        ImGui::SameLine();

                        if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetVignetteStrength")) {
                            vignetteStrength = DEFAULT_VIGNETTE_STRENGTH;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Reset Vignette's Strength to the default value.");
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    if (ImGui::DragFloat("##StrengthControl", &vignetteStrength, 0.001f, 0.0f, 1.0f)) {
                        SetShaderValue(
                            shaderManager.m_vignetteShader,
                            shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "strength"),
                            &vignetteStrength, SHADER_UNIFORM_FLOAT
                        );
                    }

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("VIGNETTE-RADIUS-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Radius");

                    constexpr float DEFAULT_VIGNETTE_RADIUS = 0.5f;
                    if (vignetteRadius != DEFAULT_VIGNETTE_RADIUS) {
                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 25.0f, 0));
                        ImGui::SameLine();


                        if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetVignetteRadius")) {
                            vignetteRadius = DEFAULT_VIGNETTE_RADIUS;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Reset Vignette's Radius to the default value.");
                        }

                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    if (ImGui::DragFloat("##RadiusControl", &vignetteRadius, 0.001f, 0.0f, 1.0f)) {
                        SetShaderValue(
                            shaderManager.m_vignetteShader,
                            shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "radius"),
                            &vignetteRadius, SHADER_UNIFORM_FLOAT
                        );
                    }

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("VIGNETTE-COLOR-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Color");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    float colorValue[4] = { vignetteColor.x, vignetteColor.y, vignetteColor.z, vignetteColor.w };
                    if (ImGui::ColorEdit4("##ColorValue", colorValue, ImGuiColorEditFlags_AlphaBar)) {
                        vignetteColor = Vector4(colorValue[0], colorValue[1], colorValue[2], colorValue[3]);
                        SetShaderValue(
                            shaderManager.m_vignetteShader,
                            shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "color"),
                            &vignetteColor, SHADER_UNIFORM_VEC4
                        );
                    }

                    ImGui::PopID();
                }

                ImGui::PopStyleVar(1);
                ImGui::Unindent(10.0f);
                ImGui::EndTable();
            }

            ImGui::Unindent(20.0f);
        }

        if (ImGui::CollapsingHeader(aberration_cstr)) {
            ImGui::Indent(20.0f);

            if (ImGui::BeginTable("Aberration", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                ImGui::Indent(10.0f);

                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

                {
                    ImGui::PushID("ABERRATION-ENABLED-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Enabled");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    ImGui::ToggleButton("##AberrationToggle", aberrationEnabled);

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("ABERRATION-OFFSETS-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Offset");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    float channelOffset[3] = {
                        aberrationOffset.x,
                        aberrationOffset.y,
                        aberrationOffset.z
                    };

                    if (ImGui::DragFloat3("##ChannelOffset", channelOffset, 0.001f, -0.1f, 0.1f, "%.3f")) {
                        aberrationOffset = {
                            channelOffset[0],
                            channelOffset[1],
                            channelOffset[2]
                        };

                        SetShaderValue(
                            shaderManager.m_chromaticAberration,
                            shaderManager.GetUniformLocation(shaderManager.m_chromaticAberration.id, "offset"),
                            &aberrationOffset, SHADER_UNIFORM_VEC3
                        );
                    }

                    ImGui::PopID();
                }

                ImGui::PopStyleVar(1);
                ImGui::Unindent(10.0f);
                ImGui::EndTable();
            }

            ImGui::Unindent(20.0f);
        }

        ImGui::Unindent(20.0f);
    }
}

inline void Lighting() {
    constexpr const char* lighting_cstr = ICON_FA_LIGHTBULB " Lighting";

    if (ImGui::CollapsingHeader(lighting_cstr)) {
        ImGui::Indent(20.0f);
        constexpr const char* skybox_cstr       = ICON_FA_CLOUD " Skybox";
        constexpr const char* removeSkyTex_cstr = ICON_FA_TRASH "##RemoveSkyboxTex";

        if (ImGui::CollapsingHeader(skybox_cstr)) {
            ImGui::Indent(20.0f);

            if (ImGui::BeginTable("Skybox", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                ImGui::Indent(10.0f);

                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

                {
                    ImGui::PushID("SKYBOX-COLOR-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Color");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    ImVec4 lightColorImGUI = ImVec4(skybox.color.x, skybox.color.y, skybox.color.z, skybox.color.w);

                    if (ImGui::ColorButton(ICON_FA_PALETTE " Skybox Color", lightColorImGUI)) {
                        ImGui::OpenPopup("##SkyboxColorPicker");
                    }

                    if (ImGui::BeginPopupContextItem("##SkyboxColorPicker")) {
                        ImGui::ColorPicker4("##SkyboxColor", (float*)&lightColorImGUI);
                        skybox.color = {
                            lightColorImGUI.x, lightColorImGUI.y,
                            lightColorImGUI.z, lightColorImGUI.w
                        };

                        SetShaderValue(
                            skybox.cubeModel.materials[0].shader,
                            shaderManager.GetUniformLocation(skybox.cubeModel.materials[0].shader.id, "skyboxColor"),
                            &skybox.color, SHADER_UNIFORM_VEC4
                        );
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                {
                    ImGui::PushID("SKYBOX-TEXTURE-ROW");
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::TextUnformatted("Texture");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    if (ImGui::ImageButton("skyboxTex", (ImTextureID)&skybox.cubeMap, ImVec2(200, 200))) {
                        showSkyboxTexture = !showSkyboxTexture;
                    }

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD")) {
                            IM_ASSERT(payload->DataSize == sizeof(int));
                            int payload_n = *(const int*)payload->Data;

                            fs::path path = dirPath / allItems[payload_n].name;

                            skybox.loadSkybox(path);
                        }

                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();

                    if (ImGui::Button(removeSkyTex_cstr, ImVec2(25, 25))) {
                        UnloadTexture(skybox.cubeMap);
                    }

                    ImGui::PopID();
                }

                ImGui::PopStyleVar(1);
                ImGui::Unindent(10.0f);
                ImGui::EndTable();
            }

            ImGui::Unindent(20.0f);
        }

        ImGui::Unindent(20.0f);
    }
}

inline void Physics() {
    constexpr const char* physics_cstr = ICON_FA_GLOBE " Physics";

    if (ImGui::CollapsingHeader(physics_cstr)) {
        ImGui::Indent(20.0f);

        if (ImGui::BeginTable("Physics", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::Indent(10.0f);

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            {
                ImGui::PushID("PHYSICS-GRAVITY-ROW");
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);

                ImGui::TextUnformatted("Gravity");

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);

                float gravity[3] = {
                    physics.gravity.x,
                    physics.gravity.y,
                    physics.gravity.z
                };


                if (ImGui::DragFloat3("##GravityVector", gravity, 0.05f, -100.0f, 100.0f, "%.2f")) {
                    physics.SetGravity(
                        LitVector3(
                            gravity[0],
                            gravity[1],
                            gravity[2]
                        )
                    );
                }

                ImGui::PopID();
            }

            ImGui::PopStyleVar(1);
            ImGui::Unindent(10.0f);
            ImGui::EndTable();
        }

        ImGui::Unindent(20.0f);
    }
}

void WorldInspector() {
    ImGui::Text("Inspecting World");

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 8.0f));

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);

    PostProcessing();

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);

    Lighting();

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);

    Physics();

    ImGui::PopStyleVar(1);
}