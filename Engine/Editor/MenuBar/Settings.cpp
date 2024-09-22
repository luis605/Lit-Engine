#include "Settings.h"

enum class SettingsCategory {
    Profile,
    Code,
    Editor,
    Rendering,
    Audio,
    Controls
};

static SettingsCategory selectedCategory = SettingsCategory::Profile;

void Settings() {
    ImGui::Begin("Settings", &showSettingsWindow);
    ImGui::SetWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

    ImGui::Columns(2, nullptr, false);

    {
        ImGui::BeginChild("Category List", ImVec2(0, 0), true);

        if (ImGui::Selectable("Profile", selectedCategory == SettingsCategory::Profile, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
            selectedCategory = SettingsCategory::Profile;
        }
        if (ImGui::Selectable("Code", selectedCategory == SettingsCategory::Code, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
            selectedCategory = SettingsCategory::Code;
        }
        if (ImGui::Selectable("Editor", selectedCategory == SettingsCategory::Editor, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
            selectedCategory = SettingsCategory::Editor;
        }
        if (ImGui::Selectable("Rendering", selectedCategory == SettingsCategory::Rendering, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
            selectedCategory = SettingsCategory::Rendering;
        }
        if (ImGui::Selectable("Audio", selectedCategory == SettingsCategory::Audio, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
            selectedCategory = SettingsCategory::Audio;
        }
        if (ImGui::Selectable("Controls", selectedCategory == SettingsCategory::Controls, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
            selectedCategory = SettingsCategory::Controls;
        }

        ImGui::EndChild();
        ImGui::NextColumn();
    }

    {
        ImGui::BeginChild("Category Settings", ImVec2(0, 0), true);

        switch (selectedCategory) {
            case SettingsCategory::Profile:
                ImGui::TextWrapped("Profile Settings:");
                ImGui::Separator();
                ImGui::Text("User preferences and account settings will be here.");
                break;

            case SettingsCategory::Code:
                ImGui::TextWrapped("Code Settings:");
                ImGui::Separator();
                ImGui::Checkbox("Auto-Save", &autoSaveCode);
                if (ImGui::Checkbox("Show Line Numbers", &showCodeLineNumbers)) {
                    editor.SetShowLineNumbers(showCodeLineNumbers);
                }
                break;

            case SettingsCategory::Editor:
                ImGui::TextWrapped("Editor Settings:");
                ImGui::Separator();

                if (ImGui::SliderFloat("Editor Font Size", &editorFontSize, 8.0f, 24.0f, "%.1f")) {
                    fontsNeedUpdate = true;
                }

                ImGui::Checkbox("Grid Snapping", &gridSnappingEnabled);
                ImGui::SliderFloat("Grid Snapping Factor", &gridSnappingFactor, 0.1f, 10.0f, "%.1f");
                break;

            case SettingsCategory::Rendering:
                ImGui::TextWrapped("Rendering Settings:");
                ImGui::Separator();
                if (ImGui::Checkbox("V-Sync", &vsyncEnabled)) {
                    SetTargetFPS(vsyncEnabled ? GetMonitorRefreshRate(GetCurrentMonitor()) : 0);
                }
                ImGui::Text("Additional rendering settings will go here.");
                break;

            case SettingsCategory::Audio:
                ImGui::TextWrapped("Audio Settings:");
                ImGui::Separator();
                break;

            case SettingsCategory::Controls:
                ImGui::TextWrapped("Controls Settings:");
                ImGui::Separator();
                ImGui::Text("Keybindings and control settings will be here.");
                break;
        }

        ImGui::EndChild();
    }

    ImGui::End();
}