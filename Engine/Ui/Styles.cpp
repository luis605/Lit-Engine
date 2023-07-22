#include "../../include_all.h"


std::string to_hex_string(ImU32 color)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(8) << std::hex << color;
    return stream.str();
}


using json = nlohmann::json;

void LoadThemeFromFile(const std::string& filename)
{
    // Step 1: Read the contents of the JSON file into a string
    std::ifstream file(filename);
    std::string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Step 2: Parse the JSON string into a JSON object
    json data = json::parse(json_string);

    // Step 3: Extract the style information from the JSON object and apply it to the ImGui style
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;


    for (const auto& option : data["options"])
    {
        std::string name = option["index"];

        int themes_colors_string_size = sizeof(themes_colors_string) / sizeof(themes_colors_string[0]);
        int option_index = -1;

        for (int index = 0; index < themes_colors_string_size; index++) {
            if (strcmp(themes_colors_string[index], name.c_str()) == 0) {
                option_index = index;
                break;
            }
        }
        
        std::string color_hex = option["color"];
        ImU32 color = std::stoul(color_hex, nullptr, 16);
        colors[themes_colors[option_index]] = ImGui::ColorConvertU32ToFloat4(color);
    }
}


enum FileExplorerType
{
    Save,
    Load,
    SaveLoad
};

string showFileExplorer(const char* folderPath, nlohmann::json_abi_v3_11_2::json fileContent, FileExplorerType type = FileExplorerType::Save)
{
    if (show_save_theme_window || show_load_theme_window)
    {
        // Show file explorer window
        show_file_explorer = true;
        
        ImGui::Begin("File Explorer", &show_file_explorer, ImGuiConfigFlags_ViewportsEnable);

        // Display the folder path
        ImGui::Text("Folder: %s", folderPath);
        

        static char fileName[256] = "";

        // Display the list of files and directories in the folder
        ImGui::BeginChild("FileList", ImVec2(0, 300), true);
        for (auto& file : std::filesystem::directory_iterator(folderPath)) {
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
                string fullFileNamePath = string(folderPath) + string(fileName);
                LoadThemeFromFile(fullFileNamePath);
            }
        }

        ImGui::End();


        // If the user closed the window, reset the other booleans
        if (!show_file_explorer)
        {
            show_save_theme_window = false;
            show_load_theme_window = false;
        }
    }
    return "";
}


ImGuiCol_ theme_create_selected_option = ImGuiCol_Text;

vector<int> new_theme_saved_options;
vector<ImVec4> new_theme_saved_options_color;
void CreateNewTheme()
{
    if (!createNewThemeWindow_open) return;
    ImGui::Begin("Create New Theme", &createNewThemeWindow_open);
    ImGui::Text("Themes options:");

    
    ImGui::Combo("##OptionsCombo", (int*)&theme_create_selected_option, themes_colors_string, IM_ARRAYSIZE(themes_colors_string));
    ImGui::SameLine();
    bool add_to_list = ImGui::Button("Add Selected Option", {110, 30});
    if (add_to_list)
    {
        auto checker_algorithm = std::find(new_theme_saved_options.begin(), new_theme_saved_options.end(), theme_create_selected_option);
        if (checker_algorithm != new_theme_saved_options.end())
        {
            std::cout << "Item already exists" << std::endl;
        }
        else
        {
            std::cout << "Selected Option '" << theme_create_selected_option <<"' Added!" << std::endl;
            new_theme_saved_options.push_back(theme_create_selected_option);
            new_theme_saved_options_color.push_back(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    for (int index = 0; index < new_theme_saved_options.size(); index++)
    {
        int option_index = new_theme_saved_options[index];
        ImGui::Text(themes_colors_string[option_index]);
        ImGui::SameLine();
        string color_picker_name = "##Color Picker - " + to_string(index);
        ImGui::ColorEdit4(color_picker_name.c_str(), (float*)&new_theme_saved_options_color[index], ImGuiColorEditFlags_NoInputs);
    }

    bool save_button = ImGui::Button("Save", {110, 30});
    ImGui::SameLine();
    bool load_button = ImGui::Button("Load", {110, 30});
    ImGui::SameLine();
    bool apply_button = ImGui::Button("Apply/Preview", {110, 30});


    if (save_button)
    {
        show_save_theme_window = true;
    }

    if (show_save_theme_window && !show_load_theme_window)
    {
        nlohmann::json_abi_v3_11_2::json data;

        for (int i = 0; i < new_theme_saved_options.size(); i++)
        {
            int option_index = new_theme_saved_options[i];

            ImU32 color = ImGui::ColorConvertFloat4ToU32(new_theme_saved_options_color[i]);
            std::string color_hex = to_hex_string(color);
            data["options"].push_back({
                {"index", themes_colors_string[option_index]},
                {"color", color_hex}
            });
        }
        

        showFileExplorer(themes_folder.c_str(), data, FileExplorerType::Save);
    }


    if (load_button)
    {
        show_load_theme_window = true;
    }

    if (show_load_theme_window && !show_save_theme_window)
        showFileExplorer(themes_folder.c_str(), "", FileExplorerType::Load);

    if (show_load_theme_window && show_save_theme_window)
        showFileExplorer(themes_folder.c_str(), "", FileExplorerType::SaveLoad);


    if (apply_button)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;
        for (int i = 0; i < new_theme_saved_options.size(); i++)
        {
            int option_index = new_theme_saved_options[i];
            colors[option_index] = new_theme_saved_options_color[i];
        }
    }



    ImGui::End();
}





void SetStyleHighContrast(ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    colors[ImGuiCol_Text] = ImVec4(0.0f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_FrameBg] = rgbaToImguiColor(181, 154, 0, 0.8);
    colors[ImGuiCol_Button] = rgbaToImguiColor(181, 154, 0, 0.8);


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
    
    // Frames
    colors[ImGuiCol_FrameBg]            = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
    colors[ImGuiCol_FrameBgActive]      = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
    
    // Windows && Tabs
    colors[ImGuiCol_WindowBg]           = ImVec4{ 0.094f, 0.098f, 0.1f, 1.0f };
    colors[ImGuiCol_Tab]                = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabHovered]         = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
    colors[ImGuiCol_TabActive]          = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
    colors[ImGuiCol_TabUnfocused]       = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    
    // Title
    colors[ImGuiCol_TitleBg]            = ImVec4{ 0.125f, 0.125f, 0.125f, 1.0f };
    colors[ImGuiCol_TitleBgActive]      = ImVec4{ 0.25f, 0.25f, 0.25f, 1.0f };
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    
    // Others
    colors[ImGuiCol_Button]             = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_DragDropTarget]     = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_Border]             = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

}