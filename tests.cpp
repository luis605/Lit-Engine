#include "raylib.h"

#include "rlgl.h"
#include "raymath.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

int main()
{
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "CSM");

    // Setup camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Camera3D camera_shadow_map = {0};
    
    SetTargetFPS(60);

    Shader shader = LoadShader("Engine/Lighting/shaders/csm.vs", "Engine/Lighting/shaders/csm.fs");

    int ambientLoc = GetShaderLocation(shader, "ambient");
    SetShaderValue(shader, ambientLoc, &Vector4{ 0.2f, 0.2f, 0.2f, 1.0f }, SHADER_UNIFORM_VEC4);

    Model plane = LoadModelFromMesh(GenMeshPlane(10.0f, 10.0f, 1, 1));
    Model cube = LoadModelFromMesh(GenMeshCube(2.0f, 2.0f, 2.0f));

    plane.materials[0].shader = shader;
    cube.materials[0].shader = shader;

    RenderTexture2D render_texture = {0};

    DisableCursor();

    // Main game loop
    while (!WindowShouldClose())
    {
        
        SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), &camera.position, SHADER_UNIFORM_VEC3);
        UpdateCamera(&camera, CAMERA_FREE);
        BeginDrawing();
        ClearBackground(RAYWHITE);

        camera_shadow_map.position = {30,20,0};

        BeginTextureMode(render_texture);
        {
            ClearBackground(GRAY);
            BeginMode3D(camera_shadow_map);
            {
                DrawModel(plane, Vector3{0, 0, 0}, 4.0f, WHITE);
                DrawModel(cube, Vector3{0, 3, 0}, 1.0f, RED);
            EndMode3D();
            }EndTextureMode();
        }
        
        BeginMode3D(camera);

        DrawModel(plane, Vector3{0, 0, 0}, 4.0f, WHITE);
        DrawModel(cube, Vector3{0, 3, 0}, 1.0f, RED);

        EndMode3D();

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
