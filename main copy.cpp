#include "raylib.h"
#include "raymath.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <fstream>


typedef struct
{
    float x, y, z;
} Vertex;
std::vector<Vertex> vertices_list;


int main()
{
    // Initialize raylib
    InitWindow(800, 450, "example");

    // Load a 3D model
    Model model = LoadModel("assets/models/test.obj");


    // Iterate through the meshes
    for(int i=0;i<model.meshCount;i++){
        //Create a dynamic array of Vertex struct
        Vertex *vertices = (Vertex*) malloc(model.meshes[i].vertexCount*sizeof(Vertex));
        // Iterate through the vertices of the mesh
        for(int j=0;j<model.meshes[i].vertexCount;j++){
            Vertex vertice;
            vertice.x = model.meshes[i].vertices[j*3];
            vertice.y = model.meshes[i].vertices[j*3+1];
            vertice.z = model.meshes[i].vertices[j*3+2];
            vertices_list.push_back(vertice);
        }
    }



    std::vector<short unsigned int> newIndicesList;
    newIndicesList.reserve(1000000);
    newIndicesList.resize(1000000);
    
    for(int i=0;i<model.meshCount;i++){
        // Iterate through the indices of the mesh
        for(int j=0;j<model.meshes[i].triangleCount*3;j++){
            int index = &model.meshes[i].indices[j];
            newIndicesList.push_back(index);
        }
        break;
    }


    std::vector<Vertex> newVerticesList;
    for (int i = 0; i < vertices_list.size(); i++)
    {
        Vertex closestVertex = vertices_list[i];
        float closestDistance = 1;
        for (int j = 0; j < vertices_list.size(); j++)
        {
            if (i == j) continue; // Skip comparison with itself
            float distance = sqrt(pow(vertices_list[i].x - vertices_list[j].x, 2) + pow(vertices_list[i].y - vertices_list[j].y, 2) + pow(vertices_list[i].z - vertices_list[j].z, 2));
            if (distance < closestDistance)
            {
                closestVertex = vertices_list[j];
                closestDistance = distance;
            }
        }
        newVerticesList.push_back(closestVertex);
    }


    for (int i = 0; i < sizeof(Vertex); i++)
    {
        std::cout << "Vector3(" << newVerticesList[i].x << ", " << newVerticesList[i].y << ", " << newVerticesList[i].z << ")" << std::endl;
    }


    // Create the file and open it for writing
    std::ofstream objFile("model.obj");

    // Write the vertex data to the file
    for (int i = 0; i < newVerticesList.size(); i++)
    {
        objFile << "v " << newVerticesList[i].x << " " << newVerticesList[i].y << " " << newVerticesList[i].z << std::endl;
    }

    // Close the file
    objFile.close();




    // Create an empty array for indices
    int indices = NULL;
    int indexCount = 0;


    float* vertices = (float*)newVerticesList.data();
    int vertexCount = newVerticesList.size();

    int faceCount = 0;
    unsigned int materialCount = 0;






    
    std::cout << "Faces: " << faceCount << std::endl;
    
    // Create the mesh
    Mesh mesh = { 0 };
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = faceCount;
    mesh.vertices = vertices;
    mesh.indices = (short unsigned int*)newIndicesList.data();

    UploadMesh(&mesh, false);

    Model model2 = LoadModelFromMesh(mesh);
    
    // Unload the model and close the window

    Camera camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;


    SetCameraMode(camera, CAMERA_FREE); // Set a free camera mode


    



    while (!WindowShouldClose())
    {
        UpdateCamera(&camera);
        BeginDrawing();
            BeginMode3D(camera);

            ClearBackground(GRAY);


            DrawModel(model2, (Vector3){ 0, 0, 0 }, 3.0f, BLACK);
            DrawModel(model, (Vector3){ 10, 0, 0 }, 1.0f, RED);




            DrawGrid(100, 1.0f);


            DrawFPS(10, 10);
            EndMode3D();

        EndDrawing();
    }


    UnloadModel(model);
    CloseWindow();

    return 0;
}
