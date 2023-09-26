#include "raylib.h"

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "Raylib Texture Tiling");

    // Load a texture
    Texture2D texture = LoadTexture("icon.png");

    // Create a cube mesh
    Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);

    // Load the texture onto the GPU
    Model model = LoadModelFromMesh(cube);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;

    // Set the tiling of the texture
    float tiling[2] = {5.0f, 5.0f};
    Shader shader = LoadShader(0, "custom.shader"); // Create a custom shader in a .glsl file
    SetShaderValue(shader, GetShaderLocation(shader, "tiling"), tiling, SHADER_UNIFORM_VEC2);
    model.materials[0].shader = shader;

    // Camera setup
    Camera camera = { 0 };
    camera.position = (Vector3){ 3.0f, 3.0f, 3.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Main game loop
    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        UpdateCamera(&camera, CAMERA_FREE);

        // Draw the model
        {
            BeginMode3D(camera);
            BeginShaderMode(shader);

            DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 5.0f, WHITE);

            EndShaderMode();
            EndMode3D();
        }
        
        DrawText("Use mouse to rotate the camera", 10, 10, 20, DARKGRAY);

        EndDrawing();
    }

    UnloadTexture(texture);
    UnloadModel(model);
    CloseWindow();

    return 0;
}
