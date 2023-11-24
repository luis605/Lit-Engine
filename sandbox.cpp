#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <iostream>
#include <map>

#include "include/rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"

struct Vertex {
    float Q[4][4];
    Vector3 position;
};

struct Edge {
    Vertex v1;
    Vertex v2;
};

struct VertexIndices {
    std::vector<size_t> indices;
    std::vector<Vector3> vertices;
    std::vector<Vector3> normals;
};

void ContractEdge(std::pair<Vertex, Vertex>& edge) {
    // Calculate the midpoint of the edge
    Vector3 midpoint = Vector3Lerp(edge.first.position, edge.second.position, 0.5f);

    // Update the position of both vertices to the midpoint
    edge.first.position = midpoint;
}


VertexIndices ContractMesh(const Mesh& sourceMesh) {
    VertexIndices result;

    // Create a vector of edges from the source mesh
    std::vector<std::pair<Vertex, Vertex>> edges;

    for (size_t i = 0; i < sourceMesh.vertexCount; i += 3) {
        Vertex vertex1;
        vertex1.position.x = sourceMesh.vertices[i * 3];
        vertex1.position.y = sourceMesh.vertices[i * 3 + 1];
        vertex1.position.z = sourceMesh.vertices[i * 3 + 2];

        Vertex vertex2;
        vertex2.position.x = sourceMesh.vertices[i * 3 + 3];
        vertex2.position.y = sourceMesh.vertices[i * 3 + 4];
        vertex2.position.z = sourceMesh.vertices[i * 3 + 5];

        Vertex vertex3;
        vertex3.position.x = sourceMesh.vertices[i * 3 + 6];
        vertex3.position.y = sourceMesh.vertices[i * 3 + 7];
        vertex3.position.z = sourceMesh.vertices[i * 3 + 8];

        edges.emplace_back(vertex2, vertex3);
        edges.emplace_back(vertex1, vertex2);
        edges.emplace_back(vertex1, vertex3);
    }

    // Contract edges
    for (auto& edge : edges) {
        ContractEdge(edge);
    }

    // Create a vertices vector and indices vector from the contracted edges
    for (const auto& edge : edges) {
        result.vertices.push_back(edge.first.position);
        result.vertices.push_back(edge.second.position);
        result.indices.push_back(static_cast<unsigned int>(result.indices.size()));
        result.indices.push_back(static_cast<unsigned int>(result.indices.size()));
    }

    // Calculate normals for the contracted vertices
    for (const auto& vertex : result.vertices) {
        result.normals.push_back(Vector3Normalize(vertex));
    }

    return result;
}

Mesh GenerateLODMesh(const VertexIndices& meshData, const Mesh& sourceMesh) {
    Mesh lodMesh = {0};

    if (!meshData.vertices.empty()) {
        int vertexCount = meshData.vertices.size();
        int triangleCount = vertexCount / 3;
        int indexCount = triangleCount * 3;

        // Allocate memory for the new mesh
        lodMesh.vertexCount = vertexCount;
        lodMesh.triangleCount = triangleCount;
        lodMesh.vertices = (float*)malloc(sizeof(float) * 3 * vertexCount);
        lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indexCount);
        lodMesh.normals = (float*)malloc(sizeof(float) * 3 * vertexCount);

        // Assign vertices and normals directly to the mesh
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = meshData.vertices[i].x;
            lodMesh.vertices[i * 3 + 1] = meshData.vertices[i].y;
            lodMesh.vertices[i * 3 + 2] = meshData.vertices[i].z;

            lodMesh.normals[i * 3] = meshData.normals[i].x;
            lodMesh.normals[i * 3 + 1] = meshData.normals[i].y;
            lodMesh.normals[i * 3 + 2] = meshData.normals[i].z;
        }

        // Create indices for the contracted mesh
        for (int i = 0; i < indexCount; i++) {
            lodMesh.indices[i] = meshData.indices[i];
        }
    }

    // Upload the mesh data to GPU
    UploadMesh(&lodMesh, false);

    // Free the allocated memory before returning
    if (lodMesh.vertices) {
        free(lodMesh.vertices);
        lodMesh.vertices = NULL;
    }

    if (lodMesh.normals) {
        free(lodMesh.normals);
        lodMesh.normals = NULL;
    }

    return lodMesh;
}

int main() {
    int screenWidth = 800;
    int screenHeight = 450;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "QEM Mesh Simplification");

    Mesh sphere = GenMeshSphere(1, 32, 32); // Increased subdivision for a smoother sphere

    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");

    Model model;

    float threshold = 0.1f;

    model = LoadModelFromMesh(sphere);
    model.materials[0].shader = shader;

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 1.0f, -5.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(50);

    rlImGuiSetup(true);

    VertexIndices result;

    while (!WindowShouldClose()) {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);

        BeginDrawing();
        ClearBackground(GRAY);
        rlImGuiBegin();

        BeginMode3D(camera);
        DrawModel(model, { -0.5f, 0.0f, -0.5f }, 1.0f, RED);
        EndMode3D();

        DrawText("Press ESC to close", 10, 10, 20, WHITE);
        DrawText(TextFormat("Decimated Vertex Count: %d", result.vertices.size()), 10, 50, 20, WHITE);
        DrawText(TextFormat("Original Vertex Count: %d", sphere.vertexCount), 10, 80, 20, WHITE);

        ImGui::Begin("Inspector Window", NULL);
        if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, .5)) {
            // Collapse edges (call function)
            result = ContractMesh(sphere);
            model = LoadModelFromMesh(GenerateLODMesh(result, sphere));

            if (threshold == .5)
                model = LoadModelFromMesh(sphere);

            model.materials[0].shader = shader;

        }
        ImGui::End();

        rlImGuiEnd();
        EndDrawing();
    }

    UnloadMesh(sphere);
    CloseWindow();

    return 0;
}
