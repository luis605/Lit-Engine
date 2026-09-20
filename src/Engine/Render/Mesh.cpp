module;

#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meshoptimizer.h>

module Engine.mesh;

namespace {
constexpr size_t kFloatsPerVertex = 6;
constexpr size_t kVertexStride = kFloatsPerVertex * sizeof(float);
} // namespace

Mesh Mesh::optimized() const {
    if (indices.empty() || vertices.empty()) { return Mesh(vertices, indices); }

    const size_t vertexCount = vertices.size() / kFloatsPerVertex;
    const size_t indexCount = indices.size();

    std::vector<unsigned int> remap(vertexCount);
    const size_t uniqueVertices = meshopt_generateVertexRemap(remap.data(), indices.data(), indexCount, vertices.data(), vertexCount, kVertexStride);

    std::vector<unsigned int> newIndices(indexCount);
    meshopt_remapIndexBuffer(newIndices.data(), indices.data(), indexCount, remap.data());

    std::vector<float> newVertices(uniqueVertices * kFloatsPerVertex);
    meshopt_remapVertexBuffer(newVertices.data(), vertices.data(), vertexCount, kVertexStride, remap.data());

    meshopt_optimizeVertexCache(newIndices.data(), newIndices.data(), indexCount, uniqueVertices);
    meshopt_optimizeVertexFetch(newVertices.data(), newIndices.data(), indexCount, newVertices.data(), uniqueVertices, kVertexStride);

    return Mesh(std::move(newVertices), std::move(newIndices));
}

Mesh Mesh::simplify(float targetRatio, float targetError) const {
    if (indices.empty() || vertices.empty() || targetRatio >= 1.0f) {
        return Mesh(vertices, indices);
    }

    const size_t vertexCount = vertices.size() / kFloatsPerVertex;
    const size_t indexCount = indices.size();

    size_t targetIndexCount = static_cast<size_t>(static_cast<double>(indexCount) *
                                                  static_cast<double>(targetRatio));
    targetIndexCount -= targetIndexCount % 3;
    targetIndexCount = std::clamp<size_t>(targetIndexCount, 3, indexCount);

    std::vector<unsigned int> weldedIndices(indexCount);
    meshopt_generateShadowIndexBuffer(
        weldedIndices.data(),
        indices.data(),
        indexCount,
        vertices.data(),
        vertexCount,
        3 * sizeof(float),
        kVertexStride
    );

    std::vector<unsigned int> simplifiedIndices(indexCount);
    float resultError = 0.0f;
    size_t simplifiedIndexCount = meshopt_simplify(
        simplifiedIndices.data(),
        weldedIndices.data(),
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
            weldedIndices.data(),
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
        return Mesh(vertices, indices);
    }

    simplifiedIndices.resize(simplifiedIndexCount);

    meshopt_optimizeVertexCache(
        simplifiedIndices.data(),
        simplifiedIndices.data(),
        simplifiedIndexCount,
        vertexCount
    );

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

std::vector<float> Mesh::normalSamples(std::size_t count) const {
    std::vector<float> samples(count * 4, 0.0f);
    const size_t triangleCount = indices.size() / 3;
    if (triangleCount == 0 || count == 0) { return samples; }

    auto position = [&](unsigned int index, size_t axis) { return vertices[index * kFloatsPerVertex + axis]; };
    auto normal = [&](unsigned int index, size_t axis) { return vertices[index * kFloatsPerVertex + 3 + axis]; };

    std::vector<double> cumulativeArea(triangleCount);
    double totalArea = 0.0;
    for (size_t t = 0; t < triangleCount; ++t) {
        const unsigned int i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        double e1[3], e2[3];
        for (size_t axis = 0; axis < 3; ++axis) {
            e1[axis] = position(i1, axis) - position(i0, axis);
            e2[axis] = position(i2, axis) - position(i0, axis);
        }
        const double cx = e1[1] * e2[2] - e1[2] * e2[1];
        const double cy = e1[2] * e2[0] - e1[0] * e2[2];
        const double cz = e1[0] * e2[1] - e1[1] * e2[0];
        totalArea += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
        cumulativeArea[t] = totalArea;
    }
    if (totalArea <= 0.0) { return samples; }

    uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
    };

    for (size_t s = 0; s < count; ++s) {
        const double pick = (static_cast<double>(s) + next()) / static_cast<double>(count) * totalArea;
        size_t t = static_cast<size_t>(std::lower_bound(cumulativeArea.begin(), cumulativeArea.end(), pick) - cumulativeArea.begin());
        t = std::min(t, triangleCount - 1);

        const double r1 = std::sqrt(next());
        const double r2 = next();
        const double w0 = 1.0 - r1, w1 = r1 * (1.0 - r2), w2 = r1 * r2;

        const unsigned int i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        double n[3];
        for (size_t axis = 0; axis < 3; ++axis) { n[axis] = w0 * normal(i0, axis) + w1 * normal(i1, axis) + w2 * normal(i2, axis); }
        const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length > 1e-8) {
            for (size_t axis = 0; axis < 3; ++axis) { samples[s * 4 + axis] = static_cast<float>(n[axis] / length); }
        }
    }
    return samples;
}

float Mesh::surfaceArea() const {
    double area = 0.0;
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        double e1[3], e2[3];
        for (size_t axis = 0; axis < 3; ++axis) {
            e1[axis] = static_cast<double>(vertices[indices[t + 1] * kFloatsPerVertex + axis]) - vertices[indices[t] * kFloatsPerVertex + axis];
            e2[axis] = static_cast<double>(vertices[indices[t + 2] * kFloatsPerVertex + axis]) - vertices[indices[t] * kFloatsPerVertex + axis];
        }
        const double cx = e1[1] * e2[2] - e1[2] * e2[1];
        const double cy = e1[2] * e2[0] - e1[0] * e2[2];
        const double cz = e1[0] * e2[1] - e1[1] * e2[0];
        area += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    return static_cast<float>(area);
}

bool Mesh::isRound() const {
    const size_t triangleCount = indices.size() / 3;
    if (triangleCount == 0) { return false; }

    auto position = [&](unsigned int index, size_t axis) { return static_cast<double>(vertices[index * kFloatsPerVertex + axis]); };

    std::vector<double> cumulativeArea(triangleCount);
    double totalArea = 0.0;
    for (size_t t = 0; t < triangleCount; ++t) {
        const unsigned int i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        double e1[3], e2[3];
        for (size_t axis = 0; axis < 3; ++axis) {
            e1[axis] = position(i1, axis) - position(i0, axis);
            e2[axis] = position(i2, axis) - position(i0, axis);
        }
        const double cx = e1[1] * e2[2] - e1[2] * e2[1];
        const double cy = e1[2] * e2[0] - e1[0] * e2[2];
        const double cz = e1[0] * e2[1] - e1[1] * e2[0];
        totalArea += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
        cumulativeArea[t] = totalArea;
    }
    if (totalArea <= 0.0) { return false; }

    constexpr size_t kSamples = 512;
    uint64_t state = 0xD1B54A32D192ED03ull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
    };

    std::vector<std::array<double, 3>> points(kSamples);
    std::array<double, 3> centroid = {0.0, 0.0, 0.0};
    for (size_t s = 0; s < kSamples; ++s) {
        const double pick = (static_cast<double>(s) + next()) / static_cast<double>(kSamples) * totalArea;
        size_t t = static_cast<size_t>(std::lower_bound(cumulativeArea.begin(), cumulativeArea.end(), pick) - cumulativeArea.begin());
        t = std::min(t, triangleCount - 1);
        const double r1 = std::sqrt(next());
        const double r2 = next();
        const double w0 = 1.0 - r1, w1 = r1 * (1.0 - r2), w2 = r1 * r2;
        const unsigned int i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        for (size_t axis = 0; axis < 3; ++axis) {
            points[s][axis] = w0 * position(i0, axis) + w1 * position(i1, axis) + w2 * position(i2, axis);
            centroid[axis] += points[s][axis] / static_cast<double>(kSamples);
        }
    }

    double meanDistance = 0.0;
    std::vector<double> distances(kSamples);
    for (size_t s = 0; s < kSamples; ++s) {
        double d2 = 0.0;
        for (size_t axis = 0; axis < 3; ++axis) { d2 += (points[s][axis] - centroid[axis]) * (points[s][axis] - centroid[axis]); }
        distances[s] = std::sqrt(d2);
        meanDistance += distances[s] / static_cast<double>(kSamples);
    }
    if (meanDistance <= 0.0) { return false; }

    double variance = 0.0;
    for (double d : distances) { variance += (d - meanDistance) * (d - meanDistance) / static_cast<double>(kSamples); }
    return std::sqrt(variance) / meanDistance < 0.03;
}

Mesh Mesh::makeImpostor() const {
    if (vertices.size() < kFloatsPerVertex) { return Mesh(vertices, indices); }

    constexpr float kInradius = 0.7796f;
    constexpr float kHalfBase = kInradius * 1.7320508f;
    const float offsets[3][2] = {{0.0f, 2.0f * kInradius}, {-kHalfBase, -kInradius}, {kHalfBase, -kInradius}};

    std::vector<float> v;
    v.reserve(3 * kFloatsPerVertex);
    for (const auto& corner : offsets) { v.insert(v.end(), {corner[0], corner[1], 0.0f, 0.0f, 0.0f, 1.0f}); }

    return Mesh(std::move(v), std::vector<unsigned int>{0, 1, 2});
}

namespace {
constexpr size_t kMinSimplifiableIndices = 36;
} // namespace

std::vector<Mesh> Mesh::generateLODChain() const {
    static constexpr std::array<float, kLodLevelCount - 2> kRatios = {0.50f, 0.25f, 0.12f, 0.05f, 0.015f};
    constexpr float kLodTargetError = 0.5f;

    std::vector<Mesh> lods;
    lods.reserve(kLodLevelCount - 1);
    for (float ratio : kRatios) {
        lods.push_back(indices.size() > kMinSimplifiableIndices ? simplify(ratio, kLodTargetError) : Mesh(vertices, indices));
    }
    lods.push_back(makeImpostor());
    return lods;
}
