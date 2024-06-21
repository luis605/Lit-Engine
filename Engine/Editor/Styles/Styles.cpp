std::string toHexString(ImU32 color)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(8) << std::hex << color;
    return stream.str();
}

void LoadThemeFromFile(const std::string& filename)
{
    SetStyleGray(&ImGui::GetStyle());
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }

    try {
        std::string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        json data = json::parse(json_string);

        ImGuiStyle* style = &ImGui::GetStyle();
        ImVec4* colors = style->Colors;

        for (const auto& option : data["options"])
        {
            std::string name = option["index"];

            int themes_colors_string_size = sizeof(themes_colors_string) / sizeof(themes_colors_string[0]);
            int optionIndex = -1;

            for (int index = 0; index < themes_colors_string_size; index++) {
                if (strcmp(themes_colors_string[index], name.c_str()) == 0) {
                    optionIndex = index;
                    break;
                }
            }

            if (optionIndex != -1) {
                std::string color_hex = option["color"];
                ImU32 color = std::stoul(color_hex, nullptr, 16);
                colors[themes_colors[optionIndex]] = ImGui::ColorConvertU32ToFloat4(color);
            } else {
                std::cerr << "Invalid option index: " << name << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
    }
}

std::string ShowFileExplorer(const char* folderPath, nlohmann::json fileContent, FileExplorerType type = FileExplorerType::Save)
{
    if (showSaveThemeWindow || showLoadThemeWindow)
    {
        showFileExplorer = true;
        
        ImGui::Begin("File Explorer", &showFileExplorer, ImGuiConfigFlags_ViewportsEnable);

        // Display the folder path
        ImGui::Text("Folder: %s", folderPath);
        

        static char fileName[256] = "";

        // Display the list of files and directories in the folder
        ImGui::BeginChild("FileList", ImVec2(0, 300), true);
        for (auto& file : fs::directory_iterator(folderPath)) {
            if (ImGui::Selectable(file.path().filename().string().c_str())) {
                std::strncpy(fileName, file.path().filename().string().c_str(), sizeof(fileName));
            }
        }
        ImGui::EndChild();
        
        // Input field for new file name
        ImGui::InputText("File name", fileName, 256);
        
        if ( (type == FileExplorerType::Save) || (type == FileExplorerType::SaveLoad) )
        {
            if (ImGui::Button("Save file")) {
                std::ofstream file(folderPath + std::string("/") + fileName + std::string(".theme"));
                file << fileContent.dump(4) << std::endl;
                file.close();
                ImGui::End();
                return "";
            }
        }

        if ( (type == FileExplorerType::Load) || (type == FileExplorerType::SaveLoad) )
        {
            if (ImGui::Button("Select File")) {
                std::string fullFileNamePath = std::string(folderPath) + std::string(fileName);
                LoadThemeFromFile(fullFileNamePath);
            }
        }

        ImGui::End();


        // If the user closed the window, reset the other booleans
        if (!showFileExplorer)
        {
            showSaveThemeWindow = false;
            showLoadThemeWindow = false;
        }
    }
    return "";
}

void CreateNewTheme()
{
    if (!createNewThemeWindowOpen) return;
    ImGui::Begin("Create New Theme", &createNewThemeWindowOpen);
    ImGui::Text("Themes options:");

    ImGui::Combo("##OptionsCombo", (int*)&theme_create_selected_option, themes_colors_string, IM_ARRAYSIZE(themes_colors_string));
    ImGui::SameLine();
    bool add_to_list = ImGui::Button("Add Selected Option", {ImGui::CalcTextSize("Add Selected Option").x + 30, 30});
    if (add_to_list)
    {
        auto checker_algorithm = std::find(newThemeSavedOptions.begin(), newThemeSavedOptions.end(), theme_create_selected_option);
        if (checker_algorithm != newThemeSavedOptions.end())
        {
            std::cout << "Item already exists" << std::endl;
        }
        else
        {
            std::cout << "Selected Option '" << theme_create_selected_option <<"' Added!" << std::endl;
            newThemeSavedOptions.emplace_back(theme_create_selected_option);
            newThemeSavedOptionsColor.emplace_back(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    for (int index = 0; index < newThemeSavedOptions.size(); index++)
    {
        int optionIndex = newThemeSavedOptions[index];
        ImGui::Text("%s", themes_colors_string[optionIndex]);
        ImGui::SameLine();
        std::string color_pickerName = "##Color Picker - " + std::to_string(index);
        ImGui::ColorEdit4(color_pickerName.c_str(), (float*)&newThemeSavedOptionsColor[index], ImGuiColorEditFlags_NoInputs);
    }

    bool saveButton = ImGui::Button("Save", {110, 30});
    ImGui::SameLine();
    bool load_button = ImGui::Button("Load", {110, 30});
    ImGui::SameLine();
    bool apply_button = ImGui::Button("Apply/Preview", {110, 30});


    if (saveButton)
    {
        showSaveThemeWindow = true;
    }

    if (showSaveThemeWindow && !showLoadThemeWindow)
    {
        nlohmann::json data;

        for (int i = 0; i < newThemeSavedOptions.size(); i++)
        {
            int optionIndex = newThemeSavedOptions[i];

            ImU32 color = ImGui::ColorConvertFloat4ToU32(newThemeSavedOptionsColor[i]);
            std::string color_hex = toHexString(color);
            data["options"].emplace_back(nlohmann::json{
                {"index", themes_colors_string[optionIndex]},
                {"color", color_hex}
            });
        }
        

        ShowFileExplorer(themesFolder.c_str(), data, FileExplorerType::Save);
    }


    if (load_button)
    {
        showLoadThemeWindow = true;
    }

    if (showLoadThemeWindow && !showSaveThemeWindow)
        ShowFileExplorer(themesFolder.c_str(), "", FileExplorerType::Load);

    if (showLoadThemeWindow && showSaveThemeWindow)
        ShowFileExplorer(themesFolder.c_str(), "", FileExplorerType::SaveLoad);


    if (apply_button)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;
        for (int i = 0; i < newThemeSavedOptions.size(); i++)
        {
            int optionIndex = newThemeSavedOptions[i];
            colors[optionIndex] = newThemeSavedOptionsColor[i];
        }
    }



    ImGui::End();
}





void SetStyleHighContrast(ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    // Text
    colors[ImGuiCol_Text] = ImVec4(0.0f, 0.90f, 0.90f, 1.00f);

    // Frames
    colors[ImGuiCol_FrameBg]            = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

    // Windows, Tabs, MenuBar
    colors[ImGuiCol_WindowBg]           = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_Tab]                = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    colors[ImGuiCol_TabUnfocused]       = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_MenuBarBg]          = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    // Title
    colors[ImGuiCol_TitleBg]            = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);

    // Others
    colors[ImGuiCol_Button]             = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    colors[ImGuiCol_DragDropTarget]     = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    colors[ImGuiCol_Border]             = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
}





ImVec4 rgbaToImguiColor(int red, int green, int blue, int alpha) {
    ImVec4 normalizedRGBA;
    
    normalizedRGBA.x = red / 255.0f;
    normalizedRGBA.y = green / 255.0f;
    normalizedRGBA.z = blue / 255.0f;
    normalizedRGBA.w = alpha / 255.0f;
    
    return normalizedRGBA;
}

void SetStyleGray(ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    style->FrameRounding = 4.0f;
    style->GrabRounding = 5.0f;

    style->FrameBorderSize = 1.0f;
    
    // Text
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.97f, 0.97f, 0.97f, 1.00f);
    
    // Frames
    colors[ImGuiCol_FrameBg]            = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
    colors[ImGuiCol_FrameBgActive]      = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
    
    // Windows && Tabs && MenuBar
    colors[ImGuiCol_WindowBg]           = ImVec4{ 0.094f, 0.098f, 0.1f, 1.0f };
	colors[ImGuiCol_ChildBg]            = ImVec4{ 0.094f, 0.098f, 0.1f, 1.0f };
	colors[ImGuiCol_PopupBg]            = ImVec4( 0.090f, 0.090f, 0.090f, 0.95f);
    colors[ImGuiCol_Tab]                = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabHovered]         = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
    colors[ImGuiCol_TabActive]          = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
    colors[ImGuiCol_TabUnfocused]       = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_MenuBarBg]          = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_ScrollbarBg]        = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4{ 0.310f, 0.310f, 0.310f, 1.0f };
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

    // Title
    colors[ImGuiCol_TitleBg]            = ImVec4{ 0.125f, 0.125f, 0.125f, 1.0f };
    colors[ImGuiCol_TitleBgActive]      = ImVec4{ 0.25f, 0.25f, 0.25f, 1.0f };
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    
    // Others
    colors[ImGuiCol_Button]             = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_DragDropTarget]     = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_Border]             = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    colors[ImGuiCol_Header]             = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_CheckMark]          = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

    colors[ImGuiCol_DockingPreview]     = ImVec4(0.8f, 0.8f, 0.8f, 0.7f);
    colors[ImGuiCol_DockingEmptyBg]     = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

}