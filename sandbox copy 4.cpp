std::vector<Vector3> ContractVertices(const Mesh& mesh, float maxDistance) {
    if (mesh.vertices == nullptr || mesh.vertexCount == 0) {
        std::cout << "Mesh has no vertices or is not initialized." << std::endl;
        return std::vector<Vector3>();
    }

    std::vector<Vector3> contractedVertices(mesh.vertexCount);

    // Create a spatial hash for efficient lookup
    std::unordered_map<int, int> spatialHash;

    // Store the squared maximum distance to avoid redundant calculations
    const float maxDistanceSquared = maxDistance * maxDistance;

    #pragma omp parallel for
    for (int i = 0; i < mesh.vertexCount; i++) {
        float xi = mesh.vertices[i * 3];
        float yi = mesh.vertices[i * 3 + 1];
        float zi = mesh.vertices[i * 3 + 2];

        Vector3 vertex_position = { xi, yi, zi };

        // Find the closest existing vertex within maxDistance
        int closestVertexIndex = -1;
        float closestDistance = maxDistanceSquared;

        for (int j = 0; j < i; j++) {
            const Vector3& existingVertex = contractedVertices[j];
            float distanceSquared = Vector3DistanceSquared(vertex_position, existingVertex);

            if (distanceSquared <= closestDistance) {
                closestVertexIndex = j;
                closestDistance = distanceSquared;

                // If a vertex is found within half the maxDistance, break early
                if (distanceSquared < maxDistanceSquared * 0.25f) {
                    break;
                }
            }
        }

        // If a close vertex is found, use it; otherwise, use the original vertex
        if (closestVertexIndex != -1) {
            contractedVertices[i] = contractedVertices[closestVertexIndex];
        } else {
            contractedVertices[i] = vertex_position;
        }

        // Update the spatial hash for the current vertex
        spatialHash[i] = i;
    }

    return contractedVertices;
}





        if (closestVertexIndices[0] != -1) {
            Vector3 averagePosition = Vector3Zero();
            int numUsedSlots = 0;

            for (int index : closestVertexIndices) {
                if (index != -1) {
                    averagePosition = Vector3Add(averagePosition, contractedVertices[index]);
                    numUsedSlots++;
                }
            }

            averagePosition = Vector3Scale(averagePosition, 1.0f / numUsedSlots);
            contractedVertices.at(i) = averagePosition;
            std::cout << "contracted vertex: " << i << std::endl;
            std::cout << "Near vertices: " << closestVertexIndices[0] << ", " << closestVertexIndices[1] << ", " << closestVertexIndices[2] << std::endl << std::endl;

        } else {
            contractedVertices.at(i) = vertexPosition;
            keepVertex.at(i) = false;
        }
    }

