#include <algorithm>
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <unordered_map>
#include <map>
#include <queue>
#include <limits>
#include <iostream>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"

struct VertexIndices {
    std::vector<size_t> indices;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
};

struct EdgeData {
    glm::vec3 normal;
    size_t indices[2];
    bool processed;
};

VertexIndices CollapseVertices(const std::vector<glm::vec3>& vertices, const std::vector<glm::vec3>& normals) {
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

float ComputeQuadraticError(const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& n1, const glm::vec3& n2) {
    glm::vec3 delta = v2 - v1;
    float distance = glm::length(delta);
    float dot = glm::dot(n1, n2);
    return distance * distance + (1.0f - dot);
}

std::pair<size_t, float> FindSmallestError(const std::vector<glm::vec3>& vertices, const std::vector<glm::vec3>& normals, const std::vector<EdgeData>& edges) {
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

VertexIndices SimplifyMesh(const Mesh mesh, const std::vector<glm::vec3> vertices, const std::vector<glm::vec3> normals, float threshold) {
    VertexIndices result;

    if (threshold == 0.5) {
        result.vertices = vertices;
        result.normals = normals;

        if (mesh.indices) {
            for (size_t i = 0; i < mesh.triangleCount * 3; i++) {
                result.indices.push_back(mesh.indices[i]);
            }
        }

        return result;
    }

    // Initialize the data structures for the QEM algorithm.
    std::vector<EdgeData> edges;
    for (size_t i = 0; i < vertices.size(); ++i) {
        for (size_t j = i + 1; j < vertices.size(); ++j) {
            EdgeData edge;
            edge.indices[0] = i;
            edge.indices[1] = j;
            edge.normal = glm::cross(normals[i], normals[j]);
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
            vertices[index0] = glm::mix(vertices[index0], vertices[index1], 0.5f);
            normals[index0] = glm::normalize(normals[index0] + normals[index1]);

            // Mark the edge as processed
            edges[i].processed = true;

            // Recompute the error for affected edges.
            for (size_t j = i + 1; j < edges.size(); ++j) {
                if (!edges[j].processed && (edges[j].indices[0] == index0 || edges[j].indices[1] == index0)) {
                    float newError = ComputeQuadraticError(vertices[edges[j].indices[0]], vertices[edges[j].indices[1]], normals[edges[j].indices[0]], normals[edges[j].indices[1]]);
                    if (newError < threshold * threshold) {
                        // Mark the edge as processed and update the vertices and normals directly.
                        edges[j].processed = true;
                        size_t otherIndex = (edges[j].indices[0] == index0) ? edges[j].indices[1] : edges[j].indices[0];
                        vertices[index0] = glm::mix(vertices[index0], vertices[otherIndex], 0.5f);
                        normals[index0] = glm::normalize(normals[index0] + normals[otherIndex]);
                    }
                }
            }
        }
    }

    // Directly update the result with the collapsed vertices.
    result = CollapseVertices(vertices, normals);

    return result;
}

Mesh GenerateLODMesh(const VertexIndices& meshData, const Mesh& sourceMesh) {
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
        for (int i = 0; i < meshData.indices.size(); i++) {
            lodMesh.indices[i] = meshData.indices[i];
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

    Mesh cube = GenMeshSphere(1, 6, 6);

    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;

    for (size_t i = 0; i < cube.vertexCount; ++i) {
        size_t baseIndex = i * 3;
        float x = cube.vertices[baseIndex];
        float y = cube.vertices[baseIndex + 1];
        float z = cube.vertices[baseIndex + 2];
        float nx = cube.normals[baseIndex];
        float ny = cube.normals[baseIndex + 1];
        float nz = cube.normals[baseIndex + 2];

        vertices.push_back(glm::vec3(x, y, z));
        normals.push_back(glm::vec3(nx, ny, nz));
    }

    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");

    Model model;

    float threshold = 0.1f;
    VertexIndices result = SimplifyMesh(cube, vertices, normals, threshold);

    Mesh wireframe = GenerateLODMesh(result, cube);

    model = LoadModelFromMesh(wireframe);
    model.materials[0].shader = shader;

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
        DrawModel(model, { -0.5f, 0.0f, -0.5f }, 1.0f, RED);
        EndMode3D();

        DrawText("Press ESC to close", 10, 10, 20, WHITE);
        DrawText(TextFormat("Faces: %d", result.indices.size() / 3), 10, 30, 20, WHITE);
        DrawText(TextFormat("Vertices: %d", result.vertices.size()), 10, 50, 20, WHITE);

        ImGui::Begin("Inspector Window", NULL);
        if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, .5))
        {
            VertexIndices result = SimplifyMesh(cube, vertices, normals, threshold);
            wireframe = GenerateLODMesh(result, cube);
            model = LoadModelFromMesh(wireframe);
            model.materials[0].shader = shader;
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
