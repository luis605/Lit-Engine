#include "../include_all.h"
#include "Core.h"

#define MAX(a, b) ((a)>(b)? (a) : (b))
#define MIN(a, b) ((a)<(b)? (a) : (b))


void Startup()
{
    // Raylib
    SetTargetFPS(10000);
    SetTraceLogLevel(LOG_WARNING);
    SetExitKey(KEY_NULL);

    // Window
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNDECORATED);
    InitWindow(windowWidth, windowHeight, "Lit Engine");

    // Face Culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Python
    Py_Initialize();



    // ImGui
    rlImGuiSetup(true);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    SetStyleGray(&ImGui::GetStyle());

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowMinSize.x = 370.0f;



    // ImFontConfig fontConfig;
    // fontConfig.FontDataOwnedByAtlas = false;

    // std::string fontPath = GetWorkingDirectory();
    // fontPath += "/assets/fonts/Poppins-Regular.ttf";
    // std::cout << fontPath << std::endl;

    // ImFont* robotoFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 20.0f, &fontConfig);
    // io.FontDefault = robotoFont;
    // io.Fonts->Build();
    


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
    window_icon_texture = LoadTexture("assets/images/window_icon.png");

    window_icon_image = LoadImage("assets/images/window_icon.png");

    ImageFormat(&window_icon_image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    SetWindowIcon(window_icon_image);
    

    // Code Editor
    code.resize(100000);
	auto lang = TextEditor::LanguageDefinition::CPlusPlus();

    // Gizmo
    for (int index = 0; index < sizeof(gizmo_arrow) / sizeof(gizmo_arrow[0]) + 1; index++)
        gizmo_arrow[index].model = LoadModel("assets/models/gizmo/arrow.obj");    

    for (int index = 0; index < (sizeof(gizmo_taurus) / sizeof(gizmo_taurus[0])) + 1; index++)
        gizmo_taurus[index] = LoadModel("assets/models/gizmo/taurus.obj");

    // Editor Camera
    InitEditorCamera();

    selected_entity = &Entity();
    selected_light = &Light();

    // Shaders
    shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    InitLighting();

    // Physics
    // InitPhysx();
}

void EngineMainLoop()
{
    while ((!exitWindow) && (!WindowShouldClose()))
    {


        BeginDrawing();

            ClearBackground(DARKGRAY);

            rlImGuiBegin();

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::DockSpaceOverViewport(viewport);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);


            MenuBar();

            AssetsExplorer();

            CodeEditor();

            ImGui::Begin("Entities List Window", NULL);
            EntitiesList();
            ImGui::End();        

            ImGui::Begin("Inspector Window", NULL);
            Inspector();
            ImGui::End();

            ImGui::Begin("Scene Editor Window", NULL);
            int editor_camera = EditorCamera();
            ImGui::End();

            AddEntity();
            
            rlImGuiEnd();

            DrawFPS(windowWidth*.9, windowHeight*.1);

        EndDrawing();
    }
}

void CleanUp()
{

    std::cout << "Exiting..." << std::endl;
    

    in_game_preview = false;

    first_time_gameplay = false;
    for (Entity& entity : entities_list)
        entity.running = false;


    for (auto& script_thread : scripts_thread_vector) {
        if (script_thread.joinable())
            script_thread.join();
    }

    scripts_thread_vector.clear();

    for (Entity &entity : entities_list)
        entity.remove();

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

    for (auto it = models_icons.begin(); it != models_icons.end(); ++it) {
        UnloadTexture(it->second);
    }
    models_icons.clear();

    rlImGuiShutdown();
    CloseWindow(); 

}

Vector2 GetGlobalMousePosition()
{
    ImVec2 mousePosition = ImGui::GetMousePos();
    return { static_cast<float>(mousePosition.x), static_cast<float>(mousePosition.y) };
}

void DraggableWindow()
{
    bool isTitleBarHovered = ImGui::IsWindowHovered() && ImGui::IsWindowFocused();

    if (isTitleBarHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        isDragging = true;
    }
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        isDragging = false;
    }

    if (isDragging) {
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
    if (!isWindowMaximized) {

        MaximizeWindow();
        isWindowMaximized = true;
    }
    else {
        RestoreWindow();
        isWindowMaximized = false;
    }
}

void ExitWindowRequested()
{
    const ImVec2 windowSize(200, 70);
    const ImVec2 windowPos((GetScreenWidth() - windowSize.x) * 0.5f, (GetScreenHeight() - windowSize.y) / 2.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking;

    ImGui::SetNextWindowPos(windowPos);
    ImGui::SetNextWindowSize({200, 70});

    ImGui::Begin("Do you want to quit?", nullptr, flags);

    ImGui::Separator();

    ImVec2 buttonSize = ImVec2(100, 30);

    const float posX = (windowSize.x - ImGui::CalcTextSize("Yes").x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
    if (ImGui::Button("Yes", buttonSize)) {
        exitWindow = true;
        std::cout << "QUITING\n";
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    const float posX2 = (posX + ImGui::CalcTextSize("Yes").x) + 20;
    ImGui::SetCursorPosX(posX2);
    if (ImGui::Button("No", buttonSize)) {
        exitWindowRequested = false;
    }

    ImGui::End();
}