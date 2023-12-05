#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>
#include "raylib.h"
#include "raymath.h"

#include "meshoptimizer/src/meshoptimizer.h" // Include meshoptimizer header

#include "include/rlImGui.h"
#include "imgui.h"


const int screenWidth = 1200;
const int screenHeight = 450;





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
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Half-Edge Collapsing");

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 1.0f, -5.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    
    Shader shader = LoadShader(0, "Engine/Lighting/shaders/lod.fs");
    
    Mesh mesh = GenMeshSphere(1,17,17);
    Model model = LoadModelFromMesh(mesh);
    
    
    model.materials[0].shader = shader;
    
    float threshold = 0.0;
    SetTargetFPS(50);
    rlImGuiSetup(true);



    while (!WindowShouldClose()) {
        
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_FREE);

        
        BeginDrawing();
        ClearBackground(GRAY);

        
        BeginMode3D(camera);
        if (IsModelReady(model))
        {
            if (IsKeyDown(KEY_X))
                DrawModel(model, Vector3Zero(), 1.0f, RED);
            else
                DrawModelWires(model, Vector3Zero(), 1.0f, RED);
        }
        EndMode3D();

        
        DrawText("Half-Edge Collapsing", 10, 10, 20, BLACK);

        
        rlImGuiBegin();
        if (ImGui::Begin("Inspector Window", NULL))
        {
            if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, 1)) {
                std::vector<unsigned int> lod(mesh.triangleCount * 3);

                Mesh lodMesh = mesh;
                if (threshold > 0.0) {

                    size_t lodVertexCount = meshopt_simplify(
                        &lod[0],            // Destination indices (unsigned int*)
                        reinterpret_cast<const unsigned int*>(lodMesh.indices), // Input indices (unsigned int*)
                        lodMesh.triangleCount * 3,
                        lodMesh.vertices,       // Vertex positions
                        lodMesh.vertexCount,
                        sizeof(Vector3),        // Size of each vertex position
                        threshold);


                }
            }
            ImGui::End();
        }
        
        rlImGuiEnd();

        
        EndDrawing();
    }

    
    CloseWindow();

    return 0;
}