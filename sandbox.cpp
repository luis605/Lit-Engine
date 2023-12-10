#include <iostream>
#include <vector>
#include "raylib.h"
#include "raymath.h"

#include "meshoptimizer/src/meshoptimizer.h" 

#include "include/rlImGui.h"
#include "imgui.h"


const int screenWidth = 1200;
const int screenHeight = 450;


struct OptimizedMeshData {
    std::vector<uint>& Indices;
    std::vector<Vector3>& Vertices;
    int vertexCount;

    
    OptimizedMeshData(std::vector<uint>& indices, std::vector<Vector3>& vertices)
        : Indices(indices), Vertices(vertices) {}
};

OptimizedMeshData OptimizeMesh(Mesh& mesh, std::vector<uint>& Indices, std::vector<Vector3>& Vertices, float threshold)
{
    OptimizedMeshData data(Indices, Vertices);
    size_t NumIndices = Indices.size();
    size_t NumVertices = Vertices.size();

    
    std::vector<unsigned int> remap(NumIndices);
    size_t OptVertexCount = meshopt_generateVertexRemap(remap.data(),    
                                                        Indices.data(),  
                                                        NumIndices,      
                                                        Vertices.data(), 
                                                        NumVertices,     
                                                        sizeof(Vector3)); 

    std::cout << "OptVertexCount: " << OptVertexCount << std::endl;
    std::cout << "NumVertices: " << NumVertices << std::endl;

    data.vertexCount = OptVertexCount;

    std::vector<uint> OptIndices;
    std::vector<Vector3> OptVertices;
    OptIndices.resize(NumIndices);
    OptVertices.resize(OptVertexCount);

    meshopt_remapIndexBuffer(OptIndices.data(), Indices.data(), NumIndices, remap.data());

    meshopt_remapVertexBuffer(OptVertices.data(), Vertices.data(), NumVertices, sizeof(Vector3), remap.data());

    meshopt_optimizeVertexFetch(OptVertices.data(), OptIndices.data(), NumIndices, OptVertices.data(), OptVertexCount, sizeof(Vector3));

    size_t TargetIndexCount = (size_t)(NumIndices * threshold);
    
    float TargetError = 0.0f;
    std::vector<unsigned int> SimplifiedIndices(OptIndices.size());
    size_t OptIndexCount = meshopt_simplify(SimplifiedIndices.data(), OptIndices.data(), NumIndices,
                                            &OptVertices[0].x, OptVertexCount, sizeof(Vector3), TargetIndexCount, TargetError);

    static int num_indices = 0;
    num_indices += (int)NumIndices;
    static int opt_indices = 0;
    opt_indices += (int)OptIndexCount;
    std::cout << "Num indices: " << num_indices << "\n";
    
    std::cout << "Optimized number of indices: " << opt_indices << "\n\n";
    SimplifiedIndices.resize(OptIndexCount);
    
    data.Indices.clear();
    data.Vertices.clear();

    
    data.Indices.insert(data.Indices.end(), SimplifiedIndices.begin(), SimplifiedIndices.end());
    data.Vertices.insert(data.Vertices.end(), OptVertices.begin(), OptVertices.end());

    return data;
}

void calculateNormals(const std::vector<Vector3>& vertices, const std::vector<uint>& indices, float* normals) {
    
    for (size_t i = 0; i < vertices.size(); ++i) {
        normals[i * 3] = 0.0f;
        normals[i * 3 + 1] = 0.0f;
        normals[i * 3 + 2] = 0.0f;
    }

    
    for (size_t i = 0; i < indices.size(); i += 3) {
        Vector3 v0 = vertices[indices[i]];
        Vector3 v1 = vertices[indices[i + 1]];
        Vector3 v2 = vertices[indices[i + 2]];

        Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(v1, v0), Vector3Subtract(v2, v0)));

        
        for (int j = 0; j < 3; ++j) {
            normals[indices[i + j] * 3] += normal.x;
            normals[indices[i + j] * 3 + 1] += normal.y;
            normals[indices[i + j] * 3 + 2] += normal.z;
        }
    }

    
    for (size_t i = 0; i < vertices.size(); ++i) {
        float length = sqrt(normals[i * 3] * normals[i * 3] + normals[i * 3 + 1] * normals[i * 3 + 1] + normals[i * 3 + 2] * normals[i * 3 + 2]);
        normals[i * 3] /= length;
        normals[i * 3 + 1] /= length;
        normals[i * 3 + 2] /= length;
    }
}

Mesh generateLODMesh(const std::vector<Vector3>& vertices, const std::vector<uint>& indices, int vertexCount32, Mesh sourceMesh) {
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
    
    Model model = LoadModel("project/game/models/simple_terrain.obj");
    Mesh mesh = model.meshes[0];
    
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
            if (!IsKeyDown(KEY_X))
                DrawModel(model, Vector3Zero(), 10.0f, RED);
            else
                DrawModelWires(model, Vector3Zero(), 10.0f, RED);
        }
        EndMode3D();

        
        DrawText("Half-Edge Collapsing", 10, 10, 20, BLACK);
        DrawFPS(screenWidth - 90, 10);
        DrawText(TextFormat("Original Vertex Count %d", mesh.vertexCount), 10, 40, 20, BLACK);
        DrawText(TextFormat("New Vertex Count %d", model.meshes[0].vertexCount), 10, 60, 20, BLACK);

        
        rlImGuiBegin();
        if (ImGui::Begin("Inspector Window", NULL))
        {
            if (ImGui::SliderFloat("Simplification Factor", &threshold, 0, 1)) {
                std::vector<unsigned int> lod(mesh.triangleCount * 3);

                Mesh lodMesh = mesh;

                std::vector<uint> indices;
                std::vector<Vector3> vertices;

                for (size_t i = 0; i < lodMesh.vertexCount; ++i) {
                    size_t baseIndex = i * 3;
                    float x = lodMesh.vertices[baseIndex];
                    float y = lodMesh.vertices[baseIndex + 1];
                    float z = lodMesh.vertices[baseIndex + 2];

                    size_t ix;
                    if (lodMesh.indices)
                        ix = lodMesh.indices[i];
                    else
                        ix = i;

                    vertices.push_back({x, y, z});
                    indices.push_back(ix);
                }

                OptimizedMeshData data = OptimizeMesh(lodMesh, indices, vertices, threshold);
                model = LoadModelFromMesh(generateLODMesh(data.Vertices, data.Indices, data.vertexCount, lodMesh));
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