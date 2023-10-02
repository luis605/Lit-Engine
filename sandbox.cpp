#include "raylib.h"
#include "raymath.h"
#include <iostream>

// Function to generate LOD mesh
Mesh GenerateLODMesh(Mesh sourceMesh, int lodLevel) {


    Mesh lodMesh = { 0 };

    int lodVertexCount = sourceMesh.vertexCount / (1 << lodLevel);
    int lodTriangleCount = sourceMesh.triangleCount / (1 << (2 * lodLevel));

    lodMesh.vertexCount = lodVertexCount;
    lodMesh.triangleCount = lodTriangleCount;
    lodMesh.vertices = (float *)malloc(lodMesh.vertexCount * 3 * sizeof(float));
    lodMesh.indices = (unsigned short *)malloc(lodMesh.triangleCount * 3 * sizeof(unsigned short));

    for (int i = 0; i < lodMesh.vertexCount * 3; i++) {
        lodMesh.vertices[i] = sourceMesh.vertices[i];
    }

    for (int i = 0; i < lodMesh.triangleCount * 3; i++) {
        lodMesh.indices[i] = sourceMesh.indices[i];
    }

    lodMesh.vboId = sourceMesh.vboId; // Use the original VBO
    lodMesh.vaoId = sourceMesh.vaoId; // Use the original VAO

    return lodMesh;
}

int main() {
    SetTraceLogLevel(LOG_WARNING);

    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "LOD Example");

    Mesh sourceMesh = GenMeshCube(1, 1, 1);

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 0.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float distance = Vector3Distance(camera.position, Vector3Zero());
        int lodLevel = 0;

        // Adjust LOD level based on distance (you can customize this logic)
        if (distance > 5.0f) {
            lodLevel = 1;
        }

        Mesh lodMesh = GenerateLODMesh(sourceMesh, lodLevel);

        BeginDrawing();
        ClearBackground(GRAY);
        UpdateCamera(&camera, CAMERA_FREE);

        BeginMode3D(camera);

        DrawSphere(Vector3Zero(), 0.1f, RED);
        DrawMesh(lodMesh, LoadMaterialDefault(), MatrixIdentity());

        EndMode3D();
        EndDrawing();
    }

    CloseWindow();

    return 0;
}
