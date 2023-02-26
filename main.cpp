#include "raylib.h"
#include "raymath.h"
int main(void)
{
    // Initialize window
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "Orbit rotation");

    // Initialize camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Initialize red cube position
    Vector3 redCubePos = { 3.0f, 0.0f, 0.0f };

    // Define orbit radius and speed
    float orbitRadius = 3.0f;
    float orbitSpeed = 0.05f;



    // Main loop
    SetTargetFPS(60);
    while (!WindowShouldClose())
    {
        // Update camera
        UpdateCamera(&camera);

        // Clear background
        BeginDrawing();
        ClearBackground(RAYWHITE);
        
        // Draw green cube
        DrawCube((Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f, GREEN);

        // Update red cube position
        Matrix redCubePosMat = MatrixTranslate(redCubePos.x, redCubePos.y, redCubePos.z);
        Matrix redCubeRotMat = MatrixMultiply(MatrixRotateY(orbitSpeed), redCubePosMat);
        redCubePos.x = redCubeRotMat.m12;
        redCubePos.y = redCubeRotMat.m13;
        redCubePos.z = redCubeRotMat.m14;
        redCubePos.x = orbitRadius;

        // Draw red cube
        DrawCube(redCubePos, 1.0f, 1.0f, 1.0f, RED);
        
        // End frame
        EndDrawing();
    }

    // Close window and unload resources
    CloseWindow();

    return 0;
}
