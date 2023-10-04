#include "include/raylib.h"
#include "include/raymath.h"
#include <iostream>
#include <vector>
#include <cfloat>
#include <cstring>

Vector3 CalculateMidpoint(const Vector3& vertex1, const Vector3& vertex2) {
    float midX = (vertex1.x + vertex2.x) / 2.0f;
    float midY = (vertex1.y + vertex2.y) / 2.0f;
    float midZ = (vertex1.z + vertex2.z) / 2.0f;

    return Vector3{ midX, midY, midZ };
}

std::vector<Vector3> ContractVertices(const Mesh& mesh, float maxDistance) {
    if (mesh.vertices == nullptr || mesh.vertexCount == 0) {
        std::cout << "Mesh has no vertices or is not initialized." << std::endl;
        return std::vector<Vector3>();
    }

    std::cout << "Unique vertices within a distance of " << maxDistance << " from each other:" << std::endl;

    std::vector<Vector3> uniqueVertices;

    for (int i = 0; i < mesh.vertexCount; i++) {
        float xi = mesh.vertices[i * 3];
        float yi = mesh.vertices[i * 3 + 1];
        float zi = mesh.vertices[i * 3 + 2];

        Vector3 vertex_position = { xi, yi, zi };

        bool isUnique = true;

        // Check if this vertex is unique or very close to an existing one
        for (const Vector3& uniqueVertex : uniqueVertices) {
            float distance = Vector3Distance(Vector3{ xi, yi, zi }, uniqueVertex);
            if (distance <= maxDistance) {
                vertex_position = CalculateMidpoint(vertex_position, uniqueVertex);
                isUnique = false;
                break;
            }
        }

        if (isUnique) {
            uniqueVertices.push_back(vertex_position);
        }
    }

    int index = 0;
    for (const Vector3& vertex : uniqueVertices) {
        // std::cout << "Vertex " << index << ": " << vertex.x << ", " << vertex.y << ", " << vertex.z << std::endl;
        index++;
    }

    return uniqueVertices;
}

Mesh GenerateLODMesh(const std::vector<Vector3>& uniqueVertices, Mesh& sourceMesh) {
    // Create a new mesh based on unique vertices
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

        lodMesh.indices = sourceMesh.indices;
        
        
        

        // Copy unique vertices to the new mesh's vertex array
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = uniqueVertices[i].x;
            lodMesh.vertices[i * 3 + 1] = uniqueVertices[i].y;
            lodMesh.vertices[i * 3 + 2] = uniqueVertices[i].z;
        }

        // // Generate triangle indices
        // for (int i = 0; i < indexCount; i++) {
        //     lodMesh.indices[i] = i;
        // }

        // Generate VBO for vertices
       lodMesh.vboId = (unsigned int *)RL_CALLOC(1, sizeof(unsigned int));
//        lodMesh.vboId = sourceMesh.vboId;      
        lodMesh.vaoId = sourceMesh.vaoId;      
    }

    UploadMesh(&lodMesh, false);

    return lodMesh;
}

int main() {
    SetTraceLogLevel(LOG_WARNING);

    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "LOD Example");

    std::cout << "Starting Program..." << std::endl;

    Shader shader = LoadShader("Engine/Lighting/shaders/lod.vs", "Engine/Lighting/shaders/lod.fs");

    // Starting LOD level
    Mesh sourceMesh = GenMeshSphere(1, 16, 16);

    float lodFactor = 0;
    // Get unique vertices and generate LOD mesh
    std::vector<Vector3> uniqueVertices = ContractVertices(sourceMesh, 0.3f);
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
        lodModel.meshes[0] = GenerateLODMesh(uniqueVertices, sourceMesh);

        if (IsKeyPressed(KEY_O))
        {
            lodFactor += 0.1f;
        }
        else if (IsKeyPressed(KEY_P))
        {
            lodFactor -= 0.1f;
        }


        BeginDrawing();
        ClearBackground(DARKBLUE);
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
