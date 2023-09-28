#include "raylib.h"
#include <vector>
#include <iostream>
#include <cmath>
#include "raymath.h"

using namespace std;

typedef struct Entities
{
    Vector3 position;
    Model model;
};

typedef struct Cluster
{
    Color color;
    int lodLevel;
    std::vector<Entities> entities;
};

int main() {
    SetTraceLogLevel(LOG_WARNING);
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "HLOD");
    
    Camera3D camera;
    camera.position = { 0.0f, 0.0f, 50.0f };
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

        for (int j = 0; j < 100; j++) {
            Entities entity;
            entity.model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
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
        UpdateCamera(&camera, CAMERA_FREE);

        // Iterate through clusters and group them into LOD levels based on positions
        for (size_t i = 0; i < clusters.size(); i++) {
            int clusterIndex = static_cast<int>((clusters[i].entities[0].position.x + clusters[i].entities[0].position.y + clusters[i].entities[0].position.z) / 3) + 5;
            
            // Ensure clusterIndex is within bounds
            clusterIndex = Clamp(clusterIndex, 0, 9);
            
            // Determine the LOD level based on the cluster's distance from the camera (you can adjust the threshold as needed)
            float distance = Vector3Distance(clusters[i].entities[0].position, camera.position);
            if (distance < 10.0f) {
                clusters[i].lodLevel = 0; // Highest LOD
                clusters[i].color = GREEN;
            } else if (distance < 20.0f) {
                clusters[i].lodLevel = 1; // Medium LOD
                clusters[i].color = YELLOW;
            } else {
                clusters[i].lodLevel = 2; // Lowest LOD
                clusters[i].color = RED;
            }
            
            // Draw all entities in the cluster with the cluster's color and LOD level
            for (size_t j = 0; j < clusters[i].entities.size(); j++) {
                DrawModel(clusters[i].entities[j].model, clusters[i].entities[j].position, 1.0f, clusters[i].color);
            }
        }

        EndMode3D();
        
        EndDrawing();
    }
    
    // Unload models
    for (size_t i = 0; i < clusters.size(); i++) {
        for (size_t j = 0; j < clusters[i].entities.size(); j++) {
            UnloadModel(clusters[i].entities[j].model);
        }
    }
    
    // Clean up and close the window
    CloseWindow();
    
    return 0;
}
