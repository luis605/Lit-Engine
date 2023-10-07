#include "nanoflann/include/nanoflann.hpp" // Include nanoflann for KD-tree
#undef PI
#include "include/nanoflann_utils.h"

#include "include/raylib.h"
#include "include/raymath.h"
#include <iostream>
#include <vector>
#include <cfloat>
#include <limits>
#include <cmath>
#include <unordered_map>
#include <omp.h>
#include <queue>
#include <algorithm>
#include <execution>
#include <unordered_map>


struct MeshAdaptor {
    const Mesh& mesh;
    MeshAdaptor(const Mesh& mesh) : mesh(mesh) {}
    
    inline size_t kdtree_get_point_count() const {
        return mesh.vertexCount;
    }

    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
        const float* vertex = &(mesh.vertices[idx * 3]);
        return vertex[dim];
    }

    template <class BBOX>
    bool kdtree_get_bbox(BBOX&) const {
        return false;
    }
};


typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, MeshAdaptor>,
    MeshAdaptor,
    3> KDTree;


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

    const int screenWidth = 800;
    const int screenHeight = 600;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "LOD Example");

    std::cout << "Starting Program..." << std::endl;

    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");

    // Starting LOD level
    Mesh sourceMesh = GenMeshSphere(1, 50, 50);//LoadModel("a.obj").meshes[0];

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

        if (IsKeyPressed(KEY_O))
        {
            lodFactor += 0.1f;
        }
        else if (IsKeyPressed(KEY_P))
        {
            lodFactor -= 0.1f;
            std::cout << "DECREASING\n";
        }

        std::vector<Vector3> uniqueVertices = ContractVertices(sourceMesh, lodFactor);
        lodModel.meshes[0] = GenerateLODMesh(uniqueVertices, sourceMesh);



        BeginDrawing();
        ClearBackground(WHITE);
        UpdateCamera(&camera, CAMERA_FREE);
        SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), &camera.position, SHADER_UNIFORM_VEC3);

        BeginMode3D(camera);
        BeginShaderMode(shader);

        DrawModel(lodModel, Vector3Zero(), 1.0f, WHITE);

        
        EndShaderMode();
        EndMode3D();

        DrawFPS(10,10);
        EndDrawing();
    }

    UnloadModel(lodModel);
    UnloadMesh(sourceMesh);
    UnloadShader(shader);
    CloseWindow();
    return 0;
}