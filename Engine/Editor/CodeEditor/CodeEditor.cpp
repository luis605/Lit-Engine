bool RenderImageButtonWithTooltip(ImTextureID textureID, const ImVec2& size, const char* tooltip) {
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
        TraceLog(LOG_WARNING, (std::string("Failed to save: ") + filePath).c_str());
    }
}

void CodeEditor() {
    ImGui::Begin(ICON_FA_CODE " Code Editor", NULL);

    if (autoSaveCode) {
        SaveCodeToFile(codeEditorScriptPath.string(), code);
    } else {
        if (RenderImageButtonWithTooltip((ImTextureID)&saveTexture, ImVec2(34, 34), "Save file")) {
            SaveCodeToFile(codeEditorScriptPath.string(), code);
        }
        ImGui::SameLine();
    }

    if (RenderImageButtonWithTooltip((ImTextureID)&hotReloadTexture, ImVec2(34, 34), "Reload all scripts")) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            firstTimeGameplay = true;
            for (Entity& entity : entitiesList) {
                entity.running = false;
            }
        }
    }

    ImGui::SameLine();

    ImGui::BeginDisabled();
    ImGui::Button((codeEditorScriptPath.string() + std::string("##Script Path")).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 34 + ImGui::GetStyle().FramePadding.y * 2.0f));
    ImGui::EndDisabled();

    ImGui::Spacing();

    editor.Render("TextEditor");
    code = editor.GetText();

    ImGui::End();
}
