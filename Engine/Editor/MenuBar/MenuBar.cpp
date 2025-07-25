/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include <Engine/../GameBuilder/builder.hpp>
#include <Engine/Core/Core.hpp>
#include <Engine/Core/Engine.hpp>
#include <Engine/Core/Entity.hpp>
#include <Engine/Core/SaveLoad.hpp>
#include <Engine/Core/global_variables.hpp>
#include <Engine/Editor/MenuBar/MenuBar.hpp>
#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include <Engine/Editor/Styles/Styles.hpp>
#include <extras/IconsFontAwesome6.h>
#include <Engine/Editor/MenuBar/Settings.hpp>

bool showAppearanceWindow = false;
bool showDebugWindow      = false;
bool menuButtonClicked    = false;

void Appearance() {
    ImGui::Begin("Appearance", &showAppearanceWindow);

    ImGui::Text("Themes:");
    bool themeBlue = ImGui::Button("Blue", BUTTON_SIZE);
    bool themeLight = ImGui::Button("Light", BUTTON_SIZE);
    bool themeHighContrast = ImGui::Button("High Contrast", BUTTON_SIZE);
    bool themeGray = ImGui::Button("Gray", BUTTON_SIZE);
    bool themeCustom = ImGui::Button("Custom", BUTTON_SIZE);

    if (themeBlue)
        ImGui::StyleColorsDark();
    else if (themeLight)
        ImGui::StyleColorsLight();
    else if (themeHighContrast)
        SetStyleHighContrast(&ImGui::GetStyle());
    else if (themeGray)
        SetStyleGray(&ImGui::GetStyle());
    else if (themeCustom)
        createNewThemeWindowOpen = true;

    ImGui::End();
}

void DebugWindow() {
    if (!showDebugWindow)
        return;
    ImGui::Begin("Debug", &showDebugWindow);

    if (ImGui::CollapsingHeader("Performance")) {
        ImGui::Indent();

        float framerate = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.2f", framerate);
        ImGui::Text("Frame Time: %.2f ms", 1000.0f / framerate);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("Scene Information");
        ImGui::Text("Number of Entities: %lu", entitiesListPregame.size());

        int vertexCount = 0;
        for (const Entity& entity : entitiesListPregame) {
            if (IsModelValid(entity.model)) {
                int privateVertexCount = 0;
                for (int meshIndex = 0; meshIndex < entity.model.meshCount;
                     ++meshIndex) {
                    privateVertexCount +=
                        entity.model.meshes[meshIndex].vertexCount;
                }
                vertexCount +=
                    privateVertexCount *
                    (entity.hasInstances() ? entity.instances.size() : 1);
            }
        }

        ImGui::Text("Vertex count: %d", vertexCount);
        ImGui::Unindent();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader("Editor Profiler")) {
        ImGui::Indent();

        ImGui::Text("Scene Editor %ld ms", sceneEditorProfilerDuration.count());
        ImGui::Text("Assets Explorer %ld ms",
                    assetsExplorerProfilerDuration.count());

        ImGui::Unindent();
    }

    ImGui::End();
}

void DrawMenus() {
    if (ImGui::BeginMenu("Project")) {
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            TraceLog(LOG_INFO, "Saving project.");
            SaveProject();
        }

        if (ImGui::MenuItem("Save as", "Ctrl+Shift+S")) {
            TraceLog(LOG_INFO, "Saving project as.");
        }

        if (ImGui::MenuItem("Open", "Ctrl+O")) {
            TraceLog(LOG_INFO, "Opening project.");
            LoadProject(entitiesListPregame, lights, sceneEditor.sceneCamera);
        }

        if (ImGui::MenuItem("Export")) {
            SaveProject();
            BuildProject();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::BeginMenu("Preferences")) {
            if (ImGui::MenuItem("Appearance", "Ctrl+Shift+D"))
                showAppearanceWindow = true;

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Settings"))
            showSettingsWindow = true;

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Debug"))
            showDebugWindow = !showDebugWindow;

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
        if (ImGui::MenuItem("Reload Lighting Shader")) {
            shaderManager.m_defaultShader = std::make_shared<Shader>(
                LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl")
            );

            for (Entity& entity : entitiesListPregame) {
                entity.setShader(shaderManager.m_defaultShader);
            }
        }

        if (ImGui::MenuItem("Reload Bloom Shader")) {
            shaderManager.m_verticalBlurShader =
                LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                           "Engine/Lighting/shaders/blurVertical.fs");
            shaderManager.m_horizontalBlurShader =
                LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                           "Engine/Lighting/shaders/blurHorizontal.fs");
            shaderManager.m_verticalBlurShader =
                LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                           "Engine/Lighting/shaders/blurVertical.fs");
        }

        ImGui::EndMenu();
    }
}

void openAboutPage() {
    menuButtonClicked = false;

#ifdef __linux__
    system("xdg-open https://www.litengine.org");
#elif _WIN32
    system("start https://www.litengine.org");
#elif __APPLE__
    system("open https://www.litengine.org");
#endif
}

void openManualPage() {
    menuButtonClicked = false;

#ifdef __linux__
    system("xdg-open https://www.litengine.org/manual");
#elif _WIN32
    system("start https://www.litengine.org/manual");
#elif __APPLE__
    system("open https://www.litengine.org/manual");
#endif
}

void MenuBar() {
    float originalFramePaddingY = ImGui::GetStyle().FramePadding.y;
    float framePaddingY = 19.0f;
    ImVec2 windowPadding = ImVec2(0, 0);

    ImGui::GetStyle().FramePadding.y = framePaddingY;

    float titleBarHeight = ImGui::GetFrameHeight();
    float titlebarVerticalOffset = isWindowMaximized ? -6.0f : 0.0f;

    if (ImGui::BeginMainMenuBar()) {
        ImVec2 imageSize = ImVec2(40, 40);
        ImVec2 windowSize = ImGui::GetWindowSize();
        float titleBarHeight = ImGui::GetFrameHeight();
        float centeredHeight = (titleBarHeight - imageSize.y) * 0.5f;
        ImVec2 imagePos = ImVec2(centeredHeight + 20, centeredHeight);

        ImGui::SetCursorPos(imagePos);
        ImGui::Image((ImTextureID)&windowIconTexture, imageSize);

        if (ImGui::IsItemClicked())
            menuButtonClicked = true;
        if (menuButtonClicked)
            ImGui::OpenPopup("Menu");

        if (ImGui::BeginPopup("Menu")) {
            if (ImGui::MenuItem("About", "F1"))
                openAboutPage();
            if (ImGui::MenuItem("Manual", "F2"))
                openManualPage();
            if (ImGui::MenuItem("Exit", "Alt + F4")) {
                menuButtonClicked = false;
                exitWindowRequested = true;
            }
            if (!ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                menuButtonClicked = false;

            ImGui::EndPopup();
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);

        DrawMenus();

        ImVec2 currentCursorPos = ImGui::GetCursorPos();
        static ImVec2 textSize = ImGui::CalcTextSize("Lit Engine");
        ImGui::SetCursorPos(
            ImVec2(ImGui::GetWindowWidth() * 0.5f - textSize.x * 0.5f,
                   centeredHeight - textSize.y * 0.5f));

        ImGui::Text("Lit Engine");
        ImGui::SetCursorPos(currentCursorPos);

        float buttonWidth = 30.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - buttonWidth);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.3, .3, .3, .3));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        ImGui::GetStyle().FramePadding.y = originalFramePaddingY;

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 10 - buttonWidth);

        if (ImGui::Button(ICON_FA_XMARK, ImVec2(buttonWidth, buttonWidth)) ||
            (IsKeyDown(KEY_LEFT_ALT) && IsKeyDown(KEY_F4)))
            exitWindowRequested = true;

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 20 - buttonWidth * 2);

        if (ImGui::Button(ICON_FA_WINDOW_MAXIMIZE,
                          ImVec2(buttonWidth, buttonWidth)))
            ToggleMaximization();

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 30 - buttonWidth * 3);

        if (ImGui::Button(ICON_FA_WINDOW_MINIMIZE,
                          ImVec2(buttonWidth, buttonWidth)))
            MinimizeWindow();

        ImGui::PopStyleColor(4);

        ImGui::SetCursorPosX(imageSize.x + imagePos.x + 40);
        ImGui::InvisibleButton("EMPTY", ImVec2(ImGui::GetWindowSize().x -
                                                   buttonWidth * 3 -
                                                   ImGui::GetCursorPosX(),
                                               40));
        DraggableWindow();

        ImGui::EndMainMenuBar();
    }

    ImGui::GetStyle().FramePadding.y = originalFramePaddingY;

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
        IsKeyPressed(KEY_D))
        showAppearanceWindow = true;

    if (showAppearanceWindow && !sceneEditor.movingEditorCamera)
        Appearance();
    if (showSettingsWindow && !sceneEditor.movingEditorCamera)
        Settings();

    CreateNewTheme();

    if (exitWindowRequested)
        ExitWindowRequested();

    DebugWindow();
}
