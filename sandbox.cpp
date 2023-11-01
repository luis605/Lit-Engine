#include "include/raylib.h"

int main() {
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Example using Static Library with Raylib");

    SetTargetFPS(60);

    // Main loop
    while (!WindowShouldClose()) {
        // Update
        // TODO: Update your logic here

        // Draw
        BeginDrawing();

        ClearBackground(RAYWHITE);
        DrawText("Hello, using Static Library with Raylib!", 190, 200, 20, LIGHTGRAY);

        EndDrawing();
    }

    // Close window and OpenGL context
    CloseWindow();

    return 0;
}
