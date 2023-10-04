#include "include/raylib.h"
#include "include/raymath.h"
#include <iostream>
#include <vector>
#include <cfloat>
#include <cstring>

// Define a structure to represent a vertex with its position and QEM matrix
struct VertexQEM {
    Vector3 position;
    Matrix qem;
};

// Function to generate LOD mesh using Quadric Error Metrics
Mesh GenerateLODMesh(Mesh sourceMesh, int lodLevel) {
    if (lodLevel <= 0 || sourceMesh.indices == nullptr) return sourceMesh; // Return the source mesh if lodLevel is 0 or negative or if indices are nullptr

    std::cout << "LoD Calculation\n" << std::endl;;
    // Convert the source mesh into a list of vertices with QEM matrices
    std::vector<VertexQEM> vertices(sourceMesh.vertexCount);
    for (int i = 0; i < sourceMesh.vertexCount; i++) {
        vertices[i].position = *(Vector3*)&sourceMesh.vertices[i * 3];
        vertices[i].qem = MatrixIdentity();
    }

    // Calculate QEM matrices for each vertex
    for (int i = 0; i < sourceMesh.triangleCount * 3; i += 3) {
        Vector3 normal = *(Vector3*)&sourceMesh.normals[i / 3];
        for (int j = 0; j < 3; j++) {
            int index = sourceMesh.indices[i + j];
            if (index < 0 || index >= vertices.size()) {
                // Invalid vertex index, skip this iteration
                continue;
            }
            Matrix normalMatrix = MatrixScale(normal.x, normal.y, normal.z);
            Matrix positionMatrix = MatrixTranslate(vertices[index].position.x, vertices[index].position.y, vertices[index].position.z);
            Matrix qemMatrix = MatrixMultiply(normalMatrix, positionMatrix);
            vertices[index].qem = MatrixAdd(vertices[index].qem, qemMatrix);
        }
    }

    // Simplification loop
    while (vertices.size() > sourceMesh.vertexCount / (1 << lodLevel)) {
        // Find the two vertices to collapse (minimize error)
        int v1 = 0, v2 = 0;
        float minError = FLT_MAX;

        for (int i = 0; i < sourceMesh.triangleCount * 3; i++) {
            int vertex1 = sourceMesh.indices[i];
            int vertex2 = sourceMesh.indices[i + 1];
            if (vertex1 < 0 || vertex1 >= vertices.size() || vertex2 < 0 || vertex2 >= vertices.size()) {
                // Invalid vertex indices, skip this iteration
                continue;
            }
            float error = Vector3DotProduct(vertices[vertex1].position, *(Vector3*)&vertices[vertex1].qem.m0) * vertices[vertex1].position.x +
                        Vector3DotProduct(vertices[vertex2].position, *(Vector3*)&vertices[vertex2].qem.m0) * vertices[vertex2].position.x;

            if (error < minError) {
                minError = error;
                v1 = vertex1;
                v2 = vertex2;
            }
        }

        // Collapse vertices v1 and v2 into a new vertex
        if (v1 < 0 || v1 >= vertices.size() || v2 < 0 || v2 >= vertices.size()) {
            // Invalid vertex indices, break the loop
            break;
        }
        vertices[v1].position = Vector3Scale(Vector3Add(vertices[v1].position, vertices[v2].position), 0.5f);

        // Update the QEM matrix for v1 and remove v2 from the list
        if (v1 < 0 || v1 >= vertices.size() || v2 < 0 || v2 >= vertices.size()) {
            // Invalid vertex indices, break the loop
            break;
        }
        vertices[v1].qem = MatrixAdd(vertices[v1].qem, vertices[v2].qem);
        vertices.erase(vertices.begin() + v2);
    }

    // Generate a simplified mesh from the remaining vertices
    Mesh lodMesh = { 0 };
    lodMesh.vertexCount = vertices.size();
    lodMesh.triangleCount = sourceMesh.triangleCount; // Keeping the same number of triangles for simplicity
    lodMesh.vertices = (float*)malloc(lodMesh.vertexCount * 3 * sizeof(float));
    lodMesh.indices = (unsigned short*)malloc(lodMesh.triangleCount * 3 * sizeof(unsigned short));

    for (int i = 0; i < lodMesh.vertexCount; i++) {
        lodMesh.vertices[i * 3] = vertices[i].position.x;
        lodMesh.vertices[i * 3 + 1] = vertices[i].position.y;
        lodMesh.vertices[i * 3 + 2] = vertices[i].position.z;
    }

    // Copy the indices from the source mesh
    if (sourceMesh.indices != nullptr) {
        memcpy(lodMesh.indices, sourceMesh.indices, lodMesh.triangleCount * 3 * sizeof(unsigned short));
    }

    lodMesh.vboId = sourceMesh.vboId;
    lodMesh.vaoId = sourceMesh.vaoId;

    return lodMesh;
}





float CalculateDistance(const Vector3& a, const Vector3& b) {
    return Vector3Distance(a, b);
}

void PrintMeshVerticesWithinRange(const Mesh& mesh, float distanceThreshold) {
    if (mesh.vertices == nullptr || mesh.vertexCount == 0) {
        std::cout << "Mesh has no vertices or is not initialized." << std::endl;
        return;
    }

    for (int i = 0; i < mesh.vertexCount; i++) {
        Vector3 currentVertex = { mesh.vertices[i * 3], mesh.vertices[i * 3 + 1], mesh.vertices[i * 3 + 2] };

        // Check the distance to all other vertices in the mesh
        for (int j = i + 1; j < mesh.vertexCount; j++) {
            Vector3 otherVertex = { mesh.vertices[j * 3], mesh.vertices[j * 3 + 1], mesh.vertices[j * 3 + 2] };

            float distance = CalculateDistance(currentVertex, otherVertex);

            if (distance <= distanceThreshold) {
                std::cout << "Vertices " << i << " and " << j << " are within the specified distance range (" << distance << " units)." << std::endl;
            }
        }
    }
}



int main() {
    SetTraceLogLevel(LOG_WARNING);

    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "LOD Example");

    // Starting LOD level
    Mesh sourceMesh = GenMeshCube(1,1,1);
    int lodLevel = 2;

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 0.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);


    PrintMeshVerticesWithinRange(sourceMesh, 0.0f);

    CloseWindow();
    return 0;

    while (!WindowShouldClose()) {
        // Simplify the source mesh to the desired LOD level
        Mesh lodMesh = GenerateLODMesh(sourceMesh, lodLevel); // Adjust the lodLevel as needed

        BeginDrawing();
        ClearBackground(GRAY);
        UpdateCamera(&camera, CAMERA_FREE);

        BeginMode3D(camera);

        DrawSphere(Vector3Zero(), 0.1f, RED);
        DrawModel(LoadModelFromMesh(lodMesh), Vector3Zero(), 1, RED);
        DrawModel(LoadModelFromMesh(sourceMesh), Vector3Zero(), 1, { 255, 0, 255, 100 });

        EndMode3D();
        EndDrawing();

        // Increase or decrease LOD level based on user input (e.g., arrow keys)
        if (IsKeyPressed(KEY_P)) {
            lodLevel++;
            std::cout << "Lod Level Increased" << std::endl;
            if (lodLevel > 2) lodLevel = 2; // Limit maximum LOD level
        }
        else if (IsKeyPressed(KEY_O)) {
            lodLevel--;
            std::cout << "Lod Level Decreased" << std::endl;
            if (lodLevel < 0) lodLevel = 0; // Limit minimum LOD level
        }
    }

    CloseWindow();

    return 0;
}
