#include "../include_all.h"

#define MAX(a, b) ((a)>(b)? (a) : (b))
#define MIN(a, b) ((a)<(b)? (a) : (b))


void Startup() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(windowWidth, windowHeight, "Lit Engine");

    // Face Culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Python
    Py_Initialize();

    // Raylib
    SetTargetFPS(10000);
    SetTraceLogLevel(LOG_WARNING);

    // ImGui
    rlImGuiSetup(true);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    SetStyleGray(&ImGui::GetStyle());

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowMinSize.x = 370.0f;

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
    while (!WindowShouldClose())
    {
        BeginDrawing();

            ClearBackground(DARKGRAY);

            rlImGuiBegin();
            ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

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

void CleanUp() {

    std::cout << "Exiting..." << std::endl;
    rlImGuiShutdown();
    CloseWindow(); 


    UnloadShader(shader);


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

}
