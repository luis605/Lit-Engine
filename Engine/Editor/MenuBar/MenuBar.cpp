#include "../../include_all.h"

bool appearance_window_enabled = false;


void Appearance()
{
    ImGui::Begin("Appearance", &appearance_window_enabled);

    float button_width = ImGui::CalcTextSize("High Contrast").x + 30;
    float button_height = 30;
    ImVec2 ButtonSize = ImVec2(button_width, button_height);

    ImGui::Text("Themes: ");
    bool theme_blue = ImGui::Button("Blue", ButtonSize);
    bool theme_light = ImGui::Button("Light", ButtonSize);
    bool theme_high_contrast = ImGui::Button("High Contrast", ButtonSize);
    bool theme_gray = ImGui::Button("Gray", ButtonSize);
    bool theme_custom = ImGui::Button("Custom", ButtonSize);

    if (theme_blue)
        ImGui::StyleColorsDark();
    else if (theme_light)
        ImGui::StyleColorsLight();
    else if (theme_high_contrast)
        SetStyleHighContrast(&ImGui::GetStyle());
    else if (theme_gray)
        SetStyleGray(&ImGui::GetStyle());
    else if (theme_custom)
        createNewThemeWindow_open = true;

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
            LoadProject(entities_list_pregame, lights, lights_info, scene_camera);
        }

    
        if (ImGui::MenuItem("Preview", "Ctrl+P"))
        {
            // Can Preview Project
            close(pipe_fds[0]);
            can_previewProject = true;
            ssize_t bytes_written = write(pipe_fds[1], &can_previewProject, sizeof(bool));
            if (bytes_written != sizeof(bool)) {
                std::cerr << "Error writing to the pipe." << std::endl;
            }
            close(pipe_fds[1]);

            // Pass entities_list_pregame to Preview
            close(pipe_fds_entities[0]);

            for (Entity entity : entities_list_pregame) {
                write(pipe_fds_entities[1], &entity, sizeof(Entity));
            }

            close(pipe_fds_entities[1]);

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
        // Add menu items for "Edit" menu here
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Preferences"))
    {
        if (ImGui::MenuItem("Appearance", "Ctrl+Shift+D"))
        {
            appearance_window_enabled = true;
        }
        
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug/Developers Options"))
    {
        if (ImGui::MenuItem("Reload Lighting Shader", ""))
        {
            std::cout << "\n\n\n\n\nReloading Lighting Shaders\n\n";
            UnloadShader(shader);
            shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
            for (Entity& entity : entities_list_pregame) {
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
        ImGui::Image((ImTextureID)&window_icon_texture, imageSize);

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


    if (IsKeyPressed(KEY_F1))
    {
        openAboutPage();
    }

    if (IsKeyPressed(KEY_F2))
    {
        openManualPage();
    }

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
        IsKeyPressed(KEY_D)
        )
    {
        appearance_window_enabled = true;
    }

    if (appearance_window_enabled) Appearance();
    CreateNewTheme();

    if (exitWindowRequested)
        ExitWindowRequested();
}