bool RenderImageButtonWithTooltip(ImTextureID textureID, const ImVec2& size, const char* tooltip) {
    bool buttonClicked = ImGui::ImageButton(textureID, size);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", tooltip);
        ImGui::EndTooltip();
    }
    return buttonClicked;
}

void SaveCodeToFile(const std::string& filePath, const std::string& code) {
    std::cout << "Saving file..." << std::endl;
    std::ofstream file(filePath);
    if (file.is_open()) {
        file << code;
        file.close();
    } else {
        std::cout << "Unable to open file." << std::endl;
    }
}

void CodeEditor() {
    ImGui::Begin(ICON_FA_CODE " Code Editor", NULL);

    if (RenderImageButtonWithTooltip((ImTextureID)&saveTexture, ImVec2(34, 34), "Save file")) {
        SaveCodeToFile(codeEditorScriptPath, code);
    }

    ImGui::SameLine();

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
    ImGui::Button((codeEditorScriptPath.string() + std::string("##Script Path")).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 34));
    ImGui::EndDisabled();

    editor.Render("TextEditor");
    code = editor.GetText();

    ImGui::End();
}