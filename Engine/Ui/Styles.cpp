#include "../../include_all.h"



const char* option_names[] = {
    "Text",
    "TextDisabled",
    "WindowBg",
    "ChildBg",
    "PopupBg",
    "Border",
    "BorderShadow",
    "FrameBg",
    "FrameBgHovered",
    "FrameBgActive",
    "TitleBg",
    "TitleBgActive",
    "TitleBgCollapsed",
    "MenuBarBg",
    "ScrollbarBg",
    "ScrollbarGrab",
    "ScrollbarGrabHovered",
    "ScrollbarGrabActive",
    "CheckMark",
    "SliderGrab",
    "SliderGrabActive",
    "Button",
    "ButtonHovered",
    "ButtonActive",
    "Header",
    "HeaderHovered",
    "HeaderActive",
    "Separator",
    "SeparatorHovered",
    "SeparatorActive",
    "ResizeGrip",
    "ResizeGripHovered",
    "ResizeGripActive",
    "Tab",
    "TabHovered",
    "TabActive",
    "TabUnfocused",
    "TabUnfocusedActive",
    "DockingPreview",
    "DockingEmptyBg",
    "PlotLines",
    "PlotLinesHovered",
    "PlotHistogram",
    "PlotHistogramHovered",
    "TableHeaderBg",
    "TableBorderStrong",
    "TableBorderLight",
    "TableRowBg",
    "TableRowBgAlt",
    "TextSelectedBg",
    "DragDropTarget",
    "NavHighlight",
    "NavWindowingHighlight",
    "NavWindowingDimBg",
    "ModalWindowDimBg"
};

ImGuiCol_ theme_create_selected_option = ImGuiCol_Text;

vector<int> new_theme_saved_options;
vector<ImVec4> new_theme_saved_options_color;
void CreateNewTheme()
{
    if (!createNewThemeWindow_open) return;
    ImGui::Begin("Create New Theme", &createNewThemeWindow_open);
    ImGui::Text("Themes options:");

    
    ImGui::Combo("##OptionsCombo", (int*)&theme_create_selected_option, option_names, IM_ARRAYSIZE(option_names));
    ImGui::SameLine();
    bool add_to_list = ImGui::Button("Add Selected Option", {80, 40});
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
        ImGui::Text(option_names[option_index]);
        ImGui::SameLine();
        string color_picker_name = "##Color Picker - " + to_string(index);
        ImGui::ColorEdit4(color_picker_name.c_str(), (float*)&new_theme_saved_options_color[index], ImGuiColorEditFlags_NoInputs);
    }

    bool save_button = false;
    bool apply_button = ImGui::Button("Apply/Preview", {80, 40});


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


void SetStyleGray(ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    style->FrameRounding = 2.0f;
    style->FrameBorderSize = 1.0f;
    
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);


}