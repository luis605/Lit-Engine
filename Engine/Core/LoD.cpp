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

struct OptimizedMeshData {
    std::vector<uint32_t> Indices;
    std::vector<Vector3> Vertices;
    int vertexCount;

    OptimizedMeshData(std::vector<uint32_t> indices, std::vector<Vector3> vertices)
        : Indices(indices), Vertices(vertices) {}

    // Provide a custom copy constructor
    OptimizedMeshData(const OptimizedMeshData& other)
        : Indices(other.Indices), Vertices(other.Vertices), vertexCount(other.vertexCount) {}

    // Provide a custom assignment operator
    OptimizedMeshData operator=(const OptimizedMeshData other) {

        if (!other.Indices.empty())
           Indices.assign(other.Indices.begin(), other.Indices.end());

        if (!other.Vertices.empty())
            Vertices.assign(other.Vertices.begin(), other.Vertices.end());

        vertexCount = other.vertexCount;
        return *this;
    }
};



OptimizedMeshData OptimizeMesh(Mesh& mesh, std::vector<uint32_t>& Indices, std::vector<Vector3>& Vertices, float threshold)
{
    OptimizedMeshData data(Indices, Vertices);
    size_t NumIndices = Indices.size();
    size_t NumVertices = Vertices.size();


    if (
        (NumIndices % 3 != 0) ||
        NumVertices < 100
    ) {
        std::cerr << "Error: Number of indices must be a multiple of 3." << std::endl;
        return data;
    }

    std::vector<unsigned int> remap(NumIndices);
    size_t OptVertexCount = meshopt_generateVertexRemap(remap.data(),    
                                                        Indices.data(),  
                                                        NumIndices,      
                                                        Vertices.data(), 
                                                        NumVertices,     
                                                        sizeof(Vector3)); 

    data.vertexCount = OptVertexCount;

    std::vector<uint32_t> OptIndices;
    std::vector<Vector3> OptVertices;
    OptIndices.resize(NumIndices);
    OptVertices.resize(OptVertexCount);

    meshopt_remapIndexBuffer(OptIndices.data(), Indices.data(), NumIndices, remap.data());

    meshopt_remapVertexBuffer(OptVertices.data(), Vertices.data(), NumVertices, sizeof(Vector3), remap.data());

    // Optimization #2: improve the locality of the vertices
    meshopt_optimizeVertexCache(OptIndices.data(), OptIndices.data(), NumIndices, OptVertexCount);

    // Optimization #3: reduce pixel overdraw
    meshopt_optimizeOverdraw(OptIndices.data(), OptIndices.data(), NumIndices, &(OptVertices[0].x), OptVertexCount, sizeof(Vector3), 1.05f);

    // Optimization #4: optimize access to the vertex buffer
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
    
    SimplifiedIndices.resize(OptIndexCount);
    
    data.Indices.clear();
    data.Vertices.clear();

    
    data.Indices.insert(data.Indices.end(), SimplifiedIndices.begin(), SimplifiedIndices.end());
    data.Vertices.insert(data.Vertices.end(), OptVertices.begin(), OptVertices.end());

    return data;
}

void calculateNormals(const std::vector<Vector3>& vertices, const std::vector<uint32_t>& indices, float* normals) {
    
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

Mesh generateLODMesh(const std::vector<Vector3>& vertices, const std::vector<uint32_t>& indices, int vertexCount32, Mesh sourceMesh) {
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

    free(lodMesh.vertices);
    free(lodMesh.indices);

    return lodMesh;
}
