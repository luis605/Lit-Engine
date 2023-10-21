#include "../include_all.h"

bool can_previewProject = false;



void PreviewStartup()
{
    SetTraceLogLevel(LOG_ERROR);
    SetExitKey(KEY_NULL);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1200, 700, "Game Preview");

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    Py_Initialize();

    shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    InitLighting();


    SetupPhysicsWorld();

    InitSkybox();

    DisableCursor();

}

bool received_data = false;
std::vector<Entity> entities_list_preview;
std::vector<Light> lights_preview;
std::vector<AdditionalLightInfo> light_info_preview;

LitCamera camera_preview;
bool first_time = true;

vector<std::thread> scripts_threads_preview;

void InitPreviewCamera()
{
    camera_preview.position = { 10.0f, 5.0f, 0.0f };
    camera_preview.target = { 0.0f, 0.0f, 0.0f };
    camera_preview.up = { 0.0f, 1.0f, 0.0f };

    Vector3 front = Vector3Subtract(camera_preview.target, camera_preview.position);
    front = Vector3Normalize(front);

    camera_preview.fovy = 60.0f;
    camera_preview.projection = CAMERA_PERSPECTIVE;
}

void PreviewProject()
{
    do {

        close(pipe_fds[1]); // Close the write end of the pipe (child doesn't need it)

        // Read the data from the pipe
        read(pipe_fds[0], &received_data, sizeof(bool));

        close(pipe_fds[0]); // Close the read end of the pipe
    }
    while (received_data == false);

    std::cout << "PreviewProject started" << std::endl;

    PreviewStartup();

    LoadProject(entities_list_preview, lights_preview, light_info_preview, camera_preview);

    std::cout << "Project Loaded" << std::endl;

    InitPreviewCamera();
    while (!WindowShouldClose())
    {
        dynamicsWorld->stepSimulation(GetFrameTime(), 10);
        UpdateInGameGlobals();

        BeginDrawing();

            ClearBackground(GRAY);

            BeginMode3D(camera_preview);

                DrawSkybox();

                for (Entity& entity : entities_list_preview)
                {
                    RenderAndRunEntity(entity, scripts_threads_preview, first_time, &camera_preview);
                }


                if (first_time)
                {
                    for (auto& script_thread : scripts_threads_preview) {
                        if (script_thread.joinable())
                            script_thread.detach();
                    }
                }

                first_time = false;


            EndMode3D();


        EndDrawing();



  
    }

    CloseWindow();
    CleanScriptThreads(scripts_threads_preview);

    close(pipe_fds[1]);
    can_previewProject = false;

    ssize_t bytes_written = write(pipe_fds[0], &can_previewProject, sizeof(bool));
    if (bytes_written != sizeof(bool)) {
        std::cerr << "Error writing to the pipe." << std::endl;
    }

    close(pipe_fds[0]);

    // Set received_data back to false before the recursive call
    received_data = false;

    PreviewProject(); // Recursive cal
}