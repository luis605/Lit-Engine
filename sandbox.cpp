#include "raylib.h"
#include "raymath.h"

int main()
{
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Texture Tiling Example");

    // Load texture
    Texture2D texture = LoadTexture("assets/images/window_icon.png");

    // Create cube mesh
    Model cube = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));

    // Shader code
    const char *vsCode = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        uniform mat4 model;
        uniform mat4 projection;
        void main()
        {
            TexCoord = aTexCoord;
            gl_Position = projection * model * vec4(aPos, 1.0);
        })";

    const char *fsCode = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D ourTexture;
        void main()
        {
            FragColor = texture(ourTexture, TexCoord);
        })";

    Shader shader = LoadShaderFromMemory(vsCode, fsCode);

    Camera3D camera;
    camera.position = { 10, 10, 10 };
    camera.target = { 0,0,0 };
    camera.up = { 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;




    while (!WindowShouldClose())
    {
        // Update

        // Draw
        BeginDrawing();
        ClearBackground(BLACK);

        // Begin 3D drawing
        BeginMode3D(camera);


        DrawModel(cube, Vector3Zero(), 1.0f, WHITE);


        // End 3D drawing
        EndMode3D();

        EndDrawing();
    }


    CloseWindow();

    return 0;
}
