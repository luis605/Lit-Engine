#ifndef GLOBALS_H
#define GLOBALS_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <chrono>
#include <string>
#include <raylib.h>

extern bool firstTimeGameplay;

#ifndef GAME_SHIPPING
    extern std::string selectedGameObjectType;
    extern std::string themesFolder;
    extern std::chrono::milliseconds sceneEditorProfilerDuration;
    extern std::chrono::milliseconds assetsExplorerProfilerDuration;

    extern bool inGamePreview;
    extern bool canDuplicateEntity;
    extern bool showObjectTypePopup;

    extern Texture2D runTexture;
    extern Texture2D pauseTexture;
    extern Texture2D viewportTexture;
    extern RenderTexture2D viewportRenderTexture;
    extern Rectangle viewportRectangle;

    extern bool showFileExplorer;
    extern bool showSaveThemeWindow;
    extern bool showLoadThemeWindow;
    extern bool createNewThemeWindowOpen;
#endif

#endif // GLOBALS_H