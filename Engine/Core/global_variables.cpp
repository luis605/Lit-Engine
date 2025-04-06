#ifndef GLOBALS_H
#define GLOBALS_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/global_variables.hpp>
#include <chrono>
#include <string>
#include <raylib.h>

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

    std::chrono::milliseconds sceneEditorProfilerDuration;
    std::chrono::milliseconds assetsExplorerProfilerDuration;

    bool showFileExplorer = false;
    bool showSaveThemeWindow = false;
    bool showLoadThemeWindow = false;
    bool createNewThemeWindowOpen = false;
#endif

#endif // GLOBALS_H