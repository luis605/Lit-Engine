#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>
#include "raylib.h"
#include "raymath.h"


const int screenWidth = 1200;
const int screenHeight = 450;



int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "meshes");

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 1.0f, -5.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    
    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");
    
    float vertices[] = {
        -1, 1, 1,    // Front top-left
         1, 1, 1,    // Front top-right
         1,-1, 1,    // Front bottom-right
        -1,-1, 1,    // Front bottom-left
        -1, 1,-1,    // Back top-left
         1, 1,-1,    // Back top-right
         1,-1,-1,    // Back bottom-right
        -1,-1,-1     // Back bottom-left
    };

    // Define indices for a cube (forming two triangles per face)
    int indices[] = {
        0, 1, 2,  // Front face
        2, 3, 0,
        1, 5, 6,  // Right face
        6, 2, 1,
        4, 7, 6,  // Back face
        6, 5, 4,
        0, 3, 7,  // Left face
        7, 4, 0,
        0, 4, 5,  // Top face
        5, 1, 0,
        2, 6, 7,  // Bottom face
        7, 3, 2
    };

    Mesh mesh = { 0 };
    mesh.vertexCount = sizeof(vertices) / (3 * sizeof(float));
    mesh.triangleCount = sizeof(indices) / (3 * sizeof(int));
    mesh.vertices = (float*)malloc(sizeof(float) * mesh.vertexCount * 3);
    mesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * mesh.triangleCount * 3);

    // Copy vertices and indices to the Mesh
    for (int i = 0; i < mesh.vertexCount * 3; ++i) {
        mesh.vertices[i] = vertices[i];
    }

    for (int i = 0; i < mesh.triangleCount * 3; i += 3) {
        mesh.indices[i] = indices[i];
        mesh.indices[i + 1] = indices[i + 2];
        mesh.indices[i + 2] = indices[i + 1];
        
    }

    UploadMesh(&mesh, false);

    Model model = LoadModelFromMesh(mesh);
    
    
    model.materials[0].shader = shader;

    SetTargetFPS(50);



    while (!WindowShouldClose()) {
        
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);

        
        BeginDrawing();
        ClearBackground(GRAY);

        
        BeginMode3D(camera);
        if (IsModelReady(model))
        {
            if (IsKeyDown(KEY_P))
                DrawModelWires(model, Vector3Zero(), 1.0f, RED);
            else
                DrawModel(model, Vector3Zero(), 1.0f, RED);
        }
        EndMode3D();

        
        
        EndDrawing();
    }

    
    CloseWindow();

    return 0;
}