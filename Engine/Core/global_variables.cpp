bool firstTimeGameplay = true;

#ifndef GAME_SHIPPING
    std::string selectedGameObjectType;
    std::string themesFolder = "project/themes/";

    bool inGamePreview = false;
    bool canDuplicateEntity = true;
    bool showObjectTypePopup = false;

    Texture2D runTexture;
    Texture2D pauseTexture;
    Texture2D viewportTexture;

    RenderTexture2D viewportRenderTexture;

    Rectangle viewportRectangle;

    float sceneEditorWindowWidth;
    float sceneEditorWindowHeight;
    float sceneEditorWindowX;
    float sceneEditorWindowY;

    std::chrono::milliseconds sceneEditorProfilerDuration;
    std::chrono::milliseconds assetsExplorerProfilerDuration;

    bool showFileExplorer = false;
    bool showSaveThemeWindow = false;
    bool showLoadThemeWindow = false;
    bool createNewThemeWindowOpen = false;

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