#include "../include_all.h"


pid_t pid;
int pipe_fds[2];
int pipe_fds_entities[2];
int pipe_fds_lights[2];

#ifndef GAME_SHIPPING
    /* Not specific */
    char* selected_game_object_type = "";

    /* Entities List */
    bool in_game_preview = false;

    Texture2D run_texture;
    Texture2D pause_texture;

    int listViewExScrollIndex = 0;
    int listViewExActive = 0;
    int listViewExFocus = 0;
    vector<char*> listViewExList;
    vector<char*> listViewExListTypes;

    bool canAddEntity = false;

    vector<string> objectNames;
    char name[256] = { 0 };

    float entity_create_scale = 1;
    ImVec4 entity_create_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    bool is_create_entity_a_child = false;

    bool showNextTime = true;
    bool create = false;


    /* Code Editor [IDE] */
    string code;
    string code_editor_script_path;
    TextEditor editor;

    /* Scene Editor */
    RenderTexture2D renderTexture;
    Texture2D texture;
    Rectangle rectangle = { screenWidth*.2f, screenHeight*.2f, static_cast<float>(texture.width), static_cast<float>(texture.height) };



    // ImGui Window Info
    float sceneEditorWindowWidth;
    float sceneEditorWindowHeight;
    float sceneEditorWindowX;
    float sceneEditorWindowY;
#endif



/* RunGame */
LitCamera camera;

pybind11::object export_camera()
{
    pybind11::module m("raylib_camera");
    pybind11::class_<LitCamera>(m, "LitCamera")
        .def(pybind11::init<>())
        .def_readwrite("position", &LitCamera::position)
        .def_readwrite("front", &LitCamera::front)
        .def_readwrite("target", &LitCamera::target)
        .def_readwrite("up", &LitCamera::up)
        .def_readwrite("fovy", &LitCamera::fovy)
        .def_readwrite("projection", &LitCamera::projection);
    pybind11::object camera_obj = pybind11::cast(camera);
    camera_obj.attr("__class__") = m.attr("LitCamera");
    return camera_obj;
}

bool first_time_gameplay = true;


#ifndef GAME_SHIPPING
    /* Themes */
    // Create theme
    bool createNewThemeWindow_open = false;

    // Load && Save
    string themes_folder = "project/themes/";
    bool show_file_explorer = false;
    bool show_save_theme_window = false;
    bool show_load_theme_window = false;

    const int themes_colors[] = {
        ImGuiCol_Text,
        ImGuiCol_TextDisabled,
        ImGuiCol_WindowBg,              
        ImGuiCol_ChildBg,               
        ImGuiCol_PopupBg,               
        ImGuiCol_Border,
        ImGuiCol_BorderShadow,
        ImGuiCol_FrameBg,               
        ImGuiCol_FrameBgHovered,
        ImGuiCol_FrameBgActive,
        ImGuiCol_TitleBg,
        ImGuiCol_TitleBgActive,
        ImGuiCol_TitleBgCollapsed,
        ImGuiCol_MenuBarBg,
        ImGuiCol_ScrollbarBg,
        ImGuiCol_ScrollbarGrab,
        ImGuiCol_ScrollbarGrabHovered,
        ImGuiCol_ScrollbarGrabActive,
        ImGuiCol_CheckMark,
        ImGuiCol_SliderGrab,
        ImGuiCol_SliderGrabActive,
        ImGuiCol_Button,
        ImGuiCol_ButtonHovered,
        ImGuiCol_ButtonActive,
        ImGuiCol_Header,                
        ImGuiCol_HeaderHovered,
        ImGuiCol_HeaderActive,
        ImGuiCol_Separator,
        ImGuiCol_SeparatorHovered,
        ImGuiCol_SeparatorActive,
        ImGuiCol_ResizeGrip,            
        ImGuiCol_ResizeGripHovered,
        ImGuiCol_ResizeGripActive,
        ImGuiCol_Tab,                   
        ImGuiCol_TabHovered,
        ImGuiCol_TabActive,
        ImGuiCol_TabUnfocused,
        ImGuiCol_TabUnfocusedActive,
        ImGuiCol_DockingPreview,        
        ImGuiCol_DockingEmptyBg,        
        ImGuiCol_PlotLines,
        ImGuiCol_PlotLinesHovered,
        ImGuiCol_PlotHistogram,
        ImGuiCol_PlotHistogramHovered,
        ImGuiCol_TableHeaderBg,         
        ImGuiCol_TableBorderStrong,     
        ImGuiCol_TableBorderLight,      
        ImGuiCol_TableRowBg,            
        ImGuiCol_TableRowBgAlt,         
        ImGuiCol_TextSelectedBg,
        ImGuiCol_DragDropTarget,        
        ImGuiCol_NavHighlight,          
        ImGuiCol_NavWindowingHighlight, 
        ImGuiCol_NavWindowingDimBg,     
        ImGuiCol_ModalWindowDimBg,
        ImGuiCol_COUNT
    };


    const char* themes_colors_string[] = {
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
#endif