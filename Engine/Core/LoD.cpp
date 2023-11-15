#include "../../include_all.h"
#include <queue>

const float LOD_DISTANCE_HIGH = 10.0f;
const float LOD_DISTANCE_MEDIUM = 25.0f;
const float LOD_DISTANCE_LOW = 35.0f;

typedef struct Cluster
{
    Color color;
    int lodLevel;
    // std::vector<Entity> entities;
};

struct VertexPair {
    int v1;
    int v2;
};

// Helper functions
float Vector4DotProduct(const Vector4& v1, const Vector4& v2) {
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z + v1.w * v2.w;
}

Vector4 Vector4Transform(const Vector4& v, const Matrix& mat) {
    Vector4 result;

    result.x = v.x * mat.m0 + v.y * mat.m1 + v.z * mat.m2 + v.w * mat.m3;
    result.y = v.x * mat.m4 + v.y * mat.m5 + v.z * mat.m6 + v.w * mat.m7;
    result.z = v.x * mat.m8 + v.y * mat.m9 + v.z * mat.m10 + v.w * mat.m11;
    result.w = v.x * mat.m12 + v.y * mat.m13 + v.z * mat.m14 + v.w * mat.m15;

    return result;
}

Vector4 Vector4Add(Vector4 v1, Vector4 v2)
{
    Vector4 result = { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z , v1.w + v2.w };

    return result;
}


float determinant3x3(float a, float b, float c, float d, float e, float f, float g, float h, float i) {
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

Matrix Matrix4Invert(const Matrix& mat) {
    Matrix result;

    // Calculate the determinant of the 4x4 matrix
    float det = mat.m0 * determinant3x3(mat.m5, mat.m6, mat.m7, mat.m9, mat.m10, mat.m11, mat.m13, mat.m14, mat.m15)
                - mat.m1 * determinant3x3(mat.m4, mat.m6, mat.m7, mat.m8, mat.m10, mat.m11, mat.m12, mat.m14, mat.m15)
                + mat.m2 * determinant3x3(mat.m4, mat.m5, mat.m7, mat.m8, mat.m9, mat.m11, mat.m12, mat.m13, mat.m15)
                - mat.m3 * determinant3x3(mat.m4, mat.m5, mat.m6, mat.m8, mat.m9, mat.m10, mat.m12, mat.m13, mat.m14);

    if (std::fabs(det) < 1e-8) {
        // Matrix is singular, cannot invert
        // Handle this case appropriately for your application
        // For now, just return an identity matrix
        return Matrix{1.0f, 0.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f, 0.0f,
                       0.0f, 0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 0.0f, 1.0f};
    }

    // Calculate the inverse of the 4x4 matrix
    float invDet = 1.0f / det;

    result.m0 = determinant3x3(mat.m5, mat.m6, mat.m7, mat.m9, mat.m10, mat.m11, mat.m13, mat.m14, mat.m15) * invDet;
    // (similar calculations for other elements)

    return result;
}

Vector4 Vector4OuterProduct(const Vector4& v1, const Vector4& v2) {
    Vector4 result;

    result.x = v1.x * v2.x;
    result.y = v1.y * v2.y;
    result.z = v1.z * v2.z;
    result.w = v1.w * v2.w;

    return result;
}


struct VertexIndices {
    std::vector<size_t> indices;
    std::vector<Vector3> vertices;
};

class SimplifyMesh {
public:
    VertexIndices result;  // Store the simplified vertices here
    VertexIndices simplify(Mesh& mesh, float factor);

private:
    float calculateError(const Mesh& mesh, size_t v1, size_t v2);
    void collapse(Mesh& mesh, size_t u, size_t v);
};

VertexIndices SimplifyMesh::simplify(Mesh& mesh, float factor) {
    // Simplify the mesh here...

    // This is a simplified version of the algorithm
    // and may not work for all meshes or factors.

    // Calculate the target number of vertices
    size_t targetCount = static_cast<size_t>(mesh.vertexCount * factor);
    while (mesh.vertexCount > targetCount) {
        // Find the edge with the smallest error
        size_t minErrorV1 = 0;
        size_t minErrorV2 = 0;
        float minError = calculateError(mesh, 0, 1);
        for (size_t i = 0; i < mesh.vertexCount; i++) {
            for (size_t j = i + 1; j < mesh.vertexCount; j++) {
                float error = calculateError(mesh, i, j);
                if (error < minError) {
                    minError = error;
                    minErrorV1 = i;
                    minErrorV2 = j;
                }
            }
        }

        // Collapse the edge with the smallest error
        collapse(mesh, minErrorV1, minErrorV2);
    }

    // Store the simplified mesh in the result member
    result.indices.clear();
    result.vertices.clear();
    for (size_t i = 0; i < mesh.vertexCount; i++) {
        result.indices.push_back(i);
        result.vertices.push_back({ mesh.vertices[i], mesh.vertices[i] + 1, mesh.vertices[i] + 2 });
    }

    return result;
}

float SimplifyMesh::calculateError(const Mesh& mesh, size_t v1, size_t v2) {
    // Calculate the error of collapsing edge (v1, v2)
    // This is a placeholder implementation and should be replaced
    // with a proper error calculation based on the mesh geometry.
    return Vector3Distance({mesh.vertices[v1], mesh.vertices[v1] + 1, mesh.vertices[v1] + 2 }, { mesh.vertices[v2], mesh.vertices[v2] + 1, mesh.vertices[v2] + 2 });
}

void SimplifyMesh::collapse(Mesh& mesh, size_t u, size_t v) {
    // Collapse the edge (u, v)
    // This is a placeholder implementation and should be replaced
    // with a proper edge collapse operation based on the mesh geometry.
    result.vertices.reserve(1);
    
    result.vertices[u] = (Vector3) {
        (mesh.vertices[u] + 0 + mesh.vertices[v] + 0 ) / 2,
        (mesh.vertices[u] + 1 + mesh.vertices[v] + 1 ) / 2,
        (mesh.vertices[u] + 2 + mesh.vertices[v] + 2 ) / 2
        };

    result.vertices.erase(result.vertices.begin() + v);
    mesh.vertexCount--;
}




// Function to generate a simplified LOD mesh with recalculated texture coordinates
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

        // Calculate the bounding box of the contracted mesh
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
