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


void calculateEdgeCost(Edge& edge) {
    edge.midpoint = Vector3Lerp(edge.v0, edge.v1, 0.5f);
    edge.cost = Vector3Distance(edge.v0, edge.midpoint);
}

void collapseEdge(Edge& edge) {
    edge.midpoint = Vector3Lerp(edge.v0, edge.v1, 0.5f);
}

void halfEdgeCollapse(std::vector<Vector3>& vertices, std::vector<unsigned short>& indices, float threshold) {
    std::vector<Edge> edges;

    for (int i = 0; i < vertices.size(); i += 2) {
        Edge edge;
        edge.v0 = vertices[i];
        edge.v1 = vertices[i + 1];
        calculateEdgeCost(edge);
        edges.push_back(edge);
    }

    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        return a.cost < b.cost;
    });

    for (Edge& edge : edges) {
        if (edge.cost >= threshold) break;
        collapseEdge(edge);
    }

    std::vector<Vector3> collapsedVertices;

    for (const Edge& edge : edges) {
        if (edge.cost < threshold)
            collapsedVertices.push_back(edge.midpoint);
        else {
            collapsedVertices.push_back(edge.v0);
            collapsedVertices.push_back(edge.v1);
        }
    }

    indices.clear();

    for (int i = 0; i < static_cast<int>(collapsedVertices.size()); i += 3) {
        indices.push_back(static_cast<unsigned short>(i));
        indices.push_back(static_cast<unsigned short>(i + 2));
        indices.push_back(static_cast<unsigned short>(i + 1));
    }

    std::cout << "Lowest cost: " << edges[0].cost << std::endl;
    std::cout << "Highest cost: " << edges[edges.size() - 1].cost << std::endl;
    std::cout << "Threshold: " << threshold << std::endl;
    std::cout << "Number of edges: " << edges.size() << std::endl;
    std::cout << "Number of vertices: " << collapsedVertices.size() << std::endl;
    std::cout << "Collapsed " << vertices.size() - collapsedVertices.size() << " vertices!" << std::endl;


    vertices = collapsedVertices;


    std::cout << std::endl;
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


void calculateNormals(const std::vector<Vector3>& vertices, const std::vector<unsigned short>& indices, float* normals) {
    // Initialize normals to zero
    for (size_t i = 0; i < vertices.size(); ++i) {
        normals[i * 3] = 0.0f;
        normals[i * 3 + 1] = 0.0f;
        normals[i * 3 + 2] = 0.0f;
    }

    // Calculate normals for each triangle and accumulate
    for (size_t i = 0; i < indices.size(); i += 3) {
        Vector3 v0 = vertices[indices[i]];
        Vector3 v1 = vertices[indices[i + 1]];
        Vector3 v2 = vertices[indices[i + 2]];

        Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(v1, v0), Vector3Subtract(v2, v0)));

        // Accumulate normals for each vertex of the triangle
        for (int j = 0; j < 3; ++j) {
            normals[indices[i + j] * 3] += normal.x;
            normals[indices[i + j] * 3 + 1] += normal.y;
            normals[indices[i + j] * 3 + 2] += normal.z;
        }
    }

    // Normalize the accumulated normals
    for (size_t i = 0; i < vertices.size(); ++i) {
        float length = sqrt(normals[i * 3] * normals[i * 3] + normals[i * 3 + 1] * normals[i * 3 + 1] + normals[i * 3 + 2] * normals[i * 3 + 2]);
        normals[i * 3] /= length;
        normals[i * 3 + 1] /= length;
        normals[i * 3 + 2] /= length;
    }
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
    lodMesh.normals = sourceMesh.normals;


    if (!lodMesh.vertices || !lodMesh.indices || !lodMesh.normals) {
        TraceLog(LOG_ERROR, "generateLODMesh: Memory allocation failed.");
        
        
        if (lodMesh.vertices) free(lodMesh.vertices);
        if (lodMesh.indices) free(lodMesh.indices);
        if (lodMesh.normals) free(lodMesh.normals);

        return sourceMesh;
    }


    
    calculateNormals(vertices, indices, lodMesh.normals);
    
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


    
    float threshold = 0.0;
    SetTargetFPS(50);
    rlImGuiSetup(true);

    std::vector<unsigned short> newIndices;

    if (mesh.indices)
    {
        for (int i = 0; i < sizeof(mesh.indices)/sizeof(mesh.indices[0]); i++) {
            newIndices.push_back(mesh.indices[i]);
        }
    }

    while (!WindowShouldClose()) {
        
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);

        
        BeginDrawing();
        ClearBackground(GRAY);

        
        BeginMode3D(camera);
        if (IsModelReady(model))
            DrawModelWires(model, Vector3Zero(), 1.0f, RED);
        EndMode3D();

        
        DrawText("Half-Edge Collapsing", 10, 10, 20, BLACK);
        DrawText(TextFormat("Collapsed Vertices: %d", static_cast<int>(vertices.size())), 10, 40, 20, BLACK);
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

                halfEdgeCollapse(vertices, newIndices, threshold);
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