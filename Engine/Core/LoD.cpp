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


struct QuadricErrorMatrix {
    Matrix qem;
    int count; // Count of vertices contributing to this quadric
};

struct VertexIndices {
    std::vector<int> indices;
    std::vector<Vector3> vertices;
};

inline float Vector3DistanceSquared(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

Matrix ComputeQEM(const Vector3& normal, const Vector3& point) {
    Matrix qem = {0};
    qem.m0 += normal.x * normal.x;
    qem.m1 += normal.x * normal.y;
    qem.m2 += normal.x * normal.z;
    qem.m4 += normal.y * normal.y;
    qem.m5 += normal.y * normal.z;
    qem.m8 += normal.z * normal.z;
    qem.m3 = qem.m1;
    qem.m6 = qem.m2;
    qem.m7 = qem.m5;
    qem.m9 = -2 * normal.x * point.x;
    qem.m10 = -2 * normal.y * point.y;
    qem.m11 = -2 * normal.z * point.z;
    qem.m15 = 1;
    return qem;
}

VertexIndices ContractVertices(const Mesh& mesh, float maxDistance) {
    if (mesh.vertices == nullptr || mesh.vertexCount < 3) {
        std::cout << "Mesh has no vertices or is not initialized." << std::endl;
        VertexIndices result;
        return result;  // Return an empty result if mesh is not valid
    }

    const int vertexCount = mesh.vertexCount / 3; // Assuming each vertex has x, y, z components

    std::vector<QuadricErrorMatrix> qemMatrices(vertexCount, {MatrixIdentity(), 0});

    std::vector<bool> isContracted(vertexCount, false);

    for (int i = 0; i < vertexCount; i++) {
        Vector3 vertex_position = {
            mesh.vertices[i * 3], 
            mesh.vertices[i * 3 + 1], 
            mesh.vertices[i * 3 + 2]
        };

        // Calculate a smaller neighborhood range based on the current vertex position
        int searchStart = std::max(i - 8, 0);

        for (int j = searchStart; j < i; j++) {
            Vector3 contracted_position = {
                mesh.vertices[j * 3], 
                mesh.vertices[j * 3 + 1], 
                mesh.vertices[j * 3 + 2]
            };
            float distSq = Vector3DistanceSquared(vertex_position, contracted_position);

            if (distSq <= maxDistance * maxDistance) {
                // Compute normal using cross product
                Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vertex_position, contracted_position), {1, 0, 0}));

                // Compute QEM matrix
                Matrix qem = ComputeQEM(normal, vertex_position);
                qemMatrices[i].qem = MatrixAdd(qemMatrices[i].qem, qem);
                qemMatrices[i].count++;
                isContracted[j] = true;
            }
        }
    }

    // Create a new struct to store unique vertices and indices
    VertexIndices result;

    // Use a smaller maxDistance for better connectivity
    const float smallerMaxDistance = maxDistance * 0.5f;

    for (int i = 0; i < vertexCount; i++) {
        if (!isContracted[i]) {
            // Compute the contracted vertex position using the QEM matrix
            Vector3 contractedVertex = {0};
            if (qemMatrices[i].count > 0) {
                Matrix inverseQEM = MatrixInvert(qemMatrices[i].qem);
                contractedVertex = Vector3Transform(Vector3Zero(), inverseQEM);
            } else {
                contractedVertex = {
                    mesh.vertices[i * 3], 
                    mesh.vertices[i * 3 + 1], 
                    mesh.vertices[i * 3 + 2]
                };
            }

            // Add the contracted vertex to the result
            result.vertices.push_back(contractedVertex);
            result.indices.push_back(i * 3);  // Assuming each vertex represents a new triangle
            result.indices.push_back(i * 3 + 1);
            result.indices.push_back(i * 3 + 2);
        }
    }

    return result;
}

// Function to generate a simplified LOD mesh
Mesh GenerateLODMesh(VertexIndices meshData, Mesh& sourceMesh) {
    Mesh lodMesh = { 0 };

    if (!meshData.vertices.empty()) {
        int vertexCount = meshData.vertices.size();
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

        // Calculate the bounding box of the mesh
        Vector3 minVertex = meshData.vertices[0];
        Vector3 maxVertex = meshData.vertices[0];
        for (const auto& vertex : meshData.vertices) {
            if (vertex.x < minVertex.x) minVertex.x = vertex.x;
            if (vertex.x > maxVertex.x) maxVertex.x = vertex.x;
            if (vertex.y < minVertex.y) minVertex.y = vertex.y;
            if (vertex.y > maxVertex.y) maxVertex.y = vertex.y;
            if (vertex.z < minVertex.z) minVertex.z = vertex.z;
            if (vertex.z > maxVertex.z) maxVertex.z = vertex.z;
        }

        // Calculate the scaling factors for texture coordinates
        float scaleX = 1.0f / (maxVertex.x - minVertex.x);
        float scaleY = 1.0f / (maxVertex.y - minVertex.y);

        // Assign texture coordinates based on the scaling factors
        for (int i = 0; i < vertexCount; i++) {
            lodMesh.vertices[i * 3] = meshData.vertices[i].x;
            lodMesh.vertices[i * 3 + 1] = meshData.vertices[i].y;
            lodMesh.vertices[i * 3 + 2] = meshData.vertices[i].z;

            lodMesh.texcoords[i * 2] = (meshData.vertices[i].x - minVertex.x) * scaleX;
            lodMesh.texcoords[i * 2 + 1] = (meshData.vertices[i].y - minVertex.y) * scaleY;
        }

        // Generate new indices for non-indexed mesh
        if (sourceMesh.indices) {
            // Allocate memory for the indices
            lodMesh.indices = (unsigned short*)malloc(sizeof(unsigned short) * indexCount);

            // Copy indices from the source mesh
            for (int i = 0; i < indexCount; i++) {
                lodMesh.indices[i] = meshData.indices.at(i);
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
