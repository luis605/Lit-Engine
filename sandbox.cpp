#include "raylib.h"
#include "raymath.h"
#include "ufbx/ufbx.h"
#include <iostream>

Model ConvertUfbxMeshToModel(ufbx_mesh* ufbxMesh) {
    Model model = { 0 };

    if (ufbxMesh) {
        int vertexCount = ufbxMesh->num_vertices;
        int indexCount = vertexCount * 3;

        printf("Vertex Count: %d, Index Count: %d\n", vertexCount, indexCount);

        // Create arrays to store the converted data
        float* vertices = (float*)RL_MALLOC(vertexCount * 3 * sizeof(float));
        float* normals = (float*)RL_MALLOC(vertexCount * 3 * sizeof(float));
        float* texcoords = (float*)RL_MALLOC(vertexCount * 2 * sizeof(float));

        // Check if UV coordinates are available
        if (ufbxMesh->vertex_uv.values.data) {
            // Allocate memory for texcoords and copy ufbx UV data to the array
            for (int i = 0; i < vertexCount; i++) {
                texcoords[i * 2] = ufbxMesh->vertex_uv.values[i].x;
                texcoords[i * 2 + 1] = 1.0f - ufbxMesh->vertex_uv.values[i].y; // Flip Y-coordinate if needed
            }
        }

        // Copy ufbx position and normal data to the arrays
        for (int i = 0; i < vertexCount; i++) {
            vertices[i * 3] = ufbxMesh->vertex_position[i].x;
            vertices[i * 3 + 1] = ufbxMesh->vertex_position[i].y;
            vertices[i * 3 + 2] = ufbxMesh->vertex_position[i].z;

            normals[i * 3] = ufbxMesh->vertex_normal[i].x;
            normals[i * 3 + 1] = ufbxMesh->vertex_normal[i].y;
            normals[i * 3 + 2] = ufbxMesh->vertex_normal[i].z;
        }

        // Allocate memory for indices
        unsigned short* indices = (unsigned short*)RL_MALLOC(indexCount * sizeof(unsigned short));

        // Copy ufbx indices and convert data type
        for (int i = 0; i < indexCount; i++) {
            indices[i] = (unsigned short)ufbxMesh->vertex_indices.data[i];
        }

        // Create a raylib mesh from the converted data
        Mesh raylibMesh = { 0 };
        raylibMesh.vertices = vertices;
        raylibMesh.normals = normals;
        raylibMesh.texcoords = texcoords; // Set UV coordinates if available
        raylibMesh.indices = indices; // Indices are not needed initially
        raylibMesh.vertexCount = vertexCount;
        raylibMesh.triangleCount = indexCount / 3;

        // Generate tangents and upload the mesh
        GenMeshTangents(&raylibMesh);
        UploadMesh(&raylibMesh, true);

        // Set the raylib mesh to the model
        model.meshes = (Mesh*)RL_MALLOC(sizeof(Mesh) * 1);
        model.meshes[0] = raylibMesh;
        model.meshCount = 1;

        // Set up the material
        model.materials = (Material*)RL_MALLOC(sizeof(Material) * 1);
        model.materialCount = 1;
        model.materials[0] = LoadMaterialDefault(); // You can customize this material

        // Make sure the model uses this material
        model.meshMaterial = (int*)RL_MALLOC(sizeof(int) * 1);
        model.meshMaterial[0] = 0;
    }

    return model;
}




Model ProcessUfbxNode(ufbx_node* node, Model* model) {

    // Check if this node has a mesh
    if (node->mesh) {
        printf("Object: %s\n", node->name.data);
        printf("-> Mesh with %zu faces\n", node->mesh->faces.count);

        // Convert ufbx mesh to a raylib model
        *model = ConvertUfbxMeshToModel(node->mesh);
    } else if (node->is_root)
    {
        ufbx_node* child = node->children[0];
        if (child) {
            model = &ProcessUfbxNode(child, model);
        }
    }

    return *model;
}


int main() {
    SetTraceLogLevel(LOG_WARNING);

    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "UFBX DEMO");

    Camera3D camera;
    camera.position = (Vector3){10, 10, 10};
    camera.target = (Vector3){0, 0, 0};
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    ufbx_load_opts opts = {0};
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file("assets/model.fbx", &opts, &error);
    if (!scene) {
        TraceLog(LOG_ERROR, "Failed to load ufbx model.");
        CloseWindow();
        return 1;
    }

    Model model = {0};
    model = ProcessUfbxNode(scene->nodes.data[0], &model);

    DisableCursor();

    while (!WindowShouldClose()) {
        // Draw
        BeginDrawing();
        ClearBackground(GRAY);

        // Update the camera and projection matrix
        UpdateCamera(&camera, CAMERA_FREE);

        // Begin 3D drawing
        BeginMode3D(camera);

        // Draw the converted model
        if (IsModelReady(model)) {
            DrawModel(model, (Vector3){0, 0, 0}, 10.0f, WHITE);
        }

        // End 3D drawing
        EndMode3D();

        DrawFPS(10, 10);
        EndDrawing();
    

    }

    UnloadModel(model);
    CloseWindow();

    return 0;
}
