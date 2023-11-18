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

VertexIndices CollapseVertices(const std::vector<Vector3>& vertices, const std::vector<Vector3>& normals) {
    VertexIndices result;
    std::unordered_map<size_t, size_t> indexMap;

    for (size_t i = 0; i < vertices.size(); ++i) {
        auto iter = indexMap.find(i);
        if (iter == indexMap.end()) {
            result.indices.push_back(i);
            result.vertices.push_back(vertices[i]);
            result.normals.push_back(normals[i]);
            indexMap[i] = result.vertices.size() - 1;
        }
    }

    return result;
}

float ComputeQuadraticError(const Vector3& v1, const Vector3& v2, const Vector3& n1, const Vector3& n2) {
    Vector3 delta = Vector3Subtract(v2, v1);
    float distance = Vector3Length(delta);
    float dot = Vector3DotProduct(n1, n2);
    return distance * distance * (1.0f - dot * dot);
}

std::pair<size_t, float> FindSmallestError(const std::vector<Vector3>& vertices, const std::vector<Vector3>& normals, const std::vector<EdgeData>& edges) {
    float minError = std::numeric_limits<float>::max();
    size_t minIndex = 0;

    for (size_t i = 0; i < edges.size(); ++i) {
        float error = ComputeQuadraticError(vertices[edges[i].indices[0]], vertices[edges[i].indices[1]], normals[edges[i].indices[0]], normals[edges[i].indices[1]]);
        if (error < minError) {
            minError = error;
            minIndex = i;
        }
    }

    return std::make_pair(minIndex, minError);
}

VertexIndices SimplifyMesh(const std::vector<Vector3> vertices, const std::vector<Vector3> normals, float threshold) {
    VertexIndices result;

    // Initialize the data structures for the QEM algorithm.
    std::vector<EdgeData> edges;
    for (size_t i = 0; i < vertices.size(); ++i) {
        for (size_t j = i + 1; j < vertices.size(); ++j) {
            EdgeData edge;
            edge.indices[0] = i;
            edge.indices[1] = j;
            edge.normal = Vector3CrossProduct(normals[i], normals[j]);
            edges.push_back(edge);
        }
    }

    // Sort edges based on error (ascending order).
    std::sort(edges.begin(), edges.end(), [&](const EdgeData& a, const EdgeData& b) {
        float errorA = ComputeQuadraticError(vertices[a.indices[0]], vertices[a.indices[1]], normals[a.indices[0]], normals[a.indices[1]]);
        float errorB = ComputeQuadraticError(vertices[b.indices[0]], vertices[b.indices[1]], normals[b.indices[0]], normals[b.indices[1]]);
        return errorA < errorB;
    });

    // Process the edges.
    for (size_t i = 0; i < edges.size(); ++i) {
        float error = ComputeQuadraticError(vertices[edges[i].indices[0]], vertices[edges[i].indices[1]], normals[edges[i].indices[0]], normals[edges[i].indices[1]]);

        if (error < threshold) {
            // Update the edge information.
            size_t index0 = edges[i].indices[0];
            size_t index1 = edges[i].indices[1];

            // Update the vertices and normals directly.
            vertices[index0] = Vector3Lerp(vertices[index0], vertices[index1], 0.5f);
            normals[index0] = Vector3Lerp(normals[index0], normals[index1], 0.5f);

            // Mark the edge as processed
            edges[i].processed = true;

            // Recompute the error for affected edges.
            for (size_t j = i + 1; j < edges.size(); ++j) {
                if (!edges[j].processed && (edges[j].indices[0] == index0 || edges[j].indices[1] == index0)) {
                    float newError = ComputeQuadraticError(vertices[edges[j].indices[0]], vertices[edges[j].indices[1]], normals[edges[j].indices[0]], normals[edges[j].indices[1]]);
                    if (newError < threshold) {
                        // Mark the edge as processed and update the vertices and normals directly.
                        edges[j].processed = true;
                        size_t otherIndex = (edges[j].indices[0] == index0) ? edges[j].indices[1] : edges[j].indices[0];
                        vertices[index0] = Vector3Lerp(vertices[index0], vertices[otherIndex], 0.5f);
                        normals[index0] = Vector3Lerp(normals[index0], normals[otherIndex], 0.5f);
                    }
                }
            }
        }
    }

    // Directly update the result with the collapsed vertices.
    result = CollapseVertices(vertices, normals);

    return result;
}


Mesh GenerateLODMesh(const VertexIndices& meshData, const Mesh sourceMesh) {
    Mesh lodMesh = { 0 };

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

        // Generate new indices for non-indexed mesh
        if (sourceMesh.indices) {
            // Allocate memory for the indices
            lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indexCount);

            // Copy indices from the source mesh
            for (int i = 0; i < indexCount; i++) {
                lodMesh.indices[i] = meshData.indices.at(i);
            }
        }
        else {
            lodMesh.indices = sourceMesh.indices;
        }
    }

    // Upload the mesh data to GPU
    UploadMesh(&lodMesh, false);

    // Free the allocated memory before returning
    if (lodMesh.vertices) {
        free(lodMesh.vertices);
        lodMesh.vertices = NULL;
    }
    if (lodMesh.indices) {
        free(lodMesh.indices);
        lodMesh.indices = NULL;
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

    Mesh cube = GenMeshSphere(1,5,5);

    std::vector<Vector3> vertices;
    std::vector<Vector3> normals;

    for (size_t i = 0; i < cube.vertexCount; ++i) {
        size_t baseIndex = i * 6;
        float x = cube.vertices[baseIndex];
        float y = cube.vertices[baseIndex + 1];
        float z = cube.vertices[baseIndex + 2];
        float nx = cube.normals[baseIndex];
        float ny = cube.normals[baseIndex + 1];
        float nz = cube.normals[baseIndex + 2];

        vertices.push_back({ x, y, z });
        normals.push_back({ nx, ny, nz });
    }

    float threshold = 0.1f;
    VertexIndices result = SimplifyMesh(vertices, normals, threshold);

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
            VertexIndices result = SimplifyMesh(vertices, normals, threshold);
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

