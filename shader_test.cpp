#include <iostream>
#include "raylib.h"

int main()
{
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "raylib [shaders] example");

    Shader shader = LoadShader("vert.glsl", "frag.glsl");

    Model model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));

    Camera camera = { { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 45.0f, 0 };
    
    SetCameraMode(camera, CAMERA_FREE);

    SetTargetFPS(60);
    while (!WindowShouldClose())
    {
        UpdateCamera(&camera);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        BeginMode3D(camera);

        BeginShaderMode(shader);
        model.materials[0].shader = shader;
        DrawModel(model, (Vector3){ 0, 0, 0 }, 1.0f, RED);
        EndShaderMode();

        EndMode3D();

        EndDrawing();
    }

    UnloadShader(shader);
    UnloadModel(model);
    CloseWindow();

    return 0;
}
