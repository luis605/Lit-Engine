#include "include/raylib.h"
#include "include/raymath.h"
#include <iostream>
#include <vector>
#include <cfloat>
#include <limits>
#include <cmath>
#include <unordered_map>
#include <omp.h>
#include <algorithm>

using namespace std;

const float LOD_DISTANCE_HIGH = 10.0f;
const float LOD_DISTANCE_MEDIUM = 25.0f;
const float LOD_DISTANCE_LOW = 35.0f;

typedef struct Entities
{
    Vector3 position;
    Model model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    Model LodModels[4] = { model, LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f)), LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f)) };
};

typedef struct Cluster
{
    Color color;
    int lodLevel;
    std::vector<Entities> entities;
};



// Function to calculate the midpoint between two vertices
Vector3 CalculateMidpoint(const Vector3& vertex1, const Vector3& vertex2) {
    return Vector3{ (vertex1.x + vertex2.x) / 2.0f, (vertex1.y + vertex2.y) / 2.0f, (vertex1.z + vertex2.z) / 2.0f };
}

inline float Vector3DistanceSquared(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// Define the spatial hash cell size (adjust as needed)
std::vector<Vector3> ContractVertices(const Mesh& mesh, float maxDistance) {
    if (mesh.vertices == nullptr || mesh.vertexCount == 0) {
        std::cout << "Mesh has no vertices or is not initialized." << std::endl;
        return std::vector<Vector3>();
    }

    const float maxDistanceSquared = maxDistance * maxDistance;
    const int vertexCount = mesh.vertexCount;

    std::vector<Vector3> contractedVertices(vertexCount, Vector3{0.0f, 0.0f, 0.0f});

    // Sort vertices by x-coordinate for better memory access patterns
    std::vector<int> sortedIndices(vertexCount);
    #pragma omp parallel for
    for (int i = 0; i < vertexCount; i++) {
        sortedIndices[i] = i;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        return mesh.vertices[a * 3] < mesh.vertices[b * 3];
    });

    #pragma omp parallel for
    for (int i = 0; i < vertexCount; i++) {
        int idx = sortedIndices[i];
        float xi = mesh.vertices[idx * 3];
        float yi = mesh.vertices[idx * 3 + 1];
        float zi = mesh.vertices[idx * 3 + 2];
        Vector3 vertex_position = { xi, yi, zi };

        int closestVertexIndex = -1;
        float closestDistance = maxDistanceSquared;

        // Calculate a smaller neighborhood range based on current vertex position
        int searchStart = i - 4; // Further reduce the neighborhood size

        // Ensure that searchStart doesn't go out of bounds
        searchStart = std::max(searchStart, 0);

        for (int j = searchStart; j < i; j++) {
            int jdx = sortedIndices[j];
            float distSq = Vector3DistanceSquared(vertex_position, contractedVertices[jdx]);
            if (distSq <= closestDistance) {
                closestVertexIndex = jdx;
                closestDistance = distSq;

                // Early exit if a very close vertex is found
                if (distSq < maxDistanceSquared * 0.25f) {
                    break;
                }
            }
        }

        contractedVertices[idx] = (closestVertexIndex != -1) ? contractedVertices[closestVertexIndex] : vertex_position;
    }

    sortedIndices.clear();
    std::cout << "Vertices Count -> " << contractedVertices.size() << std::endl;

    return contractedVertices;
}



// Function to generate a simplified LOD mesh
Mesh GenerateLODMesh(const std::vector<Vector3>& uniqueVertices, Mesh& sourceMesh) {
    Mesh lodMesh = { 0 };

    if (!uniqueVertices.empty()) {
        int vertexCount = uniqueVertices.size();
        int triangleCount = vertexCount / 3;
        int indexCount = triangleCount * 3;

        // Allocate memory for the new mesh
        lodMesh.vertexCount = vertexCount;
        lodMesh.triangleCount = triangleCount;
        lodMesh.vertices = (float*)malloc(sizeof(float) * 3 * vertexCount);
        lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indexCount);

        // Copy unique vertices to the new mesh's vertex array
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = uniqueVertices[i].x;
            lodMesh.vertices[i * 3 + 1] = uniqueVertices[i].y;
            lodMesh.vertices[i * 3 + 2] = uniqueVertices[i].z;
        }

        // Generate new indices for non-indexed mesh
        if (sourceMesh.indices) {
            for (int i = 0; i < triangleCount; i++) {
                lodMesh.indices[i * 3] = sourceMesh.indices[i * 3];
                lodMesh.indices[i * 3 + 1] = sourceMesh.indices[i * 3 + 1];
                lodMesh.indices[i * 3 + 2] = sourceMesh.indices[i * 3 + 2];
            }
        }
        else {
            lodMesh.indices = sourceMesh.indices;
        }
    }

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

    return lodMesh;
}





int main() {
    SetTraceLogLevel(LOG_WARNING);
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "HLOD");

    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");

    Camera3D camera;
    camera.position = { 0.0f, 0.0f, 10.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Create a vector of clusters
    std::vector<Cluster> clusters;
    
    // Define colors for clusters
    Color clusterColors[] = {
        RED, GREEN, BLUE, YELLOW, ORANGE,
        PINK, PURPLE, DARKGRAY, LIME, SKYBLUE
    };
    
    // Create clusters and populate them with entities (for example, cubes)
    for (int i = 0; i < 10; i++) {
        Cluster cluster;
        cluster.color = clusterColors[i];
        cluster.lodLevel = 0; // Start with the highest LOD level

        for (int j = 0; j < 2; j++) {
            Entities entity;
            entity.model = LoadModelFromMesh(GenMeshSphere(1.0f, 99.0f, 99.0f));
            entity.LodModels[0] = entity.model;
            entity.LodModels[1] = LoadModelFromMesh(GenerateLODMesh(ContractVertices(entity.model.meshes[0], 0.5f), entity.model.meshes[0]));
            entity.LodModels[2] = LoadModelFromMesh(GenerateLODMesh(ContractVertices(entity.model.meshes[0], 1.0f), entity.model.meshes[0]));
            entity.LodModels[3] = LoadModelFromMesh(GenerateLODMesh(ContractVertices(entity.model.meshes[0], 1.5f), entity.model.meshes[0]));
            

            entity.position = { static_cast<float>(GetRandomValue(-10, 10)), static_cast<float>(GetRandomValue(-10, 10)), static_cast<float>(GetRandomValue(-10, 10)) };
            cluster.entities.push_back(entity);
        }

        clusters.push_back(cluster);
    }
    
    
    // Main game loop
    while (!WindowShouldClose()) {
        // Update
        
        // Clear the background
        BeginDrawing();
        ClearBackground(GRAY);
        BeginMode3D(camera);
        BeginShaderMode(shader);
        UpdateCamera(&camera, CAMERA_FREE);


        // Iterate through clusters and group them into LOD levels based on positions
        for (Cluster& cluster : clusters) {
            float distance = Vector3Distance(cluster.entities[0].position, camera.position);
            int lodLevel = 0;

            if (distance < LOD_DISTANCE_HIGH) {
                lodLevel = 0;
                cluster.color = GREEN;
            } else if (distance < LOD_DISTANCE_MEDIUM) {
                lodLevel = 1;
                cluster.color = YELLOW;
            } else if (distance < LOD_DISTANCE_LOW) {
                lodLevel = 2;
                cluster.color = RED;
            } else {
                lodLevel = 3;
                cluster.color = WHITE;
            }

            // Update LOD level for all entities in the cluster
            for (Entities& entity : cluster.entities) {
                entity.LodModels[lodLevel] = LoadModelFromMesh(entity.LodModels[lodLevel].meshes[0]);
            }

            // Draw all entities in the cluster with the cluster's color and LOD level
            for (Entities& entity : cluster.entities) {
                DrawModel(entity.LodModels[lodLevel], entity.position, 1.0f, cluster.color);
            }
        }


        EndShaderMode();

        EndMode3D();

        DrawFPS(10,10);
        
        EndDrawing();
    }
    
    // Unload models
    for (Cluster& cluster : clusters) {
        for (Entities& entity : cluster.entities) {
            UnloadModel(entity.model);
            for (int i = 0; i < 4; i++) {
                UnloadModel(entity.LodModels[i]);
            }
        }
    }
    
    // Clean up and close the window
    CloseWindow();
    
    return 0;
}

