#include <algorithm>
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <iostream>

#include "include/rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"


struct HalfEdge {
    int vertexIndex;  
    int pairIndex;    
    int faceIndex;    
    bool boundary;     
};

std::vector<HalfEdge> initializeHalfEdges(const std::vector<Vector3>& vertices, const std::vector<Vector3>& normals) {
    std::vector<HalfEdge> halfEdges;

    // Initialize half-edges
    for (size_t i = 0; i < vertices.size(); ++i) {
        HalfEdge he;
        he.vertexIndex = static_cast<int>(i);
        he.pairIndex = -1;  // Initialize pair index to -1
        he.faceIndex = -1;  // Initialize face index to -1
        he.boundary = false; // Initialize boundary flag to false
        halfEdges.push_back(he);
    }

    // Assign pair indices to half-edges
    for (size_t i = 0; i < halfEdges.size(); ++i) {
        halfEdges[i].pairIndex = static_cast<int>((i + 1) % halfEdges.size());
    }

    return halfEdges;
}

void collapseEdge(std::vector<HalfEdge>& halfEdges, int edgeIndex, std::vector<Vector3>& vertices) {
    // Get the two vertices associated with the edge
    int v1 = halfEdges[edgeIndex].vertexIndex;
    int v2 = halfEdges[halfEdges[edgeIndex].pairIndex].vertexIndex;

    // Calculate the midpoint of the edge
    Vector3 midpoint = Vector3Scale(Vector3Add(vertices[v1], vertices[v2]), 0.5f);

    // Update the position of the first vertex
    vertices[v1] = midpoint;

    // Invalidate the second vertex (it will be removed later)
    vertices[v2] = { NAN, NAN, NAN };

    // Update the pair index of the affected edges
    halfEdges[halfEdges[edgeIndex].pairIndex].pairIndex = halfEdges[edgeIndex].pairIndex;

    // Invalidate the edge (it will be removed later)
    halfEdges[edgeIndex].vertexIndex = -1;
    halfEdges[halfEdges[edgeIndex].pairIndex].vertexIndex = -1;
}

void removeInvalidVertices(std::vector<Vector3>& vertices, const std::vector<HalfEdge>& halfEdges) {
    auto invalidVertex = [](const Vector3& v) { return std::isnan(v.x); };

    // Remove vertices marked as invalid
    vertices.erase(std::remove_if(vertices.begin(), vertices.end(), invalidVertex), vertices.end());
}

void halfEdgeCollapse(std::vector<HalfEdge>& halfEdges, std::vector<Vector3>& vertices, float threshold) {
    // Identify edges to collapse based on the threshold
    for (size_t i = 0; i < halfEdges.size(); ++i) {
        int v1 = halfEdges[i].vertexIndex;
        int v2 = halfEdges[halfEdges[i].pairIndex].vertexIndex;

        if (v1 != -1 && v2 != -1) {
            float distance = Vector3Distance(vertices[v1], vertices[v2]);

            if (distance < threshold) {
                collapseEdge(halfEdges, static_cast<int>(i), vertices);
            }
        }
    }

    // Remove invalid vertices and update indices
    removeInvalidVertices(vertices, halfEdges);
}

std::vector<unsigned short> computeIndices(const std::vector<Vector3>& vertices) {
    std::vector<unsigned short> indices;

    // Assuming vertices form a triangle list
    for (size_t i = 0; i < vertices.size(); ++i) {
        indices.push_back(static_cast<unsigned short>(i));
    }

    return indices;
}

Mesh GenerateLODMesh(const std::vector<Vector3> vertices, const std::vector<Vector3> normals, const std::vector<unsigned short>& indices) {
    Mesh lodMesh = { 0 };

    if (!vertices.empty() && !indices.empty()) {
        int vertexCount = vertices.size();
        int triangleCount = indices.size() / 3;

        // Allocate memory for the new mesh
        lodMesh.vertexCount = vertexCount;
        lodMesh.triangleCount = triangleCount;
        lodMesh.vertices = (float*)malloc(sizeof(float) * 3 * vertexCount);
        lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indices.size());
        lodMesh.normals = (float*)malloc(sizeof(float) * 3 * vertexCount);

        // Assign vertices and normals directly to the mesh
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = vertices[i].x;
            lodMesh.vertices[i * 3 + 1] = vertices[i].y;
            lodMesh.vertices[i * 3 + 2] = vertices[i].z;

            lodMesh.normals[i * 3] = normals[i].x;
            lodMesh.normals[i * 3 + 1] = normals[i].y;
            lodMesh.normals[i * 3 + 2] = normals[i].z;
        }

        // Copy indices from the result mesh
        for (size_t i = 0; i < vertexCount; i++) {
            lodMesh.indices[i] = indices[i];
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
    const int screenWidth = 800;
    const int screenHeight = 450;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Half-Edge Collapsing");

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 1.0f, -5.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");

    Mesh mesh = GenMeshSphere(1, 7, 7);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].shader = shader;

    std::vector<Vector3> vertices;

    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.vertices[i * 3];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];
        vertices.push_back({ x, y, z });
    }

    std::vector<Vector3> normals;
    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.normals[i * 3];
        float y = mesh.normals[i * 3 + 1];
        float z = mesh.normals[i * 3 + 2];
        normals.push_back({ x, y, z });
    }

    std::vector<HalfEdge> halfEdges = initializeHalfEdges(vertices, normals);

    float threshold = 0.2;

    SetTargetFPS(50);
    rlImGuiSetup(true);

    while (!WindowShouldClose()) {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);

        // Perform half-edge collapsing
        halfEdgeCollapse(halfEdges, vertices, threshold);

        // Update the mesh structure with the new vertices
        for (size_t i = 0; i < vertices.size(); ++i) {
            mesh.vertices[i * 3] = vertices[i].x;
            mesh.vertices[i * 3 + 1] = vertices[i].y;
            mesh.vertices[i * 3 + 2] = vertices[i].z;
        }

        // Compute new indices
        std::vector<unsigned short> newIndices = computeIndices(vertices);

        BeginDrawing();
        ClearBackground(GRAY);

        BeginMode3D(camera);

        if (IsModelReady(model))
            DrawModel(model, { -0.5f, 0.0f, -0.5f }, 1.0f, RED);
    
        EndMode3D();

        DrawText("Half-Edge Collapsing", 10, 10, 20, BLACK);

        std::vector<Vector3> newVertices;
        for (const auto& vertex : vertices) {
            newVertices.push_back(vertex);
        }

        DrawText(TextFormat("Collapsed Vertices: %d", vertices.size() - newVertices.size()), 10, 40, 20, BLACK);

        for (size_t i = 0; i < newVertices.size(); ++i) {
            DrawText(TextFormat("Vertex %d: [%.2f, %.2f, %.2f]", i, newVertices[i].x, newVertices[i].y, newVertices[i].z), 10, 70 + 30 * i, 20, BLACK);
        }

        rlImGuiBegin();

        ImGui::Begin("Inspector Window", NULL);
        if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, .5)) {

            vertices.clear();
            normals.clear();
            
            for (int i = 0; i < mesh.vertexCount; i++)
            {
                float x = mesh.vertices[i * 3];
                float y = mesh.vertices[i * 3 + 1];
                float z = mesh.vertices[i * 3 + 2];
                vertices.push_back({ x, y, z });
            }

            for (int i = 0; i < mesh.vertexCount; i++)
            {
                float x = mesh.normals[i * 3];
                float y = mesh.normals[i * 3 + 1];
                float z = mesh.normals[i * 3 + 2];
                normals.push_back({ x, y, z });
            }
            
            halfEdgeCollapse(halfEdges, vertices, threshold);


            // Update the indices in the new mesh
            model = LoadModelFromMesh(GenerateLODMesh(vertices, normals, newIndices));
            model.materials[0].shader = shader;

            if (threshold == .5) {
                // Reset the model to the original mesh when threshold is maximum
                model = LoadModelFromMesh(mesh);
                model.materials[0].shader = shader;
            }
        }
        ImGui::End();

        rlImGuiEnd();

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
