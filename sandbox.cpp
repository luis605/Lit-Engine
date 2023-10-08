#include <iostream>
#include <vector>
#include <unordered_map>
#include <cmath>
#include "include/raylib.h"

// Define a custom hash function for Vector3
struct Vector3Hash {
    std::size_t operator()(const Vector3& v) const {
        std::size_t h1 = std::hash<float>{}(v.x);
        std::size_t h2 = std::hash<float>{}(v.y);
        std::size_t h3 = std::hash<float>{}(v.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// Define a custom equality comparison function for Vector3
struct Vector3Equal {
    bool operator()(const Vector3& v1, const Vector3& v2) const {
        return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z;
    }
};

std::unordered_map<Vector3, Vector3, Vector3Hash, Vector3Equal> AssignGridIDs(const std::vector<Vector3>& vertices, float gridSize) {
    std::unordered_map<Vector3, Vector3, Vector3Hash, Vector3Equal> vertexGridID;
    
    // Calculate the grid position for each vertex
    for (int i = 0; i < vertices.size(); ++i) {
        const Vector3& vertex = vertices[i];
        
        int gridX = static_cast<int>(std::floor(vertex.x / gridSize));
        int gridY = static_cast<int>(std::floor(vertex.y / gridSize));
        int gridZ = static_cast<int>(std::floor(vertex.z / gridSize));
        
        // Create a Vector3 grid position
        Vector3 gridPosition = { gridX * gridSize, gridY * gridSize, gridZ * gridSize };
        
        // Store the vertex position and its corresponding grid position in the unordered_map
        vertexGridID[vertex] = gridPosition;
    }
    
    return vertexGridID;
}

std::vector<Vector3> GetNearbyVertices(const std::unordered_map<Vector3, Vector3, Vector3Hash, Vector3Equal>& vertexGridID, const Vector3& targetVertex, float gridSize) {
    std::vector<Vector3> nearbyVertices;

    // Calculate the grid position of the target vertex
    int gridX = static_cast<int>((targetVertex.x / gridSize));
    int gridY = static_cast<int>((targetVertex.y / gridSize));
    int gridZ = static_cast<int>((targetVertex.z / gridSize));

    // Iterate through nearby grid positions (e.g., a 3x3x3 cube of grids)
    for (int xOffset = -1; xOffset <= 1; ++xOffset) {
        for (int yOffset = -1; yOffset <= 1; ++yOffset) {
            for (int zOffset = -1; zOffset <= 1; ++zOffset) {
                // Calculate the neighboring grid position
                int nearbyGridX = gridX + xOffset;
                int nearbyGridY = gridY + yOffset;
                int nearbyGridZ = gridZ + zOffset;

                // Create a Vector3 representing the nearby grid position
                Vector3 nearbyGridPosition{
                    nearbyGridX * gridSize,
                    nearbyGridY * gridSize,
                    nearbyGridZ * gridSize
                };

                // Check if the nearby grid position exists in the map
                auto it = vertexGridID.find(nearbyGridPosition);
                if (it != vertexGridID.end()) {
                    // If it exists, add the associated vertex to the result
                    nearbyVertices.push_back(it->second);
                }
            }
        }
    }

    return nearbyVertices;
}

int main() {
    std::vector<Vector3> vertices = {
        {0.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {0.0f, 2.0f, 0.0f},
        {2.0f, 5.0f, 0.0f},
        {0.0f, 0.0f, 2.0f},
        {2.0f, 0.0f, 4.0f},
        {0.0f, 2.0f, 3.0f},
        {2.0f, 2.0f, 2.0f}
    };

    float gridSize = 2.0f; // Adjust the grid size as needed
    
    std::unordered_map<Vector3, Vector3, Vector3Hash, Vector3Equal> vertexGridID = AssignGridIDs(vertices, gridSize);

    // Get nearby vertices of a specific target vertex (e.g., vertex 0)
    Vector3 targetVertex = {0.0f, 0.0f, 0.0f};
    std::vector<Vector3> nearbyVertices = GetNearbyVertices(vertexGridID, targetVertex, gridSize);

    // Output the nearby vertices
    std::cout << "Nearby vertices of target vertex (" << targetVertex.x << ", " << targetVertex.y << ", " << targetVertex.z << "):" << std::endl;
    for (const Vector3& nearbyVertex : nearbyVertices) {
        std::cout << "Vertex (" << nearbyVertex.x << ", " << nearbyVertex.y << ", " << nearbyVertex.z << ")" << std::endl;
    }

    return 0;
}
