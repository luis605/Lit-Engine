/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include "CodeEditor.hpp"
#include <Engine/Core/Entity.hpp>
#include <Engine/Core/Engine.hpp>
#include <Engine/Core/global_variables.hpp>
#include <Engine/Core/RunGame.hpp>
#include <Engine/Editor/MenuBar/Settings.hpp>
#include <cstddef>
#include <fstream>
#include <raylib.h>
#include <string>

Texture2D saveTexture;
Texture2D hotReloadTexture;

std::string code;
fs::path codeEditorScriptPath;
TextEditor editor;

bool RenderImageButtonWithTooltip(const ImTextureID& textureID, const ImVec2& size,
                                  const char* tooltip) {
    bool buttonClicked = ImGui::ImageButton(tooltip, textureID, size);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", tooltip);
        ImGui::EndTooltip();
    }
    return buttonClicked;
}

void SaveCodeToFile(const std::string& filePath, const std::string& code) {
    std::ofstream file(filePath);
    if (file.is_open()) {
        file << code;
        file.close();
    } else {
        TraceLog(LOG_WARNING,
                 (std::string("Failed to save: ") + filePath).c_str());
    }
}

void CodeEditor() {
    ImGui::Begin(ICON_FA_CODE " Code Editor", NULL);

    if (autoSaveCode) {
        SaveCodeToFile(codeEditorScriptPath.string(), code);
    } else {
        if (RenderImageButtonWithTooltip((ImTextureID)&saveTexture,
                                         ImVec2(22, 22), "Save file")) {
            SaveCodeToFile(codeEditorScriptPath.string(), code);
        }
        ImGui::SameLine();
    }

    if (RenderImageButtonWithTooltip((ImTextureID)&hotReloadTexture,
                                     ImVec2(22, 22), "Reload all scripts")) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            for (Entity& entity : entitiesList) {
                RenderAndRunEntity(entity);
            }
        }
    }

    ImGui::SameLine();

    ImGui::BeginDisabled();
    ImGui::Button(
        (codeEditorScriptPath.string() + std::string("##Script Path")).c_str(),
        ImVec2(ImGui::GetContentRegionAvail().x,
               22 + ImGui::GetStyle().FramePadding.y * 2.0f));
    ImGui::EndDisabled();

    ImGui::Spacing();

    editor.Render("TextEditor");
    code = editor.GetText();

    ImGui::End();
}
