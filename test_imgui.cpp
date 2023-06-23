#include "include_all.h"


static ImTextureID icon_texture;






int LitEngine()
{
    Py_Initialize();
    
    
    int screenWidth1 = 1900;
    int screenHeight1 = 900;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth1, screenHeight1, "Lit Engine");
    SetTargetFPS(100000);
    rlImGuiSetup(true);

    SetTraceLogLevel(LOG_WARNING);
    
    folder_texture = LoadTexture("assets/images/gray_folder.png");
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
    
    code.resize(100000);
	auto lang = TextEditor::LanguageDefinition::CPlusPlus();


    for (int index = 0; index < sizeof(gizmo_arrow) / sizeof(gizmo_arrow[0]) + 1; index++)
    {
        gizmo_arrow[index].model = LoadModel("assets/models/gizmo/arrow.obj");
    }
    

    for (int index = 0; index < (sizeof(gizmo_taurus) / sizeof(gizmo_taurus[0])) + 1; index++)
    {
        gizmo_taurus[index] = LoadModel("assets/models/gizmo/taurus.obj");
    }
    
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    SetStyleGray(&ImGui::GetStyle());

    InitEditorCamera();

    shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    InitLighting();

    selected_entity = &Entity();
    selected_light = &Light();

    // InitPhysx();

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

            DrawFPS(screenWidth1*.9, screenHeight1*.1);

        EndDrawing();
    }

    std::cout << "Exiting..." << std::endl;
    CleanUp();
    rlImGuiShutdown();
    CloseWindow(); 

    return 0;
}

int main()
{
    LitEngine();
    return 0;
}