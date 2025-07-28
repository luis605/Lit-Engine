/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS

#include <Engine/Editor/MaterialNodeEditor/Editor.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialBlueprints.hpp>
#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <filesystem>

namespace fs = std::filesystem;

bool isMaterialEditorOpen = false;
fs::path selectedMaterialBlueprintPath;

namespace {
    constexpr ImVec4 kErrorBackgroundColor{0.7f, 0.0f, 0.0f, 1.0f};
    constexpr const char* kNoSelectionMsg = "No Material Blueprint selected.";
    constexpr const char* kBlueprintMissingMsg = "Material Blueprint not found.";

    [[nodiscard]] inline ImVec2 CenteredCursorPos(const char* msg) noexcept {
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 textSize = ImGui::CalcTextSize(msg);
        return ImVec2{
            (windowSize.x - textSize.x) * 0.5f,
            (windowSize.y - textSize.y) * 0.5f
        };
    }

    inline void DrawErrorMessage(const char* message) noexcept {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kErrorBackgroundColor);
        ImGui::SetCursorPos(CenteredCursorPos(message));
        ImGui::TextUnformatted(message);
        ImGui::PopStyleColor();
    }

    inline void RenderNoSelectionScreen() noexcept {
        DrawErrorMessage(kNoSelectionMsg);
    }

    inline void RenderMissingBlueprintScreen(const fs::path& blueprintPath) noexcept {
        DrawErrorMessage(kBlueprintMissingMsg);
        LoadMaterialBlueprint(blueprintPath);
    }

    inline void RenderBlueprintEditor(MaterialBlueprint& blueprint) noexcept {
        blueprint.nodeSystem.DrawEditor();

        if (IsKeyPressed(KEY_P)) {
            SaveMaterialBlueprints(blueprint.materialPath, blueprint);
        }
    }
}

void MaterialEditor() noexcept {
    constexpr const char* kEditorTitle = ICON_FA_CUBE " Material Editor";
    ImGui::Begin(kEditorTitle, &isMaterialEditorOpen);

    if (!materialBlueprints.contains(selectedMaterialBlueprintPath)) {
        RenderNoSelectionScreen();
        ImGui::End();
        return;
    }

    const auto it = materialBlueprints.find(selectedMaterialBlueprintPath);
    if (it == materialBlueprints.end()) {
        RenderMissingBlueprintScreen(selectedMaterialBlueprintPath);
        ImGui::End();
        return;
    }

    RenderBlueprintEditor(it->second);
    ImGui::End();
}