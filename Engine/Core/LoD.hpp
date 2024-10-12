#ifndef LOD_H
#define LOD_H

const float LOD_DISTANCE_HIGH = 10.0f;
const float LOD_DISTANCE_MEDIUM = 25.0f;
const float LOD_DISTANCE_LOW = 35.0f;

struct OptimizedMeshData {
	std::vector<unsigned int> indices;
    std::vector<Vector3> vertices;

    OptimizedMeshData() {}

    OptimizedMeshData(std::vector<unsigned int> indices, std::vector<Vector3> vertices)
        : indices(indices), vertices(vertices) {}

    OptimizedMeshData(const OptimizedMeshData& other)
        : indices(other.indices), vertices(other.vertices) {}

    OptimizedMeshData& operator=(const OptimizedMeshData& other) {
        if (this != &other) {
            indices = other.indices;
            vertices = other.vertices;
        }
        return *this;
    }
};

OptimizedMeshData OptimizeMesh(std::vector<unsigned int>& indices, std::vector<Vector3>& vertices, float threshold);
void calculateNormals(const std::vector<Vector3>& vertices, const std::vector<unsigned int>& indices, float* normals);
Mesh generateLODMesh(const std::vector<Vector3>& vertices, const std::vector<unsigned int>& indices, Mesh sourceMesh);


#endif // LOD_H