#include "raylib.h"

#include "rlgl.h"
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

    Shader shader = LoadShader("Engine/Lighting/shaders/csm.vs", "Engine/Lighting/shaders/csm.fs");

    Model plane = LoadModelFromMesh(GenMeshPlane(10.0f, 10.0f, 1, 1));
    Model cube = LoadModelFromMesh(GenMeshCube(2.0f, 2.0f, 2.0f));

    plane.materials[0].shader = shader;
    cube.materials[0].shader = shader;


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
                    DrawModel(cube, Vector3{0, 3, 0}, 1.0f, RED);
                EndShaderMode();
            EndMode3D();
            }EndTextureMode();
        }
        

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
