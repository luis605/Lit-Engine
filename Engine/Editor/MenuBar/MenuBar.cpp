#include <iostream>
#include <vector>
#include <imgui.h>

// Constants
constexpr float BUTTON_PADDING = 30.0f;
constexpr float BUTTON_HEIGHT = 30.0f;
constexpr ImVec2 BUTTON_SIZE = ImVec2(120.0f, BUTTON_HEIGHT); // Assuming 120.0f is the max button width + padding

// Global variables
bool showAppearanceWindow = false;
bool showDebugWindow = false;
bool menuButtonClicked = false;

// Function declarations
void SetStyleHighContrast(ImGuiStyle* style);
void SetStyleGray(ImGuiStyle* style);

void Appearance() {
    ImGui::Begin("Appearance", &showAppearanceWindow);

    ImGui::Text("Themes:");
    bool themeBlue = ImGui::Button("Blue", BUTTON_SIZE);
    bool themeLight = ImGui::Button("Light", BUTTON_SIZE);
    bool themeHighContrast = ImGui::Button("High Contrast", BUTTON_SIZE);
    bool themeGray = ImGui::Button("Gray", BUTTON_SIZE);
    bool themeCustom = ImGui::Button("Custom", BUTTON_SIZE);

    if (themeBlue) ImGui::StyleColorsDark();
    else if (themeLight) ImGui::StyleColorsLight();
    else if (themeHighContrast) SetStyleHighContrast(&ImGui::GetStyle());
    else if (themeGray) SetStyleGray(&ImGui::GetStyle());
    else if (themeCustom) createNewThemeWindowOpen = true;

    ImGui::End();
}

void DebugWindow() {
    if (!showDebugWindow) return;
    ImGui::Begin("Debug", &showDebugWindow);

    if (ImGui::CollapsingHeader("Performance")) {
        ImGui::Indent(15.0f);

        float framerate = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.2f", framerate);
        ImGui::Text("Frame Time: %.2f ms", 1000.0f / framerate);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("Scene Information");
        ImGui::Text("Number of Entities: %lu", entitiesListPregame.size());

        int polygonCount = 0;
        for (const Entity& entity : entitiesListPregame) {
            if (IsModelReady(entity.model)) {
                int vertexCount = 0;
                for (int meshIndex = 0; meshIndex < entity.model.meshCount; ++meshIndex) {
                    vertexCount += entity.model.meshes[meshIndex].vertexCount;
                }
                polygonCount += vertexCount * (entity.hasInstances() ? entity.instances.size() : 1);
            }
        }

        ImGui::Text("Polygon count: %d", polygonCount);
        ImGui::Unindent(15.0f);
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader("Editor Profiler")) {
        ImGui::Indent(15.0f);

        ImGui::Text("Scene Editor %ld ms", sceneEditorProfilerDuration.count());
        ImGui::Text("Assets Explorer %ld ms", assetsExplorerProfilerDuration.count());

        ImGui::Unindent(15.0f);
    }

    ImGui::End();
}

void DrawMenus() {
    if (ImGui::BeginMenu("Project")) {
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            std::cout << "Saving Project...\n";
            SaveProject();
        }

        if (ImGui::MenuItem("Save as", "Ctrl+Shift+S")) {
            std::cout << "Saving Project as...\n";
        }

        if (ImGui::MenuItem("Open", "Ctrl+O")) {
            std::cout << "Opening Project...\n";
            LoadProject(entitiesListPregame, lights, sceneCamera);
        }

        if (ImGui::MenuItem("Export")) {
            SaveProject();
            BuildProject();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::BeginMenu("Preferences")) {
            if (ImGui::MenuItem("Appearance", "Ctrl+Shift+D")) showAppearanceWindow = true;

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Debug")) showDebugWindow = !showDebugWindow;

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
        if (ImGui::MenuItem("Reload Lighting Shader")) {
            std::cout << "\n\n\n\n\nReloading Lighting Shaders\n\n";
            UnloadShader(shader);
            shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
            for (Entity& entity : entitiesListPregame) {
                entity.setShader(shader);
            }

            for (int index = 0; index < NUM_GIZMO_ARROWS; ++index)
                gizmoArrow[index].model.materials[0].shader = shader;

            for (int index = 0; index < NUM_GIZMO_CUBES; ++index)
                gizmoCube[index].model.materials[0].shader = shader;

            gizmoTorus[0].model.materials[0].shader = shader;
        }

        if (ImGui::MenuItem("Reload Skybox Shader")) {
            std::cout << "\n\n\n\n\nReloading Skybox Shaders\n\n";
            InitSkybox();
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
        ImVec2 imagePos = ImVec2(centeredHeight + 10, centeredHeight);


        ImGui::SetCursorPos(imagePos);
        ImGui::Image((ImTextureID)&windowIconTexture, imageSize);

        if (ImGui::IsItemClicked()) menuButtonClicked = true;
        if (menuButtonClicked) ImGui::OpenPopup("Menu");

        if (ImGui::BeginPopup("Menu")) {
            if (ImGui::MenuItem("About", "F1"))  openAboutPage();
            if (ImGui::MenuItem("Manual", "F2")) openManualPage();
            if (ImGui::MenuItem("Exit", "Alt + F4")) {
                menuButtonClicked = false;
                exitWindowRequested = true;
            }
            if (!ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) menuButtonClicked = false;

            ImGui::EndPopup();
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);

        DrawMenus();

        ImGui::PushFont(s_Fonts["Bold"]);

        ImVec2 currentCursorPos = ImGui::GetCursorPos();
        ImVec2 textSize = ImGui::CalcTextSize("Lit Engine");
        ImGui::SetCursorPos(ImVec2(
            ImGui::GetWindowWidth() * 0.5f - textSize.x * 0.5f,
            centeredHeight - textSize.y * 0.5f
            ));

        ImGui::Text("Lit Engine");
        ImGui::SetCursorPos(currentCursorPos);

        ImGui::PopFont();

        float buttonWidth = 30.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - buttonWidth);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.3, .3, .3, .3));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        ImGui::GetStyle().FramePadding.y = originalFramePaddingY;

        if (ImGui::Button("x", ImVec2(buttonWidth, buttonWidth)) || (IsKeyDown(KEY_LEFT_ALT) && IsKeyDown(KEY_F4))) exitWindowRequested = true;

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - buttonWidth * 2);

        if (ImGui::Button(ICON_FA_WINDOW_MAXIMIZE, ImVec2(buttonWidth, buttonWidth))) ToggleMaximization();

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - buttonWidth * 3);

        if (ImGui::Button(ICON_FA_WINDOW_MINIMIZE, ImVec2(buttonWidth, buttonWidth))) MinimizeWindow();

        ImGui::PopStyleColor(4);

        ImGui::SetCursorPosX(imageSize.x + imagePos.x + ImGui::CalcTextSize("File   Edit   Preferences").x + 40);
        ImGui::InvisibleButton("EMPTY", ImVec2(ImGui::GetWindowSize().x - buttonWidth * 3 - ImGui::GetCursorPosX(), 40));
        DraggableWindow();

        ImGui::EndMainMenuBar();
    }

    ImGui::GetStyle().FramePadding.y = originalFramePaddingY;

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
        IsKeyPressed(KEY_D)) showAppearanceWindow = true;

    if (showAppearanceWindow && !movingEditorCamera) Appearance();
    CreateNewTheme();

    if (exitWindowRequested) ExitWindowRequested();

    DebugWindow();
}
