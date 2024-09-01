#include "Settings.h"

enum class SettingsCategory {
    Profile,
    Editor,
    Rendering,
    Audio,
    Controls
};

static SettingsCategory selectedCategory = SettingsCategory::Profile;

void Settings() {
    ImGui::Begin("Settings", &showSettingsWindow);

    // Create two columns
    ImGui::Columns(2, nullptr, false);

    // Left column: Main options
    {
        if (ImGui::Selectable("Profile", selectedCategory == SettingsCategory::Profile)) {
            selectedCategory = SettingsCategory::Profile;
        }
        if (ImGui::Selectable("Editor", selectedCategory == SettingsCategory::Editor)) {
            selectedCategory = SettingsCategory::Editor;
        }
        if (ImGui::Selectable("Rendering", selectedCategory == SettingsCategory::Rendering)) {
            selectedCategory = SettingsCategory::Rendering;
        }
        if (ImGui::Selectable("Audio", selectedCategory == SettingsCategory::Audio)) {
            selectedCategory = SettingsCategory::Audio;
        }
        if (ImGui::Selectable("Controls", selectedCategory == SettingsCategory::Controls)) {
            selectedCategory = SettingsCategory::Controls;
        }

        // Move to the next column
        ImGui::NextColumn();
    }

    {
        switch (selectedCategory) {
            case SettingsCategory::Profile:
                ImGui::Text("Profile Settings:");
                break;

            case SettingsCategory::Editor:
                ImGui::Text("Editor Settings:");
                ImGui::Checkbox("Auto-Save", &autoSave);
                ImGui::Checkbox("Show Line Numbers", &showCodeLineNumbers);
                ImGui::SliderFloat("Editor Font Size", &editorFontSize, 8.0f, 24.0f, "%.1f");
                ImGui::Checkbox("Grid Snapping", &gridSnappingEnabled);
                ImGui::SliderFloat("Grid Snapping Factor", &gridSnappingFactor, 0.0f, 10.0f, "%.1f");

                break;

            case SettingsCategory::Rendering:
                ImGui::Text("Rendering Settings:");
                if (ImGui::Checkbox("V-Sync", &vsyncEnabled)) {
                    SetTargetFPS(vsyncEnabled ? GetMonitorRefreshRate(GetCurrentMonitor()) : 0);
                }
                break;

            // case SettingsCategory::Audio:
            //     ImGui::Text("Audio Settings:");
            //     ImGui::SliderFloat("Master Volume", nullptr, 0.0f, 100.0f);
            //     ImGui::Checkbox("Enable Surround Sound", nullptr);
            //     ImGui::Combo("Output Device", nullptr, "Default\0Speakers\0Headphones\0");
            //     break;

            case SettingsCategory::Controls:
                ImGui::Text("Controls Settings:");
                break;
        }
    }

    ImGui::End();
}