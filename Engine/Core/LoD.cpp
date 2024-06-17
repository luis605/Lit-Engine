#include "../../include_all.h"

const float LOD_DISTANCE_HIGH = 10.0f;
const float LOD_DISTANCE_MEDIUM = 25.0f;
const float LOD_DISTANCE_LOW = 35.0f;

struct OptimizedMeshData {
	std::vector<unsigned int> Indices;
    std::vector<Vector3> Vertices;
    int vertexCount;

    OptimizedMeshData() {}

    OptimizedMeshData(std::vector<unsigned int> indices, std::vector<Vector3> vertices)
        : Indices(indices), Vertices(vertices), vertexCount(vertices.size()) {}

    OptimizedMeshData(const OptimizedMeshData& other)
        : Indices(other.Indices), Vertices(other.Vertices), vertexCount(other.vertexCount) {}

    OptimizedMeshData& operator=(const OptimizedMeshData& other) {
        if (this != &other) {
            Indices = other.Indices;
            Vertices = other.Vertices;
            vertexCount = other.vertexCount;
        }
        return *this;
    }
};

OptimizedMeshData OptimizeMesh(std::vector<unsigned int>& Indices, std::vector<Vector3>& Vertices, float threshold) {
    OptimizedMeshData data;
    size_t NumIndices = Indices.size();
    size_t NumVertices = Vertices.size();

    if ((NumIndices % 3 != 0) || NumVertices < 100) {
        std::cerr << "Error: Number of indices must be a multiple of 3 and vertices should be more than 100." << std::endl;
        return data;
    }

    size_t target_index_count = static_cast<size_t>(NumIndices * threshold);
    float target_error = 1e-2f;
    float result_error = 0;

    data.Indices.resize(Indices.size());

    size_t optimized_index_count = meshopt_simplify(
        &data.Indices[0], &Indices[0], NumIndices, &Vertices[0].x, NumVertices, 
        sizeof(Vector3), target_index_count, target_error);

    data.Indices.resize(optimized_index_count);

    std::vector<unsigned int> remap(NumVertices);
    size_t optimized_vertex_count = meshopt_generateVertexRemap(
        remap.data(), &data.Indices[0], optimized_index_count, 
        &Vertices[0], NumVertices, sizeof(Vector3)
    );

    data.Vertices.resize(optimized_vertex_count);

    meshopt_remapVertexBuffer(&data.Vertices[0], &Vertices[0], NumVertices, sizeof(Vector3), remap.data());
    meshopt_remapIndexBuffer(&data.Indices[0], &data.Indices[0], optimized_index_count, remap.data());

    meshopt_optimizeVertexCache(&data.Indices[0], &data.Indices[0], optimized_index_count, optimized_vertex_count);
    meshopt_optimizeOverdraw(&data.Indices[0], &data.Indices[0], optimized_index_count, &data.Vertices[0].x, optimized_vertex_count, sizeof(Vector3), 1.05f);
    meshopt_optimizeVertexFetch(&data.Vertices[0], &data.Indices[0], optimized_index_count, &data.Vertices[0], optimized_vertex_count, sizeof(Vector3));

    data.vertexCount = optimized_vertex_count;

    return data;
}

void calculateNormals(const std::vector<Vector3>& vertices, const std::vector<unsigned int>& indices, float* normals) {
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
        if (length > 0) {
            normals[i * 3] /= length;
            normals[i * 3 + 1] /= length;
            normals[i * 3 + 2] /= length;
        }
    }
}

Mesh generateLODMesh(const std::vector<Vector3>& vertices, const std::vector<unsigned int>& indices, int vertexCount32, Mesh sourceMesh) {
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
    lodMesh.normals = (float*)malloc(sizeof(float) * 3 * vertexCount);

    if (!lodMesh.vertices || !lodMesh.indices || !lodMesh.normals) {
        TraceLog(LOG_ERROR, "generateLODMesh: Memory allocation failed.");
        if (lodMesh.vertices) free(lodMesh.vertices);
        if (lodMesh.indices) free(lodMesh.indices);
        if (lodMesh.normals) free(lodMesh.normals);
        return sourceMesh;
    }

    calculateNormals(vertices, indices, lodMesh.normals);

    for (int i = 0; i < vertexCount; i++) {
        lodMesh.vertices[i * 3] = vertices[i].x;
        lodMesh.vertices[i * 3 + 1] = vertices[i].y;
        lodMesh.vertices[i * 3 + 2] = vertices[i].z;
    }

    for (size_t i = 0; i < indices.size(); i++) {
        lodMesh.indices[i] = indices[i];
    }

    UploadMesh(&lodMesh, false);

    free(lodMesh.vertices);
    free(lodMesh.indices);
    free(lodMesh.normals);

    return lodMesh;
}
