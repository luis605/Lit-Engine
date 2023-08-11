#include "raylib.h"

#include "include/par_shapes.h"

#include "rlgl.h"
#include "raymath.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>





Mesh SimplifyMesh(Mesh mes32h)
{
    Mesh mesh = { 0 };
    

    mesh.vertices = (float *)RL_MALLOC(8*sizeof(float));
    mesh.texcoords = (float *)RL_MALLOC(8*sizeof(float));
    mesh.normals = (float *)RL_MALLOC(8*sizeof(float));

    mesh.vertexCount = 8;
    mesh.triangleCount = 8/2;


    for (int k = 0; k < mesh.vertexCount; k++)
    {
        mesh.vertices[k*3] = cubeMesh->points[cubeMesh->triangles[k]*3];
        mesh.vertices[k*3 + 1] = cubeMesh->points[cubeMesh->triangles[k]*3 + 1];
        mesh.vertices[k*3 + 2] = cubeMesh->points[cubeMesh->triangles[k]*3 + 2];

        mesh.normals[k*3] = cubeMesh->normals[cubeMesh->triangles[k]*3];
        
        mesh.texcoords[k*2] = cubeMesh->tcoords[cubeMesh->triangles[k]*2];
        mesh.texcoords[k*2 + 1] = cubeMesh->tcoords[cubeMesh->triangles[k]*2 + 1];
    }

    par_shapes_free_mesh(cubeMesh);

    // Upload vertex data to GPU (static mesh)
    UploadMesh(&mesh, false);


    return mesh;
}



int main()
{
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "Dynamic LoD - Implementation");

    // Setup camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    
    SetTargetFPS(60);

    Mesh mesh = GenMeshSphere(2, 100, 100);

    Mesh mesh2 = SimplifyMesh(mesh);

    Model model = LoadModelFromMesh(mesh2);

    DisableCursor();
    
    // Main game loop
    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_FREE);

        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // 3D drawing
        BeginMode3D(camera);

        DrawModel(model, (Vector3){ 0, 0, 0 }, 1.0f, BLACK);

        EndMode3D();

        EndDrawing();
    }

    // Cleanup

    CloseWindow();

    return 0;
}
