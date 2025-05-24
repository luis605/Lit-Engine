/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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
    #include <Engine/Core/Textures.hpp>

    std::string selectedGameObjectType;
    std::string themesFolder = "project/themes/";

    bool inGamePreview = false;
    bool canDuplicateEntity = true;
    bool showObjectTypePopup = false;

    Texture2D runTexture;
    Texture2D pauseTexture;
    Texture2D viewportTexture;

    GBuffer viewportMRT;

    Rectangle viewportRectangle;

    std::chrono::milliseconds sceneEditorProfilerDuration;
    std::chrono::milliseconds assetsExplorerProfilerDuration;

    bool showFileExplorer = false;
    bool showSaveThemeWindow = false;
    bool showLoadThemeWindow = false;
    bool createNewThemeWindowOpen = false;
#endif

#endif // GLOBALS_H