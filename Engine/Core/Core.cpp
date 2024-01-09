#include "../include_all.h"
#include "Core.h"

#define STRESS_TEST false
#if STRESS_TEST
    #include "stressTest.cpp"
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void InitLitWindow() {
    SetTraceLogLevel(LOG_WARNING);

    InitWindow(windowWidth, windowHeight, "Lit Engine - INITIALISING");

    windowWidth = GetMonitorWidth(0) * 0.95;
    windowHeight = GetMonitorHeight(0) * 0.9;    
    CloseWindow();


    // Window
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED);
    InitWindow(windowWidth, windowHeight, "Lit Engine");

    SetExitKey(KEY_NULL);
}

void LoadTextures() {
    folder_texture = LoadTexture("assets/images/folder.png");
    image_texture = LoadTexture("assets/images/image_file_type.png");
    cpp_texture = LoadTexture("assets/images/cpp_file_type.png");
    python_texture = LoadTexture("assets/images/python_file_type.png");
    model_texture = LoadTexture("assets/images/model_file_type.png");
    empty_texture = LoadTexture("assets/images/empty_file_file_type.png");
    run_texture = LoadTexture("assets/images/run_game.png");
    pause_texture = LoadTexture("assets/images/pause_game.png");
    save_texture = LoadTexture("assets/images/save_file.png");
    hot_reload_texture = LoadTexture("assets/images/hot_reload.png");
    light_texture = LoadTexture("assets/images/light_bulb.png");
    window_icon_image = LoadImage("assets/images/window_icon.png");
    window_icon_texture = LoadTextureFromImage(window_icon_image);

    ImageFormat(&window_icon_image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    SetWindowIcon(window_icon_image);

    downsamplerTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    upsamplerTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
}

void InitImGui() {
    rlImGuiSetup(true);
    ImGui::CreateContext();

    io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    SetStyleGray(&ImGui::GetStyle());

    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowMinSize.x = 370.0f;

    std::string fontPath = GetWorkingDirectory();
    fontPath += "/assets/fonts/";

    float fontSize = 19.0f * io->FontGlobalScale;

    ImFont *defaultFont = io->Fonts->Fonts[0];
    s_Fonts["ImGui Default"] = defaultFont;
    s_Fonts["Default"] = io->Fonts->AddFontFromFileTTF((fontPath + "NotoSans-Medium.ttf").c_str(), fontSize);
    s_Fonts["Bold"] = io->Fonts->AddFontFromFileTTF((fontPath + "NotoSans-Bold.ttf").c_str(), fontSize + 4);
    s_Fonts["Italic"] = io->Fonts->AddFontFromFileTTF((fontPath + "NotoSans-Italic.ttf").c_str(), fontSize);
    s_Fonts["FontAwesome"] = io->Fonts->AddFontFromFileTTF((fontPath + "fontawesome-webfont.ttf").c_str(), fontSize);

    rlImGuiReloadFonts();
}

void InitShaders() {
    shader = LoadShaderFromMemory(lightingVert, lightingFrag); // LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    instancing_shader = LoadShaderFromMemory(lightingVert, lightingFrag);
    downsamplerShader = LoadShaderFromMemory(lightingVert, downsamplerFrag); // LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/downsampler.glsl");
    upsamplerShader = LoadShaderFromMemory(lightingVert, upsamplerFrag); // LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/upsampler.glsl");
}

void InitCodeEditor() {
    code.resize(10);
    auto lang = TextEditor::LanguageDefinition::Python();
    editor.SetLanguageDefinition(lang);
}

void Startup()
{
    InitLitWindow();

    // Face Culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    SetupPhysicsWorld();
    InitSkybox();
    Py_Initialize();
    InitImGui();
    LoadTextures();
    InitGizmo();
    InitEditorCamera();
    InitCodeEditor();
    InitShaders();
    InitLighting();

    #if STRESS_TEST
        InitStressTest();
    #endif
}

void EngineMainLoop()
{
    while ((!exitWindow))
    {
        if (WindowShouldClose()) {
            exitWindowRequested = true;
        }

        BeginDrawing();

        ClearBackground(DARKGRAY);

        rlImGuiBegin();

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::DockSpaceOverViewport(viewport);

        ImGui::PushFont(s_Fonts["Default"]);

        MenuBar();

        AssetsExplorer();

        CodeEditor();

        EntitiesList();

        Inspector();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene Editor", NULL);
        int editor_camera = EditorCamera();
        ImGui::End();
        ImGui::PopStyleVar();

        AddEntity();

        ImGui::PopFont();

        rlImGuiEnd();

        DrawFPS(windowWidth * .9, windowHeight * .1);

        EndDrawing();
    }
}

void CleanUp()
{
    std::cout << "Exiting..." << std::endl;
    //kill(-pid, SIGTERM);

    in_game_preview = false;
    first_time_gameplay = false;

    for (Entity &entity : entities_list_pregame)
        entity.remove();

    entities_list_pregame.clear();

    UnloadShader(shader);
    UnloadImage(window_icon_image);
    UnloadTexture(folder_texture);
    UnloadTexture(image_texture);
    UnloadTexture(cpp_texture);
    UnloadTexture(python_texture);
    UnloadTexture(model_texture);
    UnloadTexture(empty_texture);
    UnloadTexture(run_texture);
    UnloadTexture(pause_texture);
    UnloadTexture(save_texture);
    UnloadTexture(hot_reload_texture);
    UnloadTexture(light_texture);
    UnloadTexture(window_icon_texture);

    for (auto it = models_icons.begin(); it != models_icons.end(); ++it)
    {
        UnloadTexture(it->second);
    }
    models_icons.clear();

    lights.clear();
    lights_info.clear();

    rlImGuiShutdown();
    CloseWindow();
}

void DraggableWindow()
{
    bool isTitleBarHovered = ImGui::IsItemHovered();
    ImVec2 currentMousePos = ImGui::GetMousePos();
    ImVec2 mouseDelta = ImVec2(currentMousePos.x - lastMousePosition.x, currentMousePos.y - lastMousePosition.y);

    float moveThreshold = 3.f;

    if (isTitleBarHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (!isDragging && (fabs(mouseDelta.x) > moveThreshold || fabs(mouseDelta.y) > moveThreshold))
        {
            isDragging = true;
            ImVec2 windowPos = ImGui::GetWindowPos();
            windowOriginalPos = ImVec2(currentMousePos.x - windowPos.x, currentMousePos.y - windowPos.y);
        }
    }
    else if (isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDragging = false;
    }

    if (isDragging)
    {
        if (isWindowMaximized)
            ToggleMaximization();

        ImVec2 newPosition = ImVec2(ImGui::GetMousePos().x - windowOriginalPos.x, ImGui::GetMousePos().y - windowOriginalPos.y);
        float smoothFactor = 0.3f;
        newPosition.x = lerp(ImGui::GetWindowPos().x, newPosition.x * 2.f, smoothFactor);
        newPosition.y = lerp(ImGui::GetWindowPos().y, newPosition.y * 2.f, smoothFactor);
        std::cout << "New position: " << newPosition.x << ", " << newPosition.y << std::endl;
        SetWindowPosition(newPosition.x, newPosition.y);
    }

    lastMousePosition = currentMousePos;
}

void ToggleMaximization()
{
    if (isWindowMaximized) RestoreWindow();
    else                   MaximizeWindow();
    
    isWindowMaximized = !isWindowMaximized;
}

static int currentExitMenuButton = 1;


void ExitWindowRequested()
{
    const ImVec2 windowSize(200, 90);
    const ImVec2 windowPos((GetScreenWidth() - windowSize.x) * 0.5f, (GetScreenHeight() - windowSize.y) * 0.5f);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoResize;

    ImGui::SetNextWindowPos(windowPos);
    ImGui::SetNextWindowSize(windowSize);

    ImGui::OpenPopup("Are you sure you want to exit?");

    if (ImGui::BeginPopupModal("Are you sure you want to exit?", nullptr, windowFlags))
    {
        ImGui::Separator();

        ImVec2 buttonSize(100, 30);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));

        if (currentExitMenuButton == 0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6, 0.6, 0.6, 1));

        if (ImGui::Button("Yes", buttonSize) || (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && currentExitMenuButton == 0)) exitWindow = true;
        ImGui::PopStyleColor(1);

        if (currentExitMenuButton == 0) ImGui::PopStyleColor(1);

        ImGui::SameLine();

        if (currentExitMenuButton == 1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6, 0.6, 0.6, 1));

        if (ImGui::Button("No", buttonSize) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)) || (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && currentExitMenuButton == 1))
        {
            exitWindowRequested = false;
        }

        if (currentExitMenuButton == 1)
            ImGui::PopStyleColor(1);

        // Update selected button based on arrow key input
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow)))
        {
            currentExitMenuButton = (currentExitMenuButton + 1) % 2; // Circular navigation
        }
        else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow)))
        {
            currentExitMenuButton = (currentExitMenuButton - 1 + 2) % 2; // Circular navigation
        }

        ImGui::EndPopup();
    }
}
