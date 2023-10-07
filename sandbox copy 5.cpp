#include "nanoflann/include/nanoflann.hpp"
#undef PI

#include "include/raylib.h"
#include "include/raymath.h"
#include "include/kd_tree_utils.h"
#include <iostream>
#include <vector>
#include <cfloat>
#include <limits>
#include <cmath>
#include <unordered_map>
#include <omp.h>

// Function to calculate the midpoint between two vertices
Vector3 CalculateMidpoint(const Vector3& vertex1, const Vector3& vertex2) {
    return Vector3{ (vertex1.x + vertex2.x) / 2.0f, (vertex1.y + vertex2.y) / 2.0f, (vertex1.z + vertex2.z) / 2.0f };
}

// Function to contract vertices based on a maximum distance
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

    // Create a spatial hash for efficient lookup
    std::unordered_map<int, int> spatialHash;

    // Store the squared maximum distance to avoid redundant calculations
    const float maxDistanceSquared = maxDistance * maxDistance;

    #pragma omp parallel for
    for (int i = 0; i < mesh.vertexCount; i++) {
        float xi = mesh.vertices[i * 3];
        float yi = mesh.vertices[i * 3 + 1];
        float zi = mesh.vertices[i * 3 + 2];

        Vector3 vertex_position = { xi, yi, zi };

        // Find the closest existing vertex within maxDistance
        int closestVertexIndex = -1;
        float closestDistance = maxDistanceSquared;

        // Determine the spatial hash cell for the current vertex
        int cellX = static_cast<int>(xi / maxDistance);
        int cellY = static_cast<int>(yi / maxDistance);
        int cellZ = static_cast<int>(zi / maxDistance);

        int hashKey = cellX * 1000000 + cellY * 1000 + cellZ;
        
        // Iterate over neighboring cells within a radius of 1
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dz = -1; dz <= 1; dz++) {
                    int neighborCellX = cellX + dx;
                    int neighborCellY = cellY + dy;
                    int neighborCellZ = cellZ + dz;

                    // Calculate the hash key for the neighboring cell
                    int hashKey = neighborCellX * 1000000 + neighborCellY * 1000 + neighborCellZ;

                    // Check if the neighboring cell exists in the spatial hash
                    auto neighborCellIter = spatialHash.find(hashKey);

                    if (neighborCellIter != spatialHash.end()) {
                        int neighborVertexIndex = neighborCellIter->second;

                        // Calculate the distance squared to the neighboring vertex
                        float distanceSquared = Vector3DistanceSquared(vertex_position, contractedVertices[neighborVertexIndex]);

                        if (distanceSquared <= closestDistance) {
                            closestVertexIndex = neighborVertexIndex;
                            closestDistance = distanceSquared;

                            // If a vertex is found within half the maxDistance, break early
                            if (distanceSquared < maxDistanceSquared * 0.25f) {
                                break;
                            }
                        }
                    }
                }
            }
        }

        // If a close vertex is found, use it; otherwise, use the original vertex
        if (closestVertexIndex != -1) {
            contractedVertices[i] = contractedVertices[closestVertexIndex];
        } else {
            contractedVertices[i] = vertex_position;
        }

        // Update the spatial hash for the current vertex
        spatialHash[hashKey] = i;
    }

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

        // Generate new indices based on the new vertex positions
        for (int i = 0; i < triangleCount; i++) {
            lodMesh.indices[i * 3] = i * 3;
            lodMesh.indices[i * 3 + 1] = i * 3 + 1;
            lodMesh.indices[i * 3 + 2] = i * 3 + 2;
        }

        // Copy unique vertices to the new mesh's vertex array
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = uniqueVertices[i].x;
            lodMesh.vertices[i * 3 + 1] = uniqueVertices[i].y;
            lodMesh.vertices[i * 3 + 2] = uniqueVertices[i].z;
        }
    }

    UploadMesh(&lodMesh, false); // Upload mesh data to GPU (VBO/IBO)

    return lodMesh;
}

int main() {
    SetTraceLogLevel(LOG_WARNING);

    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "LOD Example");

    std::cout << "Starting Program..." << std::endl;

    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");

    // Starting LOD level
    Mesh sourceMesh = LoadModel("a.obj").meshes[0];

    std::cout << "loaded" << std::endl;

    float lodFactor = 0;
    // Get unique vertices and generate LOD mesh
    Model lodModel = LoadModelFromMesh(GenMeshCube(1,1,1));
    lodModel.materials[0].shader = shader;

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 0.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);

    // Use the generated LOD mesh as needed
    while (!WindowShouldClose()) {

        // Get unique vertices and generate LOD mesh
        std::vector<Vector3> uniqueVertices = ContractVertices(sourceMesh, lodFactor);

        // Debug output to check if vertices are being contracted

        lodModel.meshes[0] = GenerateLODMesh(uniqueVertices, sourceMesh);

        if (IsKeyPressed(KEY_O))
        {
            lodFactor += 0.1f;
        }
        else if (IsKeyPressed(KEY_P))
        {
            lodFactor -= 0.1f;
            std::cout << "DECREASING\n";
        }

        BeginDrawing();
        ClearBackground(WHITE);
        UpdateCamera(&camera, CAMERA_FREE);
        SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), &camera.position, SHADER_UNIFORM_VEC3);

        BeginMode3D(camera);
        BeginShaderMode(shader);

        DrawModel(lodModel, Vector3Zero(), 1.0f, WHITE);
        
        EndShaderMode();
        EndMode3D();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}