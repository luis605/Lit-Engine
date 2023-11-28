#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>
#include "raylib.h"
#include "raymath.h"

#include "include/rlImGui.h"
#include "imgui.h"


const int screenWidth = 1200;
const int screenHeight = 450;

/*
        ALGORITHM OVERVIEW 

Select the edge with the minimum cost, collapse it, and re-evaluate
Substitute an edge { v0, v1 } with a new vertex, at the midpoint of the 2 vertices

FIRST STEP:
Assign costs to all edges in the mesh, which are maintained in a priority queue

SECOND STEP:
Collapse the edge with the lowest cost

THIRD STEP:
Re-evaluate the costs of all edges in the mesh

FOURTH STEP:
Repeat the process until no more edges can be collapsed (no cost lower than a threshold)

*/

struct Edge {
    float cost;
    Vector3 v0;
    Vector3 v1;

    Vector3 midpoint;
};

struct HalfEdge {
    int vertexIndex;
    int pairIndex;
    int nextIndex;
    bool isBoundary;
};


std::vector<HalfEdge> initializeHalfEdges(const std::vector<Vector3>& vertices) {
    std::vector<HalfEdge> halfEdges;

    
    for (size_t i = 0; i < vertices.size(); i += 3) {
        
        for (int j = 0; j < 3; ++j) {
            HalfEdge he;
            he.vertexIndex = i + j;
            he.nextIndex = i + (j + 1) % 3;
            he.pairIndex = -1;
            he.isBoundary = true;
            halfEdges.push_back(he);
        }
    }

    
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

void calculateEdgeCost(Edge& edge) {
    Vector3 midpoint = Vector3Add(edge.v0, edge.v1);
    midpoint.x /= 2.0f;
    midpoint.y /= 2.0f;
    midpoint.z /= 2.0f;

    // Calculate the distance between the original edge midpoint and the new midpoint
    edge.cost = sqrt(pow(midpoint.x - edge.v0.x, 2) + pow(midpoint.y - edge.v0.y, 2) + pow(midpoint.z - edge.v0.z, 2));
}

void halfEdgeCollapse(std::vector<HalfEdge>& halfEdges, std::vector<Vector3>& vertices, float threshold) {
    std::vector<Edge> edges;

    for (const HalfEdge& he : halfEdges) {
        Edge edge;
        edge.v0 = vertices[he.vertexIndex];
        edge.v1 = vertices[he.nextIndex];
        calculateEdgeCost(edge); // Calculate the cost for the edge
        edges.push_back(edge);

        std::cout << "Edge cost: " << edge.cost << std::endl;
    }

}




std::vector<unsigned short> computeIndices(const std::vector<Vector3>& vertices) {
    std::vector<unsigned short> indices;
    
    for (size_t i = 0; i < vertices.size(); i += 3) {
        indices.push_back(static_cast<unsigned short>(i));
        indices.push_back(static_cast<unsigned short>(i + 1));
        indices.push_back(static_cast<unsigned short>(i + 2));
    }
    return indices;
}

Mesh generateLODMesh(const std::vector<Vector3>& vertices, const std::vector<unsigned short>& indices, Mesh sourceMesh) {
    Mesh lodMesh = { 0 };

    if (vertices.empty() || indices.empty()) {
        TraceLog(LOG_WARNING, "generateLODMesh: Input arrays are empty.");
        return sourceMesh;
    }


    int vertexCount = static_cast<int>(vertices.size());
    int triangleCount = static_cast<int>(indices.size()) / 3;

    lodMesh.vertexCount = vertexCount;
    lodMesh.triangleCount = triangleCount;
    lodMesh.vertices = (float*)malloc(sizeof(float) * 3 * vertexCount);
    lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indices.size());
    lodMesh.normals = (float*)malloc(sizeof(float) * 3 * vertexCount);


    if (!lodMesh.vertices || !lodMesh.indices || !lodMesh.normals) {
        TraceLog(LOG_ERROR, "generateLODMesh: Memory allocation failed.");
        
        
        if (lodMesh.vertices) free(lodMesh.vertices);
        if (lodMesh.indices) free(lodMesh.indices);
        if (lodMesh.normals) free(lodMesh.normals);

        return sourceMesh;
    }


    
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

    
    for (int i = 0; i < vertexCount; i++) {
        lodMesh.vertices[i * 3] = vertices[i].x;
        lodMesh.vertices[i * 3 + 1] = vertices[i].y;
        lodMesh.vertices[i * 3 + 2] = vertices[i].z;
    }

    for (size_t i = 0; i < indices.size(); i++) {
        lodMesh.indices[i] = indices[i];
    }

    UploadMesh(&lodMesh, false);

    if (lodMesh.vertexCount == 0 || lodMesh.triangleCount == 0 || !lodMesh.vertices || !lodMesh.indices) {
        TraceLog(LOG_ERROR, "generateLODMesh: Mesh creation failed.");
        
        if (lodMesh.vertices) {
            free(lodMesh.vertices);
            lodMesh.vertices = NULL;
        }

        if (lodMesh.normals) {
            free(lodMesh.normals);
            lodMesh.normals = NULL;
        }
    }
    
    return lodMesh;
}


int main() {
    
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Half-Edge Collapsing");

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 1.0f, -5.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    
    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");
    Mesh mesh = GenMeshSphere(1, 15, 15);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].shader = shader;

    
    std::vector<Vector3> vertices;
    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.vertices[i * 3];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];
        vertices.push_back({ x, y, z });
    }

    std::vector<HalfEdge> halfEdges = initializeHalfEdges(vertices);

    
    float threshold = 0.0;
    SetTargetFPS(50);
    rlImGuiSetup(true);

    
    while (!WindowShouldClose()) {
        
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);

        
        vertices.clear();
        for (int i = 0; i < mesh.vertexCount; i++) {
            float x = mesh.vertices[i * 3];
            float y = mesh.vertices[i * 3 + 1];
            float z = mesh.vertices[i * 3 + 2];
            vertices.push_back({ x, y, z });
        }
        halfEdgeCollapse(halfEdges, vertices, threshold);

        
        std::vector<unsigned short> newIndices = computeIndices(vertices);

        
        BeginDrawing();
        ClearBackground(GRAY);

        
        BeginMode3D(camera);
        if (IsModelReady(model))
            DrawModel(model, Vector3Zero(), 1.0f, RED);
        EndMode3D();

        
        DrawText("Half-Edge Collapsing", 10, 10, 20, BLACK);
        DrawText(TextFormat("Collapsed Vertices: %d", mesh.vertexCount - static_cast<int>(vertices.size())), 10, 40, 20, BLACK);
        for (size_t i = 0; i < vertices.size(); ++i) {
            DrawText(TextFormat("Vertex %d: [%.2f, %.2f, %.2f]", i, vertices[i].x, vertices[i].y, vertices[i].z), 10, 70 + 30 * i, 20, BLACK);
        }

        
        rlImGuiBegin();
        if (ImGui::Begin("Inspector Window", NULL))
        {
            if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, 1)) {

                vertices.clear();
                for (int i = 0; i < mesh.vertexCount; i++) {
                    float x = mesh.vertices[i * 3];
                    float y = mesh.vertices[i * 3 + 1];
                    float z = mesh.vertices[i * 3 + 2];
                    vertices.push_back({ x, y, z });
                }

                halfEdgeCollapse(halfEdges, vertices, threshold);
                model = LoadModelFromMesh(generateLODMesh(vertices, newIndices, mesh));

                if (threshold == 0) {
                    vertices.clear();
                    for (int i = 0; i < mesh.vertexCount; i++) {
                        float x = mesh.vertices[i * 3];
                        float y = mesh.vertices[i * 3 + 1];
                        float z = mesh.vertices[i * 3 + 2];
                        vertices.push_back({ x, y, z });
                    }
                    newIndices = computeIndices(vertices);
                    model = LoadModelFromMesh(generateLODMesh(vertices, newIndices, mesh));
                }

                model.materials[0].shader = shader;
            }
            ImGui::End();
        }
        
        rlImGuiEnd();

        
        EndDrawing();
    }

    
    CloseWindow();

    return 0;
}