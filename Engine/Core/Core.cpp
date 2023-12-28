#include "../include_all.h"
#include "Core.h"

#define STRESS_TEST false
#if STRESS_TEST
    #include "stressTest.cpp"
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/*
void segfault_sigaction(int signal, siginfo_t *si, void *arg)
{
    printf("Safe exit enabled by segfault at address %p\n", si->si_addr);
    kill(-pid, SIGTERM);
    exit(0);
}
*/

void Startup()
{
/*
    // Core - Safety
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags   = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
*/

    // Raylib
    SetTraceLogLevel(LOG_WARNING);

    InitWindow(windowWidth, windowHeight, "Lit Engine - INITIALISING");

    windowWidth = GetMonitorWidth(0) * 0.95;
    windowHeight = GetMonitorHeight(0) * 0.9;    
    CloseWindow();


    // Window
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED);
    InitWindow(windowWidth, windowHeight, "Lit Engine");

    SetExitKey(KEY_NULL);

    // Face Culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Physics
    SetupPhysicsWorld();
    
    // Skybox
    InitSkybox();

    // Python
    Py_Initialize();

    // ImGui
    rlImGuiSetup(true);

    ImGui::CreateContext();

    io = &ImGui::GetIO();

    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    SetStyleGray(&ImGui::GetStyle());

    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowMinSize.x = 370.0f;

    std::string fontPath = GetWorkingDirectory();
    fontPath += "/assets/fonts/";

    float fontSize = 18.0f * io->FontGlobalScale;

    ImFont *defaultFont = io->Fonts->Fonts[0];
    s_Fonts["ImGui Default"] = defaultFont;
    s_Fonts["Default"] = io->Fonts->AddFontFromFileTTF((fontPath + "NotoSans-Medium.ttf").c_str(), fontSize);
    s_Fonts["Bold"] = io->Fonts->AddFontFromFileTTF((fontPath + "NotoSans-Bold.ttf").c_str(), fontSize + 4);
    s_Fonts["Italic"] = io->Fonts->AddFontFromFileTTF((fontPath + "NotoSans-Italic.ttf").c_str(), fontSize);
    s_Fonts["FontAwesome"] = io->Fonts->AddFontFromFileTTF((fontPath + "fontawesome-webfont.ttf").c_str(), fontSize);

    rlImGuiReloadFonts();

    // Textures
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

    // Code Editor
    code.resize(10);
    auto lang = TextEditor::LanguageDefinition::CPlusPlus();

    // Gizmo
    InitGizmo();
    
    // Editor Camera
    InitEditorCamera();

    selected_entity = { 0 };
    selected_light = { 0 };

    // Shaders
    shader = LoadShaderFromMemory(lightingVert, lightingFrag); // LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    instancing_shader = LoadShaderFromMemory(lightingVert, lightingFrag);
    downsamplerShader = LoadShaderFromMemory(lightingVert, downsamplerFrag); // LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/downsampler.glsl");
    upsamplerShader = LoadShaderFromMemory(lightingVert, upsamplerFrag); // LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/upsampler.glsl");
    InitLighting();

    downsamplerTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    upsamplerTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    mainThreadId = std::this_thread::get_id();

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


        // updateEntitiesList(entities_list, entities_list_pregame);

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

    // for (Entity &entity : entities_list_pregame)
    //     entity.remove();

    // entities_list_pregame.clear();

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

Vector2 GetGlobalMousePosition()
{
    ImVec2 mousePosition = ImGui::GetMousePos();
    return {static_cast<float>(mousePosition.x), static_cast<float>(mousePosition.y)};
}

void DraggableWindow()
{
    bool isTitleBarHovered = ImGui::IsItemHovered();

    if (isTitleBarHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        isDragging = true;
    }
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDragging = false;
    }

    if (isDragging)
    {
        if (isWindowMaximized)
            ToggleMaximization();

        ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;

        Vector2 currentWindowPos = GetWindowPosition();
        SetWindowPosition(static_cast<int>(currentWindowPos.x + mouseDelta.x),
                          static_cast<int>(currentWindowPos.y + mouseDelta.y));
    }
}

void ToggleMaximization()
{
    if (!isWindowMaximized)
    {

        MaximizeWindow();
        isWindowMaximized = true;
    }
    else
    {
        RestoreWindow();
        isWindowMaximized = false;
    }
}

void ExitWindowRequested()
{
    const ImVec2 windowSize(260, 65);
    const ImVec2 windowPos((GetScreenWidth() - windowSize.x) * 0.5f, (GetScreenHeight() - windowSize.y) / 2.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoResize;

    ImGui::SetNextWindowPos(windowPos);
    ImGui::SetNextWindowSize({200, 90});

    if (true) {
        ImGui::OpenPopup("Do you want to quit?");
    }
    
    if (ImGui::BeginPopupModal("Do you want to quit?")){

        ImGui::Separator();

        ImVec2 buttonSize = ImVec2(100, 30);

        const float posX = (windowSize.x - ImGui::CalcTextSize("Yes").x) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
        if (ImGui::Button("Yes", buttonSize))
        {
            exitWindow = true;
            std::cout << "QUITING\n";
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        const float posX2 = (posX + ImGui::CalcTextSize("Yes").x) + 20;
        ImGui::SetCursorPosX(posX2);
        if (ImGui::Button("No", buttonSize))
        {
            exitWindowRequested = false;
        }

        ImGui::EndPopup();
    }
}