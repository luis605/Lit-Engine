#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Engine.hpp>
#include <Engine/Core/Entity.hpp>
#include <Engine/Core/SaveLoad.hpp>
#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/Inspector/Inspector.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialBlueprints.hpp>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialShaderGenerator.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>

namespace fs = std::filesystem;

void MapInspector(const char* title, const MaterialMapIndex& materialMapIndex,
                  bool& showTexture, fs::path& texturePath,
                  SurfaceMaterialTexture& texture) {
    ImGui::Text(title);

    ImGui::Indent();

    if (ImGui::ImageButton(title,
                           (ImTextureID)&selectedEntity->model.materials[0]
                               .maps[materialMapIndex]
                               .texture,
                           ImVec2(64, 64)))
        showTexture = !showTexture;

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");

        if (payload) {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            fs::path path = dirPath / fileStruct[payload_n].name;

            texturePath = path;
            texture = path;
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    std::string emptyButtonName =
        std::string("x##" + std::string(title) + "EmptyButton").c_str();
    if (ImGui::Button(emptyButtonName.c_str(), ImVec2(25, 25))) {
        texture.cleanup();
        texturePath = "";
    }

    ImGui::Unindent();
}

void MaterialInspector(ChildMaterial& material) {
    material.SyncWithBlueprint();

    ImGui::PushID(&material);

    ImGui::Text("Material Inspector");
    ImGui::Separator();

    ImGui::Text("Material:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", material.name.c_str());
    ImGui::Spacing();

    static bool updateMaterial = false;

    if (ImGui::BeginTable("MaterialProps", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (auto& [uuid, node] : material.nodes) {
            ImGui::PushID(uuid.c_str());

            ImGui::TableNextRow();

            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;

                ImGui::TableSetColumnIndex(0);

                std::string nodeLabel = "Unknown";
                static float offset = ImGui::GetCursorPosX();

                if constexpr (std::is_same_v<T, ColorNode>) nodeLabel = "Color";
                else if constexpr (std::is_same_v<T, TextureNode>) nodeLabel = "Texture";
                else if constexpr (std::is_same_v<T, SliderNode>) nodeLabel = "Slider";
                else if constexpr (std::is_same_v<T, Vector2Node>) nodeLabel = "Vector2";
                ImGui::SetCursorPosX(offset + 10.0f);
                ImGui::TextUnformatted(nodeLabel.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);

                if constexpr (std::is_same_v<T, ColorNode>) {
                    float colorValue[4] = { arg.color.Value.x, arg.color.Value.y, arg.color.Value.z, arg.color.Value.w };
                    if (ImGui::ColorEdit4("##ColorValue", colorValue, ImGuiColorEditFlags_AlphaBar)) {
                        arg.color.Value = ImVec4(colorValue[0], colorValue[1], colorValue[2], colorValue[3]);
                        updateMaterial = true;
                    }
                }
                else if constexpr (std::is_same_v<T, TextureNode>) {
                    ImVec2 previewSize(64, 64);
                    ImGui::ImageButton(uuid.c_str(),
                                       arg.texture.hasTexture() ? (ImTextureID)&arg.texture.getTexture2D() : (ImTextureID){0},
                                       previewSize,
                                       ImVec2(0, 0), ImVec2(1, 1),
                                       ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
                                       ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

                    if (ImGui::BeginDragDropTarget()) {
                        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");
                        if (payload) {
                            if (payload->Data && payload->DataSize == sizeof(int)) {
                                int payload_n = *(const int*)payload->Data;
                                if (payload_n >= 0 && payload_n < fileStruct.size()) {
                                    arg.texturePath = fileStruct[payload_n].full_path;
                                    arg.texture = fileStruct[payload_n].full_path;
                                    updateMaterial = true;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::Text("Path:");
                    ImGui::TextWrapped("%s", arg.texturePath.string().c_str());
                    ImGui::EndGroup();

                }
                else if constexpr (std::is_same_v<T, SliderNode>) {
                    if (ImGui::SliderFloat("##SliderValue", &arg.value, -10, 10)) {
                        updateMaterial = true;
                    }
                }
                 else if constexpr (std::is_same_v<T, Vector2Node>) {
                    if (ImGui::SliderFloat2("##Vector2Value", arg.vec, -10, 10)) {
                        updateMaterial = true;
                    }
                }

            }, node);

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (updateMaterial && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        std::ifstream stream("Engine/Lighting/shaders/lighting_vertex.glsl");
        if (!stream.is_open()) {
            TraceLog(LOG_ERROR, "Failed to open default vertex shader file");
        }

        std::string vertexShaderCode = std::string((std::istreambuf_iterator<char>(stream)),
                                                     std::istreambuf_iterator<char>());

        std::shared_ptr<Shader> shader = shaderManager.LoadShaderProgramFromMemory(vertexShaderCode.c_str(), GenerateMaterialShader(material).c_str());
        if (IsShaderReady(*shader.get())) selectedEntity->setShader(*shader.get());
        else TraceLog(LOG_ERROR, "Failed to generate shader for material: %s", material.name.c_str());

        updateMaterial = false;
        selectedEntity->ReloadTextures(true);
    }

    ImGui::Separator();
    ImGui::Spacing();

    float buttonWidth = 120.0f;
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float offSetX = (availableWidth - buttonWidth) * 0.5f;
    if (offSetX > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offSetX);
    }

    if (ImGui::Button("Save Material", ImVec2(buttonWidth, 0))) {
        SaveChildMaterial(material.path, material);
    }

    ImGui::PopID();
}