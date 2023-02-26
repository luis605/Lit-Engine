#include "include_all.h"


static ImTextureID icon_texture;







int LitEngine()
{

    Py_Initialize();
    
    int screenWidth1 = 1900;
    int screenHeight1 = 900;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth1, screenHeight1, "Lit Engine");
    SetTargetFPS(144);
    rlImGuiSetup(true);

    SetTraceLogLevel(LOG_WARNING);
    
    folder_texture = LoadTexture("assets/images/gray_folder.png");
    image_texture = LoadTexture("assets/images/image_file_type.png");
    cpp_texture = LoadTexture("assets/images/cpp_file_type.png");
    empty_texture = LoadTexture("assets/images/empty_file_file_type.png");
    run_texture = LoadTexture("assets/images/run_game.png");
    pause_texture = LoadTexture("assets/images/pause_game.png");
    
    std::string code;
    code.resize(100000);


    for (int index = 0; index <  sizeof(gizmo_arrows) / sizeof(gizmo_arrows[0]); index++)
    {
        gizmo_arrows[index] = LoadModel("assets/models/gizmo/arrow.obj");;
    }
    

    for (int index = 0; index <  sizeof(gizmo_taurus) / sizeof(gizmo_taurus[0]); index++)
    {
        gizmo_taurus[index] = LoadModel("assets/models/gizmo/taurus.obj");;
    }
    

    // Init Docking
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking

    InitEditorCamera();
    // Main game loop
    while (!WindowShouldClose())
    {

        BeginDrawing();
        ClearBackground(DARKGRAY);

        // start the GUI
        rlImGuiBegin();




        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());


        // Assets Explorer
        AssetsExplorer(code);

        // Code Editor
        CodeEditor(code);

        // Entities List
        EntitiesListRun();
        



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