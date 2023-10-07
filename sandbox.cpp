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
#include <unordered_map>

using namespace std;

typedef struct Entities
{
    Vector3 position;
    Model model;
    Model lod_model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
};

typedef struct Cluster
{
    Color color;
    int lodLevel;
    std::vector<Entities> entities;
};


inline float Vector3DistanceSquared(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

std::vector<Vector3> ContractVertices(const Mesh& mesh, float maxDistance) {
    if (mesh.vertices == nullptr || mesh.vertexCount == 0) {
        std::cout << "Mesh has no vertices or is not initialized." << std::endl;
        return std::vector<Vector3>();
    }

    std::vector<Vector3> contractedVertices(mesh.vertexCount);

    const float maxDistanceSquared = maxDistance * maxDistance;

    // Sort vertices by x-coordinate for better memory access patterns
    std::vector<int> sortedIndices(mesh.vertexCount);
    #pragma omp parallel for
    for (int i = 0; i < mesh.vertexCount; i++) {
        sortedIndices[i] = i;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        return mesh.vertices[a * 3] < mesh.vertices[b * 3];
    });

    #pragma omp parallel for
    for (int i = 0; i < mesh.vertexCount; i++) {
        int idx = sortedIndices[i];
        float xi = mesh.vertices[idx * 3];
        float yi = mesh.vertices[idx * 3 + 1];
        float zi = mesh.vertices[idx * 3 + 2];
        Vector3 vertex_position = { xi, yi, zi };

        int closestVertexIndex = -1;
        float closestDistance = maxDistanceSquared;

        // Only search within a limited neighborhood
        int searchStart = std::max(0, i - 128); // Adjust the neighborhood size as needed

        for (int j = searchStart; j < i; j++) {
            int jdx = sortedIndices[j];
            float distSq = Vector3DistanceSquared(vertex_position, contractedVertices[jdx]);
            if (distSq <= closestDistance) {
                closestVertexIndex = jdx;
                closestDistance = distSq;
                if (distSq < maxDistanceSquared * 0.25f) {
                    break;
                }
            }
        }

        contractedVertices[idx] = (closestVertexIndex != -1) ? contractedVertices[closestVertexIndex] : vertex_position;
    }

    sortedIndices.clear();

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
//        lodMesh.indices = sourceMesh.indices;

        // Copy unique vertices to the new mesh's vertex array
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = uniqueVertices[i].x;
            lodMesh.vertices[i * 3 + 1] = uniqueVertices[i].y;
            lodMesh.vertices[i * 3 + 2] = uniqueVertices[i].z;
        }

        // Generate new indices for non-indexed mesh
        if (sourceMesh.indices)
        {
            for (int i = 0; i < triangleCount; i++) {
                lodMesh.indices[i * 3] = sourceMesh.indices[i * 3];
                lodMesh.indices[i * 3 + 1] = sourceMesh.indices[i * 3 + 1];
                lodMesh.indices[i * 3 + 2] = sourceMesh.indices[i * 3 + 2];
            }
        }
        else
            lodMesh.indices = sourceMesh.indices;
    }
    UploadMesh(&lodMesh, false);
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
            entity.model = LoadModelFromMesh(GenMeshSphere(1.0f, 16.0f, 16.0f));
            entity.lod_model = entity.model;
            // entity.model.materials[0].shader = shader;
            entity.position = { static_cast<float>(GetRandomValue(-10, 10)), static_cast<float>(GetRandomValue(-10, 10)), static_cast<float>(GetRandomValue(-10, 10)) };
            cluster.entities.push_back(entity);
        }

        clusters.push_back(cluster);
    }
    
    float lodLevel = 0.0f;
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
        for (size_t i = 0; i < clusters.size(); i++) {
            int clusterIndex = static_cast<int>((clusters[i].entities[0].position.x + clusters[i].entities[0].position.y + clusters[i].entities[0].position.z) / 3) + 5;
            
            // Ensure clusterIndex is within bounds
            clusterIndex = Clamp(clusterIndex, 0, 9);
            
            // Determine the LOD level based on the cluster's distance from the camera (you can adjust the threshold as needed)
            float distance = Vector3Distance(clusters[i].entities[0].position, camera.position);

            if (distance < 20.0f) {
                clusters[i].lodLevel = 0; // Highest LOD
                clusters[i].color = GREEN;
                for (Entities& entity : clusters[i].entities) {
                    entity.lod_model = entity.model;
                    // Also, update the mesh data for the lod_model
                    entity.lod_model = LoadModelFromMesh(entity.model.meshes[0]);
                    
                }

            } else if (distance < 50.0f) {
                clusters[i].lodLevel = 1; // Medium LOD

                for (size_t j = 0; j < clusters[i].entities.size(); j++) {
                    clusters[i].entities[j].lod_model = LoadModelFromMesh(GenerateLODMesh(ContractVertices(clusters[i].entities[j].model.meshes[0], 0.5f), clusters[i].entities[j].model.meshes[0]));
                    clusters[i].color = YELLOW;
                }
            } else {
                clusters[i].lodLevel = 2; // Lowest LOD
                clusters[i].color = RED;
                for (size_t j = 0; j < clusters[i].entities.size(); j++) {
                    clusters[i].entities[j].lod_model = LoadModelFromMesh(GenerateLODMesh(ContractVertices(clusters[i].entities[j].model.meshes[0], 1.0f), clusters[i].entities[j].model.meshes[0]));
                }
            }

            // Draw all entities in the cluster with the cluster's color and LOD level
            for (size_t j = 0; j < clusters[i].entities.size(); j++) {
                DrawModel(clusters[i].entities[j].lod_model, clusters[i].entities[j].position, 1.0f, clusters[i].color);
            }

        }

        EndShaderMode();

        EndMode3D();

        DrawFPS(10,10);
        
        EndDrawing();
    }
    
    // Unload models
    for (size_t i = 0; i < clusters.size(); i++) {
        for (size_t j = 0; j < clusters[i].entities.size(); j++) {
            UnloadModel(clusters[i].entities[j].model);
            UnloadModel(clusters[i].entities[j].lod_model);
        }
    }
    
    // Clean up and close the window
    CloseWindow();
    
    return 0;
}
