#include "raylib.h"
#include <iostream>

int main() {
    SetTraceLogLevel(LOG_WARNING);
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Raylib Plane Example");

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Shader shader = LoadShader(0, "custom.fs"); // Load the custom shader

    float size = 100.0f;
    Mesh plane = GenMeshPlane(100, 100, 100, 100);
    Model model = LoadModelFromMesh(plane);

    model.materials[0].shader = shader;

    SetTargetFPS(220);

    while (!WindowShouldClose()) {
        UpdateCamera(&camera, CAMERA_FREE);
        SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), &camera.position, SHADER_UNIFORM_VEC3);

        BeginDrawing();
        ClearBackground(GRAY);

        BeginMode3D(camera);

        BeginShaderMode(shader);

        DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);

        EndShaderMode();

        EndMode3D();

        DrawText("Use WASD to move the camera", 10, 10, 20, DARKGRAY);

        EndDrawing();
    }

    UnloadModel(model);
    UnloadShader(shader); // Unload the custom shader

    CloseWindow();

    return 0;
}

