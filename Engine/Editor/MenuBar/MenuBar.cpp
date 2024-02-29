#include "../../include_all.h"

bool showAppearanceWindow = false;
bool showDebugWindow = false;

void Appearance()
{
    ImGui::Begin("Appearance", &showAppearanceWindow);

    float button_width = ImGui::CalcTextSize("High Contrast").x + 30;
    float button_height = 30;
    ImVec2 ButtonSize = ImVec2(button_width, button_height);

    ImGui::Text("Themes: ");
    bool themeBlue = ImGui::Button("Blue", ButtonSize);
    bool themeLight = ImGui::Button("Light", ButtonSize);
    bool themeHighContrast = ImGui::Button("High Contrast", ButtonSize);
    bool themeGray = ImGui::Button("Gray", ButtonSize);
    bool themeCustom = ImGui::Button("Custom", ButtonSize);

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

void DebugWindow()
{
    if (!showDebugWindow) return;
    ImGui::Begin("Debug", &showDebugWindow);

    if (ImGui::CollapsingHeader("Performance"))
    {
        ImGui::Indent(15.0f);

        ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.2f ms", 1000.0f / ImGui::GetIO().Framerate);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Separator();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("Scene Information");
        ImGui::Text("Number of Entities: %d", entitiesListPregame.size());

        int polygonCount = 0;
        for (Entity& entity : entitiesListPregame)
        {
            if (IsModelReady(entity.model))
            {
                for (int meshIndex = 0; meshIndex < entity.model.meshCount; meshIndex++)
                {
                    polygonCount += entity.model.meshes[meshIndex].vertexCount;
                }
            }
        }

        ImGui::Text("Polygon count: %d", polygonCount);
        ImGui::Unindent(15.0f);
    }

    if (ImGui::CollapsingHeader("Editor Profiler"))
    {
        ImGui::Indent(15.0f);

        ImGui::Text("Scene Editor %lld ms", sceneEditorProfilerDuration.count());
        ImGui::Text("Assets Explorer %lld ms", assetsExplorerProfilerDuration.count());

        ImGui::Unindent(15.0f);
    }

    ImGui::End();
}

bool isDraggingWindow = false;
Vector2 offset = { 0.0f, 0.0f };
bool menuButtonClicked = false;

void DrawMenus()
{
    if (ImGui::BeginMenu("Project"))
    {
        if (ImGui::MenuItem("Save", "Ctrl+S"))
        {
            cout << "Saving Project..." << endl;
            SaveProject();
        }

        if (ImGui::MenuItem("Save as", "Ctrl+Shift+S"))
        {
            cout << "Saving Project as..." << endl;
        }

        if (ImGui::MenuItem("Open", "Ctrl+O"))
        {
            cout << "Opening Project..." << endl;
            LoadProject(entitiesListPregame, lights, lightsInfo, sceneCamera);
        }

    
        if (ImGui::MenuItem("Preview", "Ctrl+P"))
        {
            // // Can Preview Project
            // close(pipe_fds[0]);
            // can_previewProject = true;
            // ssize_t bytes_written = write(pipe_fds[1], &can_previewProject, sizeof(bool));
            // if (bytes_written != sizeof(bool)) {
            //     std::cerr << "Error writing to the pipe." << std::endl;
            // }
            // close(pipe_fds[1]);

            // // Pass entitiesListPregame to Preview
            // close(pipe_fds_entities[0]);

            // for (Entity entity : entitiesListPregame) {
            //     write(pipe_fds_entities[1], &entity, sizeof(Entity));
            // }

            // close(pipe_fds_entities[1]);

        }

        if (ImGui::MenuItem("Export"))
        {
            SaveProject();
            BuildProject();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::BeginMenu("Preferences"))
        {
            if (ImGui::MenuItem("Appearance", "Ctrl+Shift+D"))
            {
                showAppearanceWindow = true;
            }
            
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Debug"))
        {
            showDebugWindow = !showDebugWindow;
        }

        ImGui::EndMenu();

    }

    if (ImGui::BeginMenu("Debug"))
    {
        if (ImGui::MenuItem("Reload Lighting Shader", ""))
        {
            std::cout << "\n\n\n\n\nReloading Lighting Shaders\n\n";
            UnloadShader(shader);
            shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
            for (Entity& entity : entitiesListPregame) {
                if (entity.hasInstances())
                {
                    entity.setShader(shader);
                }
            }
        }

        if (ImGui::MenuItem("Reload Skybox Shader", ""))
        {
            std::cout << "\n\n\n\n\nReloading Lighting Shaders\n\n";
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

void MenuBar()
{
    float originalFramePaddingY = ImGui::GetStyle().FramePadding.y;
    ImVec2 windowPadding = ImVec2(0, 0);

    ImGui::GetStyle().FramePadding.y = 19.0f;

    float titleBarHeight = ImGui::GetFrameHeight();
    float titlebarVerticalOffset = isWindowMaximized ? -6.0f : 0.0f;

    if (ImGui::BeginMainMenuBar())
    {
        ImVec2 imageSize = ImVec2(40, 40);
        ImVec2 windowSize = ImGui::GetWindowSize();
        float titleBarHeight = ImGui::GetFrameHeight();
        float centeredHeight = (titleBarHeight - imageSize.y) * 0.5f;
        ImVec2 imagePos = ImVec2(centeredHeight + 10, centeredHeight);

        ImGuiWindow* window = ImGui::GetCurrentWindow();

        // Left polygon
        window->DrawList->PathClear();
        window->DrawList->PathLineTo(window->Pos + ImVec2(0, titleBarHeight / 1.5));
        window->DrawList->PathLineTo(window->Pos + ImVec2(75, titleBarHeight / 3.5));
        window->DrawList->PathLineTo(window->Pos + ImVec2(75, titleBarHeight));
        window->DrawList->PathLineTo(window->Pos + ImVec2(0, titleBarHeight));
        window->DrawList->PathLineTo(window->Pos + ImVec2(0, titleBarHeight / 1.5));
        window->DrawList->PathFillConvex(ImColor(50, 50, 50));

        // Middle rectangle
        window->DrawList->PathClear();
        window->DrawList->PathRect(window->Pos + ImVec2(75, titleBarHeight / 3.5), window->Pos + ImVec2(windowSize.x - 75, titleBarHeight));
        window->DrawList->PathFillConvex(ImColor(50, 50, 50));

        // Right polygon
        window->DrawList->PathClear();
        window->DrawList->PathLineTo(window->Pos + ImVec2(windowSize.x - 75, titleBarHeight));
        window->DrawList->PathLineTo(window->Pos + ImVec2(windowSize.x - 75, titleBarHeight / 3.5));
        window->DrawList->PathLineTo(window->Pos + ImVec2(windowSize.x, titleBarHeight / 1.5));
        window->DrawList->PathLineTo(window->Pos + ImVec2(windowSize.x, titleBarHeight));
        window->DrawList->PathLineTo(window->Pos + ImVec2(windowSize.x / 2, titleBarHeight));
        window->DrawList->PathFillConvex(ImColor(50, 50, 50));

        // Clear Path
        window->DrawList->PathClear();

        ImGui::SetCursorPos(imagePos);
        ImGui::Image((ImTextureID)&windowIconTexture, imageSize);

        if (ImGui::IsItemClicked())
        {
            menuButtonClicked = true;
        }

        if (menuButtonClicked) ImGui::OpenPopup("Menu");


        if (ImGui::BeginPopup("Menu"))
        {
            if (ImGui::MenuItem("About", "F1"))
            {
                openAboutPage();
            }

            if (ImGui::MenuItem("Manual", "F2"))
            {
                openManualPage();
            }

            if (ImGui::MenuItem("Exit", "Alt + F4"))
            {
                menuButtonClicked = false;
                exitWindowRequested = true;
            }

            if (!ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                menuButtonClicked = false;
            }
            
            ImGui::EndPopup();
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);

        DrawMenus();

        {
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
        }

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 35);
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.3, .3, .3, .3));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        ImGui::GetStyle().FramePadding.y = originalFramePaddingY;

        if (ImGui::Button("x", ImVec2(30, 30)) || (IsKeyDown(KEY_LEFT_ALT) && IsKeyDown(KEY_F4)))
        {
            exitWindowRequested = true;
        }

        ImGui::PopFont();
        ImGui::PushFont(s_Fonts["ImGui Default"]);

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 35 * 2);

        if (ImGui::Button(ICON_FA_WINDOW_MAXIMIZE, ImVec2(30, 30))) {
            ToggleMaximization();
        }

        
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 35 * 3);

        if (ImGui::Button(ICON_FA_WINDOW_MINIMIZE, ImVec2(30, 30)))
        {
            MinimizeWindow();
        }

        ImGui::PopFont();
        ImGui::PushFont(s_Fonts["Default"]);

        ImGui::PopStyleColor(4);

        ImGui::SetCursorPosX(imageSize.x + imagePos.x + ImGui::CalcTextSize("File   Edit   Preferences").x + 40);
        ImGui::InvisibleButton("EMPTY", ImVec2(ImGui::GetWindowSize().x - 35 * 3 - ImGui::GetCursorPosX(), 40));
        DraggableWindow();


        ImGui::EndMainMenuBar();
    }

    ImGui::GetStyle().FramePadding.y = originalFramePaddingY;

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
        IsKeyPressed(KEY_D)
        )
    {
        showAppearanceWindow = true;
    }

    if (showAppearanceWindow && !movingEditorCamera) Appearance();
    CreateNewTheme();

    if (exitWindowRequested)
        ExitWindowRequested();

    DebugWindow();
}