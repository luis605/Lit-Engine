#include "../include_all.h"
#include "Core.h"

#define STRESS_TEST false
#if STRESS_TEST
    #include "stressTest.cpp"
#endif

void InitLitWindow() {
    SetTraceLogLevel(LOG_WARNING);

    // Open a window to get the screen resolution
    InitWindow(windowWidth, windowHeight, "Lit Engine - Initializing");

    windowWidth = GetMonitorWidth(0) * 0.95;
    windowHeight = GetMonitorHeight(0) * 0.9;
    CloseWindow();

    // Window
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED);
    InitWindow(windowWidth, windowHeight, "Lit Engine");

    SetExitKey(KEY_NULL);
}

void LoadTextures() {
    folderTexture = LoadTexture("assets/images/folder.png");
    imageTexture = LoadTexture("assets/images/image_file_type.png");
    cppTexture = LoadTexture("assets/images/cpp_file_type.png");
    pythonTexture = LoadTexture("assets/images/python_file_type.png");
    modelTexture = LoadTexture("assets/images/model_file_type.png");
    materialTexture = LoadTexture("assets/images/material_file_type.png");
    emptyTexture = LoadTexture("assets/images/empty_file_file_type.png");
    runTexture = LoadTexture("assets/images/run_game.png");
    pauseTexture = LoadTexture("assets/images/pause_game.png");
    saveTexture = LoadTexture("assets/images/save_file.png");
    hotReloadTexture = LoadTexture("assets/images/hot_reload.png");
    lightTexture = LoadTexture("assets/images/light_bulb.png");
    windowIconImage = LoadImage("assets/images/window_icon.png");
    windowIconTexture = LoadTextureFromImage(windowIconImage);
    downsamplerTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    upsamplerTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    SetWindowIcon(windowIconImage);
}

void InitImGui() {
    rlImGuiSetup(true);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    SetStyleGray(&ImGui::GetStyle());

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowMinSize.x = 370.0f;

    fs::path fontPath = GetWorkingDirectory();
    fontPath += "/assets/fonts/";

    float fontSize = 19.0f * io.FontGlobalScale;

    auto addFont = [&](const std::string& fontName, const std::string& fileName, float sizeModifier = 0) {
        s_Fonts[fontName] = io.Fonts->AddFontFromFileTTF((fontPath.string() + fileName).c_str(), fontSize + sizeModifier);
    };

    addFont("ImGui Default", "NotoSans-Medium.ttf");
    addFont("Default", "NotoSans-Medium.ttf");
    addFont("Bold", "NotoSans-Bold.ttf", 4);

    io.Fonts->Fonts[0]->FontSize = fontSize;

    rlImGuiReloadFonts();
}

void InitShaders() {
    shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl"); // LoadShaderFromMemory(lightingVert, lightingFrag);
    instancingShader = LoadShaderFromMemory(lightingVert, lightingFrag);
    downsamplerShader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/downsampler.glsl"); // LoadShaderFromMemory(lightingVert, downsamplerFrag);
    upsamplerShader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/upsampler.glsl"); // LoadShaderFromMemory(lightingVert, upsamplerFrag);
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

    InitSkybox();
    Py_Initialize();
    InitImGui();
    LoadTextures();
    InitShaders();
    InitLighting();
    InitGizmo();
    InitEditorCamera();
    InitCodeEditor();
    InitRenderModelPreviewer();

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
        ImGui::Begin(ICON_FA_VIDEO " Scene Editor", NULL);
        int editor_camera = EditorCamera();
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::PopFont();

        rlImGuiEnd();
        
        EndDrawing();
    }
}

void CleanUp()
{
    std::cout << "Exiting..." << std::endl;
    //kill(-pid, SIGTERM);

    inGamePreview = false;
    firstTimeGameplay = false;

    for (Entity &entity : entitiesListPregame)
        entity.remove();

    entitiesListPregame.clear();

    UnloadShader(shader);
    UnloadImage(windowIconImage);
    UnloadTexture(folderTexture);
    UnloadTexture(imageTexture);
    UnloadTexture(cppTexture);
    UnloadTexture(pythonTexture);
    UnloadTexture(modelTexture);
    UnloadTexture(emptyTexture);
    UnloadTexture(runTexture);
    UnloadTexture(pauseTexture);
    UnloadTexture(saveTexture);
    UnloadTexture(hotReloadTexture);
    UnloadTexture(lightTexture);
    UnloadTexture(windowIconTexture);

    for (auto it = modelsIcons.begin(); it != modelsIcons.end(); ++it)
    {
        UnloadTexture(it->second);
    }
    modelsIcons.clear();

    lights.clear();
    lightsInfo.clear();

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
