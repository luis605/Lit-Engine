#include <algorithm>
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <unordered_map>
#include <map>
#include <queue>
#include <limits>
#include <iostream>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"

const size_t batchSize = 500;

struct VertexIndices {
    std::vector<size_t> indices;
    std::vector<Vector3> vertices;
    std::vector<Vector3> normals;
};

struct EdgeData {
    Vector3 normal;
    size_t indices[2];
    bool processed;
};



VertexIndices SimplifyMesh(const Mesh mesh, const std::vector<Vector3> vertices, const std::vector<Vector3> normals, float threshold) {
    VertexIndices result;

    result.vertices = vertices;
    result.normals = normals;

    for (size_t i = 0; i < mesh.vertexCount; i++) {
        result.indices.push_back(mesh.indices[i]);
    }



    return result;

}



Mesh GenerateLODMesh(const VertexIndices meshData, const Mesh sourceMesh) {
    Mesh lodMesh = { 0 };

    if (!meshData.vertices.empty()) {
        int vertexCount = meshData.vertices.size();
        int triangleCount = vertexCount / 3;
        int indexCount = triangleCount * 3;

        // Allocate memory for the new mesh
        lodMesh.vertexCount = vertexCount;
        lodMesh.triangleCount = triangleCount;
        lodMesh.vertices = (float*)malloc(sizeof(float) * 3 * vertexCount);
        lodMesh.indices = (unsigned short*)calloc(indexCount, sizeof(unsigned short));
        lodMesh.normals = (float*)malloc(sizeof(float) * 3 * vertexCount);

        // Calculate the bounding box of the contracted mesh
        Vector3 minVertex = meshData.vertices[0];
        Vector3 maxVertex = meshData.vertices[0];
        for (const auto& vertex : meshData.vertices) {
            minVertex = Vector3Min(minVertex, vertex);
            maxVertex = Vector3Max(maxVertex, vertex);
        }

        // Calculate the scaling factors for texture coordinates
        float scaleX = 1.0f / (maxVertex.x - minVertex.x);
        float scaleY = 1.0f / (maxVertex.y - minVertex.y);

        // Assign vertices and normals directly to the mesh
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = meshData.vertices[i].x;
            lodMesh.vertices[i * 3 + 1] = meshData.vertices[i].y;
            lodMesh.vertices[i * 3 + 2] = meshData.vertices[i].z;

            lodMesh.normals[i * 3] = meshData.normals[i].x;
            lodMesh.normals[i * 3 + 1] = meshData.normals[i].y;
            lodMesh.normals[i * 3 + 2] = meshData.normals[i].z;
        }


        // Copy indices from the result mesh
        for (int i = 0; i < triangleCount; i++) {
            lodMesh.indices[i * 3] = static_cast<unsigned short>(meshData.indices[i * 3]);
            lodMesh.indices[i * 3 + 1] = static_cast<unsigned short>(meshData.indices[i * 3 + 1]);
            lodMesh.indices[i * 3 + 2] = static_cast<unsigned short>(meshData.indices[i * 3 + 2]);
        }


    }


    // Upload the mesh data to GPU
    UploadMesh(&lodMesh, false);



    return lodMesh;
}

int main() {
    int screenWidth = 800;
    int screenHeight = 450;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "QEM Mesh Simplification");

    Mesh cube = GenMeshCube(1,1,1);

    std::vector<Vector3> vertices;
    std::vector<Vector3> normals;

    for (size_t i = 0; i < cube.vertexCount; ++i) {
        size_t baseIndex = i * 3;
        float x = cube.vertices[baseIndex];
        float y = cube.vertices[baseIndex + 1];
        float z = cube.vertices[baseIndex + 2];
        float nx = cube.normals[baseIndex];
        float ny = cube.normals[baseIndex + 1];
        float nz = cube.normals[baseIndex + 2];

        vertices.push_back({ x, y, z });
        normals.push_back({ nx, ny, nz });
    }


    for (size_t i = 0; i < cube.vertexCount; ++i) {
        std::cout << "Vertices 1: " << vertices[i].x << " " << vertices[i].y << " " << vertices[i].z << std::endl;
        std::cout << "Vertices 2: " << cube.vertices[i * 3] << " " << cube.vertices[i * 3 + 1] << " " << cube.vertices[i * 3 + 2] << std::endl;
        std::cout << "\n\n";
    }


    float threshold = 0.1f;
    VertexIndices result = SimplifyMesh(cube, vertices, normals, threshold);

    Mesh wireframe = GenerateLODMesh(result, cube);

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 1.0f, -5.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(50);
    Vector3 lightPosition = { 0.0f, 2.0f, 0.0f };

    rlImGuiSetup(true);

    while (!WindowShouldClose()) {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);
        BeginDrawing();
        ClearBackground(GRAY);
        rlImGuiBegin();

        BeginMode3D(camera);
        DrawModel(LoadModelFromMesh(wireframe), { -0.5f, 0.0f, -0.5f }, 1.0f, RED);
        EndMode3D();

        DrawText("Press ESC to close", 10, 10, 20, WHITE);
        DrawText(TextFormat("Faces: %d", result.indices.size() / 3), 10, 30, 20, WHITE);
        DrawText(TextFormat("Vertices: %d", result.vertices.size()), 10, 50, 20, WHITE);

        ImGui::Begin("Inspector Window", NULL);
        if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, .5))
        {
            VertexIndices result = SimplifyMesh(cube, vertices, normals, threshold);
            wireframe = GenerateLODMesh(result, cube);
        }
        ImGui::End();

        rlImGuiEnd();
        EndDrawing();
    }

    UnloadMesh(cube);
    UnloadMesh(wireframe);
    CloseWindow();

    return 0;
}

