module;

#include <vector>
#include <algorithm>
#include <cstddef>
#include <meshoptimizer.h>

module Engine.mesh;

namespace {
constexpr size_t kFloatsPerVertex = 6; // position xyz + normal xyz
constexpr size_t kVertexStride = kFloatsPerVertex * sizeof(float);
} // namespace

Mesh Mesh::simplify(float targetRatio, float targetError) const {
    if (indices.empty() || vertices.empty() || targetRatio >= 1.0f) {
        return Mesh(vertices, indices);
    }

    const size_t vertexCount = vertices.size() / kFloatsPerVertex;
    const size_t indexCount = indices.size();

    // Compute in double: `indexCount * float` loses precision past ~16M indices.
    // meshopt expects a triangle-list count, so floor to a multiple of 3.
    size_t targetIndexCount = static_cast<size_t>(static_cast<double>(indexCount) *
                                                  static_cast<double>(targetRatio));
    targetIndexCount -= targetIndexCount % 3;
    targetIndexCount = std::clamp<size_t>(targetIndexCount, 3, indexCount);

    std::vector<unsigned int> simplifiedIndices(indexCount);
    float resultError = 0.0f;
    size_t simplifiedIndexCount = meshopt_simplify(
        simplifiedIndices.data(),
        indices.data(),
        indexCount,
        vertices.data(),
        vertexCount,
        kVertexStride,
        targetIndexCount,
        targetError,
        0,
        &resultError
    );

    if (simplifiedIndexCount > targetIndexCount + targetIndexCount / 2 ||
        simplifiedIndexCount == indexCount) {
        simplifiedIndexCount = meshopt_simplifySloppy(
            simplifiedIndices.data(),
            indices.data(),
            indexCount,
            vertices.data(),
            vertexCount,
            kVertexStride,
            targetIndexCount,
            targetError,
            &resultError
        );
    }

    if (simplifiedIndexCount < 3) {
        // Nothing usable survived; hand back the source mesh rather than an
        // empty draw.
        return Mesh(vertices, indices);
    }

    simplifiedIndices.resize(simplifiedIndexCount);

    // Post-simplification optimisation, in the order meshoptimizer documents:
    // vertex-cache (reorders triangles for the post-transform cache) and then
    // vertex-fetch (reorders/compacts vertices to match the new index order).
    meshopt_optimizeVertexCache(
        simplifiedIndices.data(),
        simplifiedIndices.data(),
        simplifiedIndexCount,
        vertexCount
    );

    // Unique vertices can never exceed either the source vertex count or the
    // index count.
    const size_t maxUniqueVertices = std::min(vertexCount, simplifiedIndexCount);
    std::vector<float> compactedVertices(maxUniqueVertices * kFloatsPerVertex);
    const size_t uniqueVertices = meshopt_optimizeVertexFetch(
        compactedVertices.data(),
        simplifiedIndices.data(),
        simplifiedIndexCount,
        vertices.data(),
        vertexCount,
        kVertexStride
    );
    compactedVertices.resize(uniqueVertices * kFloatsPerVertex);

    return Mesh(std::move(compactedVertices), std::move(simplifiedIndices));
}

std::vector<Mesh> Mesh::generateLODs(const std::vector<float>& targetRatios) const {
    std::vector<Mesh> lods;
    lods.reserve(targetRatios.size());
    for (float ratio : targetRatios) {
        // Each LOD is simplified from the base mesh, not from the previous LOD,
        // so simplification error does not compound down the chain.
        lods.emplace_back(simplify(ratio));
    }
    return lods;
}
