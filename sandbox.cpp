#include "raylib.h"

#include "raymath.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

int main()
{
    SetTraceLogLevel(LOG_WARNING);

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
    
    SetTargetFPS(60);

    Shader shader = LoadShader("Engine/Lighting/shaders/lod.vs", "Engine/Lighting/shaders/lod.fs");
    Shader shader2 = LoadShader(0, "Engine/Lighting/shaders/lod.fs");

    Model plane = LoadModelFromMesh(GenMeshPlane(10.0f, 10.0f, 1, 1));
    Model cube = LoadModelFromMesh(GenMeshCylinder(1,3,100));
    Model cube_simple = LoadModelFromMesh(GenMeshCylinder(1,3,100));

    plane.materials[0].shader = shader;
    cube.materials[0].shader = shader;
    cube_simple.materials[0].shader = shader2;

    DisableCursor();

    // Main game loop
    while (!WindowShouldClose())
    {
        
        SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), &camera.position, SHADER_UNIFORM_VEC3);
        UpdateCamera(&camera, CAMERA_FREE);
        BeginDrawing();
        ClearBackground(RAYWHITE);


        {
            ClearBackground(GRAY);
            BeginMode3D(camera);
            {
                BeginShaderMode(shader);
                    DrawModel(plane, Vector3{0, 0, 0}, 4.0f, WHITE);
                    DrawModel(cube, Vector3{0, 3, 10}, 1.0f, RED);
                    DrawModel(cube_simple, Vector3{0, 3, 0}, 1.0f, WHITE);
                EndShaderMode();
            EndMode3D();
            }EndTextureMode();
        }
        

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
