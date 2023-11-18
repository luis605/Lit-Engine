#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <unordered_map>
#include <map>
#include <queue>
#include <limits>
#include <iostream>

// Half-edge data structure for mesh representation
struct HalfEdge {
    int vertex;   // Vertex index
    int pair;     // Index of the paired half-edge
    int face;     // Index of the incident face
    int next;     // Next half-edge in the face
};

// QEM matrix data structure
struct QEMMatrix {
    float matrix[10];  // QEM matrix coefficients
};

// Vertex data structure with associated QEM matrix
struct VertexData {
    Vector3 position;
    Vector3 normal;
    QEMMatrix qem;
    bool isDeleted;  // Flag to mark deleted vertices
};

// This function initializes the half-edge data structure from the mesh
std::vector<HalfEdge> InitializeHalfEdges(const Mesh& mesh) {
    std::vector<HalfEdge> halfEdges(mesh.vertexCount * 6);

    for (int i = 0; i < mesh.triangleCount; ++i) {
        int baseIndex = i * 3;
        for (int j = 0; j < 3; ++j) {
            int edgeIndex = baseIndex + j;
            halfEdges[edgeIndex * 2].vertex = mesh.indices[baseIndex + j];
            halfEdges[edgeIndex * 2].next = edgeIndex * 2 + 1;
            halfEdges[edgeIndex * 2].face = i;

            halfEdges[edgeIndex * 2 + 1].vertex = mesh.indices[baseIndex + (j + 1) % 3];
            halfEdges[edgeIndex * 2 + 1].next = edgeIndex * 2;
            halfEdges[edgeIndex * 2 + 1].face = i;
        }
    }

    // Pair half-edges
    for (size_t i = 0; i < halfEdges.size(); ++i) {
        int vertex = halfEdges[i].vertex;
        int nextVertex = halfEdges[halfEdges[i].next].vertex;

        for (size_t j = 0; j < halfEdges.size(); ++j) {
            if (i != j && vertex == halfEdges[j].vertex && nextVertex == halfEdges[halfEdges[j].next].vertex) {
                halfEdges[i].pair = j;
                halfEdges[j].pair = i;
                break;
            }
        }
    }

    return halfEdges;
}

// This function initializes the QEM matrix for each vertex
std::vector<VertexData> InitializeQEMMatrices(const Mesh& mesh, const std::vector<HalfEdge>& halfEdges) {
    std::vector<VertexData> vertices(mesh.vertexCount);

    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position.x = mesh.vertices[i * 3];
        vertices[i].position.y = mesh.vertices[i * 3 + 1];
        vertices[i].position.z = mesh.vertices[i * 3 + 2];

        vertices[i].normal.x = mesh.normals[i * 3];
        vertices[i].normal.y = mesh.normals[i * 3 + 1];
        vertices[i].normal.z = mesh.normals[i * 3 + 2];

        // Initialize QEM matrix as identity
        for (int j = 0; j < 10; ++j) {
            vertices[i].qem.matrix[j] = 0.0f;
        }
        vertices[i].qem.matrix[0] = 1.0f;
        vertices[i].qem.matrix[4] = 1.0f;
        vertices[i].qem.matrix[8] = 1.0f;
    }

    // Accumulate QEM matrices from faces
    for (size_t i = 0; i < halfEdges.size(); ++i) {
        int vertex = halfEdges[i].vertex;
        int nextVertex = halfEdges[halfEdges[i].next].vertex;
        int pairVertex = halfEdges[halfEdges[i].pair].vertex;

        Vector3 normal = Vector3CrossProduct(Vector3Subtract(vertices[nextVertex].position, vertices[vertex].position),
                                             Vector3Subtract(vertices[pairVertex].position, vertices[vertex].position));

        for (int j = 0; j < 10; ++j) {
            vertices[vertex].qem.matrix[j] += normal.x * normal.x;
        }
        vertices[vertex].qem.matrix[1] += normal.x * normal.y * 2.0f;
        vertices[vertex].qem.matrix[2] += normal.x * normal.z * 2.0f;
        vertices[vertex].qem.matrix[5] += normal.y * normal.y;
        vertices[vertex].qem.matrix[6] += normal.y * normal.z * 2.0f;
        vertices[vertex].qem.matrix[9] += normal.z * normal.z;
    }

    return vertices;
}

// This function computes the error for a given vertex collapse
float ComputeError(const VertexData& v1, const VertexData& v2) {
    QEMMatrix qemSum;
    for (int i = 0; i < 10; ++i) {
        qemSum.matrix[i] = v1.qem.matrix[i] + v2.qem.matrix[i];
    }

    float error = 0.0f;
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            error += qemSum.matrix[i] * v1.qem.matrix[i];
        }
    }

    return error;
}

// This function collapses a pair of vertices and updates the mesh
void CollapseVertices(std::vector<VertexData>& vertices, std::vector<HalfEdge>& halfEdges, int collapseEdge) {
    int vertex1 = halfEdges[collapseEdge].vertex;
    int vertex2 = halfEdges[halfEdges[collapseEdge].pair].vertex;

    // Update the position and normal of vertex1
    vertices[vertex1].position = Vector3Scale(Vector3Add(vertices[vertex1].position, vertices[vertex2].position), 0.5f);
    vertices[vertex1].normal = Vector3Normalize(Vector3Add(vertices[vertex1].normal, vertices[vertex2].normal));

    // Update the QEM matrix of vertex1
    for (int i = 0; i < 10; ++i) {
        vertices[vertex1].qem.matrix[i] += vertices[vertex2].qem.matrix[i];
    }

    // Mark vertex2 as deleted
    vertices[vertex2].isDeleted = true;

    // Update adjacent half-edges
    for (size_t i = 0; i < halfEdges.size(); ++i) {
        if (halfEdges[i].vertex == vertex2 && !vertices[halfEdges[i].pair].isDeleted) {
            halfEdges[i].vertex = vertex1;
            halfEdges[halfEdges[i].pair].vertex = vertex1;
        }
    }
}

// This function simplifies the mesh using the QEM algorithm
std::vector<VertexData> SimplifyMesh(const Mesh& mesh, float threshold) {
    // Initialize half-edge data structure
    std::vector<HalfEdge> halfEdges = InitializeHalfEdges(mesh);

    // Initialize QEM matrices
    std::vector<VertexData> vertices = InitializeQEMMatrices(mesh, halfEdges);

    // Priority queue for edge collapses
    std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> queue;

    // Initialize the queue with edge collapses
    for (size_t i = 0; i < halfEdges.size(); ++i) {
        int vertex1 = halfEdges[i].vertex;
        int vertex2 = halfEdges[halfEdges[i].pair].vertex;

        if (!vertices[vertex1].isDeleted && !vertices[vertex2].isDeleted) {
            float error = ComputeError(vertices[vertex1], vertices[vertex2]);
            queue.push(std::make_pair(error, i));
        }
    }

    // Perform edge collapses until the threshold is reached
    while (!queue.empty()) {
        float error;
        int collapseEdge;
        std::tie(error, collapseEdge) = queue.top();
        queue.pop();

        if (error < threshold) {
            CollapseVertices(vertices, halfEdges, collapseEdge);
            
            // Update the priority queue with new edge collapses
            for (size_t i = 0; i < halfEdges.size(); ++i) {
                int vertex1 = halfEdges[i].vertex;
                int vertex2 = halfEdges[halfEdges[i].pair].vertex;

                if (!vertices[vertex1].isDeleted && !vertices[vertex2].isDeleted) {
                    float newError = ComputeError(vertices[vertex1], vertices[vertex2]);
                    queue.push(std::make_pair(newError, i));
                }
            }
        }
    }

    return vertices;
}

int main() {
    // Initialize the screen and camera
    int screenWidth = 800;
    int screenHeight = 450;
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "QEM Mesh Simplification");

    // Create a mesh
    Mesh cube = GenMeshSphere(1,30,30);

    // Simplify the mesh
    std::vector<VertexData> simplifiedVertices = SimplifyMesh(cube, 0.1f);

    // Generate a wireframe model from the simplified mesh
    Mesh wireframe = { 0 };
    wireframe.vertexCount = static_cast<int>(simplifiedVertices.size());
    wireframe.vertices = (float*)malloc(sizeof(float) * 3 * wireframe.vertexCount);
    wireframe.normals = (float*)malloc(sizeof(float) * 3 * wireframe.vertexCount);
    wireframe.indices = (unsigned short*)malloc(sizeof(unsigned short) * wireframe.vertexCount);

    for (size_t i = 0; i < simplifiedVertices.size(); ++i) {
        wireframe.vertices[i * 3] = simplifiedVertices[i].position.x;
        wireframe.vertices[i * 3 + 1] = simplifiedVertices[i].position.y;
        wireframe.vertices[i * 3 + 2] = simplifiedVertices[i].position.z;

        wireframe.normals[i * 3] = simplifiedVertices[i].normal.x;
        wireframe.normals[i * 3 + 1] = simplifiedVertices[i].normal.y;
        wireframe.normals[i * 3 + 2] = simplifiedVertices[i].normal.z;

        wireframe.indices[i] = static_cast<unsigned short>(i);
    }

    UploadMesh(&wireframe, false);

    // Set up the camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 1.0f, -5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);

    // Main game loop
    while (!WindowShouldClose()) {
        // Update the camera
        UpdateCamera(&camera, CAMERA_FREE);

        // Draw the wireframe mesh
        BeginDrawing();
        ClearBackground(GRAY);

        BeginMode3D(camera);
        DrawModel(LoadModelFromMesh(wireframe), Vector3Zero(), 1, WHITE);
        DrawSphere(Vector3Zero(), .1, RED);
        EndMode3D();

        // Draw the UI
        DrawText("Press ESC to close", 10, 10, 20, GRAY);
        DrawText(TextFormat("Vertices: %d", wireframe.vertexCount), 10, 30, 20, GRAY);

        EndDrawing();
    }

    // Clean up
    UnloadMesh(cube);
    UnloadMesh(wireframe);
    CloseWindow();

    return 0;
}
