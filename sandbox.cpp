#include "raylib.h"
#include <iostream>
#include "raymath.h"

int main() {
    SetTraceLogLevel(LOG_WARNING);

    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 600;
    
    InitWindow(screenWidth, screenHeight, "Copy Mesh Example");

    // Create a source mesh
    Mesh sourceMesh = GenMeshCube(1.0f, 1.0f, 1.0f);

    // Create an empty destination mesh
    Mesh destMesh = { 0 };
    destMesh.vertexCount = sourceMesh.vertexCount;
    destMesh.triangleCount = sourceMesh.triangleCount;

    // Copy vertex and index data from the source mesh to the destination mesh
    destMesh.vertices = (float *)malloc(destMesh.vertexCount * 3 * sizeof(float));
    destMesh.texcoords = (float *)malloc(destMesh.vertexCount * 2 * sizeof(float));
    destMesh.texcoords2 = (float *)malloc(destMesh.vertexCount * 2 * sizeof(float));
    destMesh.normals = (float *)malloc(destMesh.vertexCount * 3 * sizeof(float));
    destMesh.tangents = (float *)malloc(destMesh.vertexCount * 4 * sizeof(float));
    destMesh.colors = (unsigned char *)malloc(destMesh.vertexCount * 4 * sizeof(unsigned char));
    destMesh.indices = (unsigned short *)malloc(destMesh.triangleCount * 3 * sizeof(unsigned short));
    destMesh.vboId = sourceMesh.vboId;

    // Copy the VBO data
    for (int i = 0; i < destMesh.vertexCount * 3; i++) {
        destMesh.vertices[i] = sourceMesh.vertices[i];
    }

    for (int i = 0; i < destMesh.triangleCount * 3; i++) {
        destMesh.indices[i] = sourceMesh.indices[i];
    }


    // Copy the texcoords, normals, colors, and indices
    for (int i = 0; i < destMesh.vertexCount * 2; i++) {
        destMesh.texcoords[i] = sourceMesh.texcoords[i];
    }

    // Copy the texcoords2, if they exist in the source mesh
    if (sourceMesh.texcoords2 != NULL) {
        for (int i = 0; i < destMesh.vertexCount * 2; i++) {
            destMesh.texcoords2[i] = sourceMesh.texcoords2[i];
        }
    } else {
        // If texcoords2 do not exist in the source mesh, set them to default values (0.0f)
        for (int i = 0; i < destMesh.vertexCount * 2; i++) {
            destMesh.texcoords2[i] = 0.0f;
        }
    }


    for (int i = 0; i < destMesh.vertexCount * 3; i++) {
        destMesh.normals[i] = sourceMesh.normals[i];
    }

    // Copy the tangents if they exist in the source mesh
    if (sourceMesh.tangents != NULL) {
        destMesh.tangents = (float *)malloc(destMesh.vertexCount * 4 * sizeof(float));
        for (int i = 0; i < destMesh.vertexCount * 4; i++) {
            destMesh.tangents[i] = sourceMesh.tangents[i];
        }
    } else {
        // If tangents do not exist in the source mesh, set them to default values (0.0f)
        destMesh.tangents = (float *)malloc(destMesh.vertexCount * 4 * sizeof(float));
        for (int i = 0; i < destMesh.vertexCount * 4; i++) {
            destMesh.tangents[i] = 0.0f;
        }
    }

    if (sourceMesh.colors != NULL) {
        destMesh.colors = (unsigned char *)malloc(destMesh.vertexCount * 4 * sizeof(unsigned char));
        for (int i = 0; i < destMesh.vertexCount * 4; i++) {
            destMesh.colors[i] = sourceMesh.colors[i];
        }
    } else {
        // If colors do not exist in the source mesh, set them to a default color (white)
        destMesh.colors = (unsigned char *)malloc(destMesh.vertexCount * 4 * sizeof(unsigned char));
        for (int i = 0; i < destMesh.vertexCount * 4; i++) {
            destMesh.colors[i] = 255; // Default to fully opaque white
        }
    }

    if (sourceMesh.vaoId != NULL) {
        destMesh.vaoId = sourceMesh.vaoId;
    }








    // Copy animation vertex data
    if (sourceMesh.animVertices != NULL) {
        destMesh.animVertices = (float *)malloc(destMesh.vertexCount * 3 * sizeof(float));
        for (int i = 0; i < destMesh.vertexCount * 3; i++) {
            destMesh.animVertices[i] = sourceMesh.animVertices[i];
        }
    }

    if (sourceMesh.animNormals != NULL) {
        destMesh.animNormals = (float *)malloc(destMesh.vertexCount * 3 * sizeof(float));
        for (int i = 0; i < destMesh.vertexCount * 3; i++) {
            destMesh.animNormals[i] = sourceMesh.animNormals[i];
        }
    }

    if (sourceMesh.boneIds != NULL) {
        destMesh.boneIds = (unsigned char *)malloc(destMesh.vertexCount * 4 * sizeof(unsigned char));
        for (int i = 0; i < destMesh.vertexCount * 4; i++) {
            destMesh.boneIds[i] = sourceMesh.boneIds[i];
        }
    }

    if (sourceMesh.boneWeights != NULL) {
        destMesh.boneWeights = (float *)malloc(destMesh.vertexCount * 4 * sizeof(float));
        for (int i = 0; i < destMesh.vertexCount * 4; i++) {
            destMesh.boneWeights[i] = sourceMesh.boneWeights[i];
        }
    }


    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 0.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);
    // Main loop
    while (!WindowShouldClose()) {
        // Draw
        BeginDrawing();
        ClearBackground(GRAY);
        UpdateCamera(&camera, CAMERA_FREE);

        BeginMode3D(camera);

        DrawSphere(Vector3Zero(), .1f, RED);
        // Draw the destination mesh
        DrawMesh(destMesh, LoadMaterialDefault(), MatrixIdentity());
        EndMode3D();
        EndDrawing();
    }

    // De-allocate memory for both meshes
    free(destMesh.vertices);
    free(destMesh.texcoords);
    free(destMesh.texcoords2);
    free(destMesh.normals);
    free(destMesh.tangents);
    free(destMesh.colors);
    free(destMesh.indices);
    free(destMesh.animVertices);
    free(destMesh.animNormals);
    free(destMesh.boneIds);
    free(destMesh.boneWeights);

    // Clean up raylib
    CloseWindow();

    return 0;
}
