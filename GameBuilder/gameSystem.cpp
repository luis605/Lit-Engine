//#include "include.h"
#define GAME_SHIPPING


#include "../include_all.h"

void InitWindow();
void WindowMainloop();
void Run();
void StartGame();

#define WindowHeight GetScreenHeight() / 1.5
#define WindowWidth GetScreenWidth() / 1.5

string gameTitle = "My Own Game!";
bool first_time = true;


Camera3D inGame_Camera = { 0 };


void InitWindow()
{
    InitWindow(WindowWidth, WindowHeight, gameTitle.c_str());
    shader = LoadShader("../Engine/Lighting/shaders/lighting_vertex.glsl", "../Engine/Lighting/shaders/lighting_fragment.glsl");
    InitLighting();
    SetTargetFPS(60);
    LoadProject(entities_list, lights, lights_info);

    inGame_Camera.position = (Vector3){ 0.0f, 10.0f, 10.0f };  // inGame_Camera position
    inGame_Camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };      // inGame_Camera looking at point
    inGame_Camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };          // inGame_Camera up vector (rotation towards target)
    inGame_Camera.fovy = 45.0f;                                // inGame_Camera field-of-view Y
    inGame_Camera.projection = CAMERA_PERSPECTIVE;             // inGame_Camera mode type

}

void WindowMainloop()
{
    while (!WindowShouldClose())
    {
        Run();
    }
}



void Run()
{
    BeginDrawing();
        ClearBackground(GRAY);

        BeginMode3D(inGame_Camera);

            UpdateInGameGlobals();

            for (Entity& entity : entities_list)
            {
                RenderAndRunEntity(entity, scripts_thread_vector, first_time, &inGame_Camera);
            }


            if (first_time)
            {
                for (auto& script_thread : scripts_thread_vector) {
                    if (script_thread.joinable())
                        script_thread.detach();
                }
            }

            first_time = false;

        EndMode3D();

    EndDrawing();
}


int main()
{
    InitWindow();
    WindowMainloop();
    CloseWindow();
}