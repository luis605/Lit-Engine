#include "../../include_all.h"

const float LOD_DISTANCE_HIGH = 10.0f;
const float LOD_DISTANCE_MEDIUM = 25.0f;
const float LOD_DISTANCE_LOW = 35.0f;

typedef struct Cluster
{
    Color color;
    int lodLevel;
    // std::vector<Entity> entities;
};

inline float Vector3DistanceSquared(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// Define the spatial hash cell size (adjust as needed)
std::vector<Vector3> ContractVertices(const Mesh& mesh, float maxDistance) {
    if (mesh.vertices == nullptr || mesh.vertexCount == 0) {
        std::cout << "Mesh has no vertices or is not initialized." << std::endl;
        return std::vector<Vector3>();
    }

    const float maxDistanceSquared = maxDistance * maxDistance;
    const int vertexCount = mesh.vertexCount;

    std::vector<Vector3> contractedVertices(vertexCount, Vector3{0.0f, 0.0f, 0.0f});

    std::vector<int> sortedIndices(vertexCount);
    #pragma omp parallel for
    for (int i = 0; i < vertexCount; i++) {
        sortedIndices[i] = i;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        return mesh.vertices[a * 3] < mesh.vertices[b * 3];
    });

    #pragma omp parallel for
    for (int i = 0; i < vertexCount; i++) {
        int idx = sortedIndices[i];
        float xi = mesh.vertices[idx * 3];
        float yi = mesh.vertices[idx * 3 + 1];
        float zi = mesh.vertices[idx * 3 + 2];
        Vector3 vertex_position = { xi, yi, zi };

        int closestVertexIndex = -1;
        float closestDistance = maxDistanceSquared;

        // Calculate a smaller neighborhood range based on current vertex position
        int searchStart = i - 4;

        searchStart = std::max(searchStart, 0);

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

    // Create a new vector to store unique vertices
    std::vector<Vector3> uniqueVertices;

    // Define the cell size for spatial hashing (adjust as needed)
    const float cellSize = maxDistance * 0.5f;

    // Create a hash map for spatial hashing
    std::unordered_map<uint64_t, Vector3> spatialHashMap;

    #pragma omp parallel for
    for (int i = 0; i < contractedVertices.size(); i++) {
        const Vector3& vertex = contractedVertices[i];

        // Calculate the cell coordinates for the vertex
        int cellX = static_cast<int>(vertex.x / cellSize);
        int cellY = static_cast<int>(vertex.y / cellSize);
        int cellZ = static_cast<int>(vertex.z / cellSize);

        // Generate a hash key based on cell coordinates
        uint64_t hashKey = static_cast<uint64_t>(cellX) * 73856093ULL ^
                        static_cast<uint64_t>(cellY) * 19349663ULL ^
                        static_cast<uint64_t>(cellZ) * 83492791ULL;

        // Check if the hash key is already in the spatialHashMap
        #pragma omp critical
        auto it = spatialHashMap.find(hashKey);
        if (it == spatialHashMap.end()) {
            // Vertex is unique within its cell, add it to uniqueVertices
            spatialHashMap[hashKey] = vertex;
            uniqueVertices.push_back(vertex);
        } else {
            // Check distance to previously stored vertex in the same cell
            const Vector3& storedVertex = it->second;
            if (Vector3DistanceSquared(vertex, storedVertex) >= maxDistanceSquared * 0.25f) {
                // Vertex is unique, add it to uniqueVertices
                spatialHashMap[hashKey] = vertex;
                uniqueVertices.push_back(vertex);
            }
        }
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
        lodMesh.normals = (float*)malloc(sizeof(float) * 3 * vertexCount);
        lodMesh.texcoords = (float*)malloc(sizeof(float) * 2 * vertexCount); // Allocate memory for texcoords
        lodMesh.texcoords2 = (float*)malloc(sizeof(float) * 2 * vertexCount); // Allocate memory for texcoords2
        lodMesh.colors = (unsigned char*)malloc(sizeof(unsigned char) * 4 * vertexCount); // Allocate memory for colors
        lodMesh.tangents = (float*)malloc(sizeof(float) * 4 * vertexCount); // Allocate memory for tangents
        lodMesh.boneWeights = (float*)malloc(sizeof(float) * 4 * vertexCount); // Allocate memory for boneWeights

        // Copy unique vertices to the new mesh's vertex array
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = uniqueVertices[i].x;
            lodMesh.vertices[i * 3 + 1] = uniqueVertices[i].y;
            lodMesh.vertices[i * 3 + 2] = uniqueVertices[i].z;
        }

        // Generate new indices for non-indexed mesh
        if (sourceMesh.indices) {
            // Allocate memory for the indices
            lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indexCount);

            // Copy indices from the source mesh
            for (int i = 0; i < indexCount; i++) {
                lodMesh.indices[i] = sourceMesh.indices[i];
            }

            // Calculate normals for the mesh
            for (int i = 0; i < triangleCount; i++) {
                // Get the indices of the vertices of the current triangle
                unsigned short index1 = lodMesh.indices[i * 3];
                unsigned short index2 = lodMesh.indices[i * 3 + 1];
                unsigned short index3 = lodMesh.indices[i * 3 + 2];

                // Get the positions of the vertices of the current triangle
                Vector3 v1 = { lodMesh.vertices[index1 * 3], lodMesh.vertices[index1 * 3 + 1], lodMesh.vertices[index1 * 3 + 2] };
                Vector3 v2 = { lodMesh.vertices[index2 * 3], lodMesh.vertices[index2 * 3 + 1], lodMesh.vertices[index2 * 3 + 2] };
                Vector3 v3 = { lodMesh.vertices[index3 * 3], lodMesh.vertices[index3 * 3 + 1], lodMesh.vertices[index3 * 3 + 2] };

                // Calculate the normal for the current triangle
                Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(v2, v1), Vector3Subtract(v3, v1)));

                // Assign the normal to each vertex of the current triangle
                lodMesh.normals[index1 * 3] += normal.x;
                lodMesh.normals[index1 * 3 + 1] += normal.y;
                lodMesh.normals[index1 * 3 + 2] += normal.z;

                lodMesh.normals[index2 * 3] += normal.x;
                lodMesh.normals[index2 * 3 + 1] += normal.y;
                lodMesh.normals[index2 * 3 + 2] += normal.z;

                lodMesh.normals[index3 * 3] += normal.x;
                lodMesh.normals[index3 * 3 + 1] += normal.y;
                lodMesh.normals[index3 * 3 + 2] += normal.z;
            }

            // Normalize the normals
            for (int i = 0; i < vertexCount; i++) {
                Vector3 normal = { lodMesh.normals[i * 3], lodMesh.normals[i * 3 + 1], lodMesh.normals[i * 3 + 2] };
                normal = Vector3Normalize(normal);
                lodMesh.normals[i * 3] = normal.x;
                lodMesh.normals[i * 3 + 1] = normal.y;
                lodMesh.normals[i * 3 + 2] = normal.z;
            }
        }
        else {
            lodMesh.indices = sourceMesh.indices;
        }

        // Assuming texture coordinates are already available in the sourceMesh
        if (sourceMesh.texcoords) {
            for (int i = 0; i < vertexCount; i++) {
                lodMesh.texcoords[i * 2] = sourceMesh.texcoords[i * 2];
                lodMesh.texcoords[i * 2 + 1] = sourceMesh.texcoords[i * 2 + 1];
            }
        }
    }

    // Upload the mesh data to GPU
    UploadMesh(&lodMesh, false);

    // Free the allocated memory before returning
    if (lodMesh.vertices) {
        free(lodMesh.vertices);
        lodMesh.vertices = NULL;
    }
    if (lodMesh.indices) {
        free(lodMesh.indices);
        lodMesh.indices = NULL;
    }
    if (lodMesh.normals) {
        free(lodMesh.normals);
        lodMesh.normals = NULL;
    }
    if (lodMesh.texcoords) {
        free(lodMesh.texcoords);
        lodMesh.texcoords = NULL;
    }
    if (lodMesh.texcoords2) {
        free(lodMesh.texcoords2);
        lodMesh.texcoords2 = NULL;
    }
    if (lodMesh.colors) {
        free(lodMesh.colors);
        lodMesh.colors = NULL;
    }
    if (lodMesh.tangents) {
        free(lodMesh.tangents);
        lodMesh.tangents = NULL;
    }
    if (lodMesh.boneWeights) {
        free(lodMesh.boneWeights);
        lodMesh.boneWeights = NULL;
    }

    return lodMesh;
}
