#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>
#include "raylib.h"
#include "raymath.h"
#include "include/rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"

// Constants
const int screenWidth = 1200;
const int screenHeight = 450;

// Structure to represent half-edge data
struct HalfEdge {
    int vertexIndex;
    int pairIndex;
    int nextIndex;
    bool isBoundary;
};

// Function to initialize half-edges from vertices
std::vector<HalfEdge> initializeHalfEdges(const std::vector<Vector3>& vertices) {
    std::vector<HalfEdge> halfEdges;

    // Iterate through triangles in the vertices
    for (size_t i = 0; i < vertices.size(); i += 3) {
        // Create three half-edges for each triangle
        for (int j = 0; j < 3; ++j) {
            HalfEdge he;
            he.vertexIndex = i + j;
            he.nextIndex = i + (j + 1) % 3;
            he.pairIndex = -1;
            he.isBoundary = true;
            halfEdges.push_back(he);
        }
    }

    // Connect pairs
    for (size_t i = 0; i < halfEdges.size(); ++i) {
        HalfEdge& he = halfEdges[i];

        for (size_t j = i + 1; j < halfEdges.size(); ++j) {
            HalfEdge& other = halfEdges[j];

            if (he.vertexIndex == other.nextIndex && he.nextIndex == other.vertexIndex) {
                he.pairIndex = j;
                other.pairIndex = i;
                he.isBoundary = false;
                other.isBoundary = false;
                break;
            }
        }
    }

    return halfEdges;
}

// Function to collapse a half-edge based on distance threshold
void halfEdgeCollapse(std::vector<HalfEdge>& halfEdges, std::vector<Vector3>& vertices, float threshold) {
    // For simplicity, assume vertex at index 1 collapses into vertex at index 0
    size_t vertexToKeep = 0;
    size_t vertexToRemove = 1;

    // Calculate the distance between the two vertices
    float distance = Vector3Distance(vertices[vertexToKeep], vertices[vertexToRemove]);

    // Check if the distance is below the threshold
    if (distance <= threshold) {
        // Collapse the vertex at index 'vertexToRemove' into the vertex at index 'vertexToKeep'
        vertices.erase(vertices.begin() + vertexToRemove);

        // Update half-edges
        for (HalfEdge& he : halfEdges) {
            if (he.vertexIndex > vertexToRemove) {
                he.vertexIndex--;
            }
            if (he.nextIndex > vertexToRemove) {
                he.nextIndex--;
            }
            if (he.pairIndex > vertexToRemove) {
                he.pairIndex--;
            }
        }

        // Remove edges connected to the removed vertex
        halfEdges.erase(std::remove_if(halfEdges.begin(), halfEdges.end(),
                                       [vertexToRemove](const HalfEdge& he) {
                                           return he.vertexIndex == vertexToRemove || he.nextIndex == vertexToRemove;
                                       }),
                        halfEdges.end());
    }
}

// Function to compute indices from vertices
std::vector<unsigned short> computeIndices(const std::vector<Vector3>& vertices) {
    std::vector<unsigned short> indices;
    // For simplicity, assume vertices are still ordered as triangles
    for (size_t i = 0; i < vertices.size(); i += 3) {
        indices.push_back(static_cast<unsigned short>(i));
        indices.push_back(static_cast<unsigned short>(i + 1));
        indices.push_back(static_cast<unsigned short>(i + 2));
    }
    return indices;
}

Mesh generateLODMesh(const std::vector<Vector3>& vertices, const std::vector<unsigned short>& indices) {
    Mesh lodMesh = { 0 };

    if (!vertices.empty() && !indices.empty()) {
        int vertexCount = static_cast<int>(vertices.size());
        int triangleCount = static_cast<int>(indices.size()) / 3;

        lodMesh.vertexCount = vertexCount;
        lodMesh.triangleCount = triangleCount;
        lodMesh.vertices = (float*)malloc(sizeof(float) * 3 * vertexCount);
        lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indices.size());
        lodMesh.normals = (float*)malloc(sizeof(float) * 3 * vertexCount);

        // Calculate normals
        for (int i = 0; i < triangleCount; ++i) {
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(
                Vector3Subtract(vertices[indices[i * 3 + 1]], vertices[indices[i * 3]]),
                Vector3Subtract(vertices[indices[i * 3 + 2]], vertices[indices[i * 3]])));

            lodMesh.normals[indices[i * 3] * 3] += normal.x;
            lodMesh.normals[indices[i * 3] * 3 + 1] += normal.y;
            lodMesh.normals[indices[i * 3] * 3 + 2] += normal.z;

            lodMesh.normals[indices[i * 3 + 1] * 3] += normal.x;
            lodMesh.normals[indices[i * 3 + 1] * 3 + 1] += normal.y;
            lodMesh.normals[indices[i * 3 + 1] * 3 + 2] += normal.z;

            lodMesh.normals[indices[i * 3 + 2] * 3] += normal.x;
            lodMesh.normals[indices[i * 3 + 2] * 3 + 1] += normal.y;
            lodMesh.normals[indices[i * 3 + 2] * 3 + 2] += normal.z;
        }

        for (int i = 0; i < vertexCount; ++i) {
            lodMesh.normals[i * 3] /= 3.0f;
            lodMesh.normals[i * 3 + 1] /= 3.0f;
            lodMesh.normals[i * 3 + 2] /= 3.0f;
        }

        // Copy vertices and indices
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = vertices[i].x;
            lodMesh.vertices[i * 3 + 1] = vertices[i].y;
            lodMesh.vertices[i * 3 + 2] = vertices[i].z;
        }

        for (size_t i = 0; i < indices.size(); i++) {
            lodMesh.indices[i] = indices[i];
        }
    }

    UploadMesh(&lodMesh, false);

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
    // Initialize window and camera
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Half-Edge Collapsing");

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 1.0f, -5.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Load shader, mesh, and model
    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");
    Mesh mesh = GenMeshSphere(1, 15, 15);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].shader = shader;

    // Initialize vertices and half-edges
    std::vector<Vector3> vertices;
    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.vertices[i * 3];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];
        vertices.push_back({ x, y, z });
    }

    std::vector<HalfEdge> halfEdges = initializeHalfEdges(vertices);

    // Set threshold and other configurations
    float threshold = 0.0;
    SetTargetFPS(50);
    rlImGuiSetup(true);

    // Main game loop
    while (!WindowShouldClose()) {
        // Update camera if right mouse button is pressed
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);

        // Update vertices and perform half-edge collapse
        vertices.clear();
        for (int i = 0; i < mesh.vertexCount; i++) {
            float x = mesh.vertices[i * 3];
            float y = mesh.vertices[i * 3 + 1];
            float z = mesh.vertices[i * 3 + 2];
            vertices.push_back({ x, y, z });
        }
        halfEdgeCollapse(halfEdges, vertices, threshold);

        // Compute new indices
        std::vector<unsigned short> newIndices = computeIndices(vertices);

        // Begin drawing
        BeginDrawing();
        ClearBackground(GRAY);

        // Begin 3D mode and draw model
        BeginMode3D(camera);
        if (IsModelReady(model))
            DrawModel(model, { -0.5f, 0.0f, -0.5f }, 1.0f, RED);
        EndMode3D();

        // Draw text information
        DrawText("Half-Edge Collapsing", 10, 10, 20, BLACK);
        DrawText(TextFormat("Collapsed Vertices: %d", mesh.vertexCount - static_cast<int>(vertices.size())), 10, 40, 20, BLACK);
        for (size_t i = 0; i < vertices.size(); ++i) {
            DrawText(TextFormat("Vertex %d: [%.2f, %.2f, %.2f]", i, vertices[i].x, vertices[i].y, vertices[i].z), 10, 70 + 30 * i, 20, BLACK);
        }

        // ImGui inspector window
        rlImGuiBegin();
        ImGui::Begin("Inspector Window", NULL);
        if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, 1)) {
            halfEdgeCollapse(halfEdges, vertices, threshold);
            model = LoadModelFromMesh(generateLODMesh(vertices, newIndices));

            if (threshold == 0) {
                vertices.clear();
                for (int i = 0; i < mesh.vertexCount; i++) {
                    float x = mesh.vertices[i * 3];
                    float y = mesh.vertices[i * 3 + 1];
                    float z = mesh.vertices[i * 3 + 2];
                    vertices.push_back({ x, y, z });
                }
                newIndices = computeIndices(vertices);
                model = LoadModelFromMesh(generateLODMesh(vertices, newIndices));
            }

            model.materials[0].shader = shader;
        }
        ImGui::End();
        rlImGuiEnd();

        // End drawing
        EndDrawing();
    }

    // Close window
    CloseWindow();

    return 0;
}