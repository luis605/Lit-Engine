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
    
    code.resize(100000);


    for (int index = 0; index < sizeof(gizmo_arrow) / sizeof(gizmo_arrow[0]); index++)
    {
        gizmo_arrow[index].model = LoadModel("assets/models/gizmo/arrow.obj");
    }
    

    for (int index = 0; index <  sizeof(gizmo_taurus) / sizeof(gizmo_taurus[0]); index++)
    {
        gizmo_taurus[index] = LoadModel("assets/models/gizmo/taurus.obj");
    }
    
    shader = LoadShader("game/shaders/lighting_vertex.glsl", "game/shaders/lighting_fragment.glsl");
    shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");

    int ambientLoc = GetShaderLocation(shader, "ambient");
    float ambient_light[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    SetShaderValue(shader, ambientLoc, ambient_light, SHADER_UNIFORM_VEC4);






    // Init Docking
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking
    
    SetStyleGray(&ImGui::GetStyle());

    InitEditorCamera();
    // Main game loop
    while (!WindowShouldClose())
    {

        BeginDrawing();
        ClearBackground(DARKGRAY);

        // start the GUI
        rlImGuiBegin();






        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

        MenuBar();

        // Assets Explorer
        AssetsExplorer();

        // Code Editor
        CodeEditor();

        // Entities List
        ImGui::Begin("Entities List Window", NULL);
        EntitiesList();
        ImGui::End();        



        // Inspector
        ImGui::Begin("Inspector Window", NULL);
        Inspector();
        ImGui::End();

        // Scene Editor
        ImGui::Begin("Scene Editor Window", NULL);
        int editor_camera = EditorCamera();
        ImGui::End();



        AddEntity();
        


        // end ImGui
        rlImGuiEnd();

        DrawFPS(screenWidth1*.9, screenHeight1*.1);
        // Finish drawing
        EndDrawing();
    }

    std::cout << "Exiting..." << std::endl;

    rlImGuiShutdown();
    CloseWindow(); 
    return 0;

}

int main()
{
    // Child process
    LitEngine();
    return 0;
}