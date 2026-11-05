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
constexpr size_t kFloatsPerVertex = kVertexFloats;
constexpr size_t kVertexStride = kFloatsPerVertex * sizeof(float);
constexpr size_t kPositionByteSize = 3 * sizeof(float);
constexpr size_t kMinSimplifiableIndices = 36;
constexpr float kLodTargetError = 0.5f;
constexpr float kImpostorInradius = 0.7796f;
constexpr float kSqrtThree = 1.7320508f;
}

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
        kPositionByteSize,
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

namespace {
using Vec3d = std::array<double, 3>;

struct AreaTable {
    std::vector<double> cumulative;
    double total = 0.0;
};

struct SurfacePoint {
    size_t triangle;
    double weight0;
    double weight1;
    double weight2;
};

class XorShiftSampler {
  public:
    explicit XorShiftSampler(uint64_t seed) : m_state(seed) {}

    double next() {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 7;
        m_state ^= m_state << 17;
        return static_cast<double>(m_state >> 11) / static_cast<double>(1ull << 53);
    }

  private:
    uint64_t m_state;
};

double triangleArea(const std::vector<float>& vertices, const std::vector<unsigned int>& indices, size_t firstIndex) {
    const size_t base = static_cast<size_t>(indices[firstIndex]) * kFloatsPerVertex;
    const size_t second = static_cast<size_t>(indices[firstIndex + 1]) * kFloatsPerVertex;
    const size_t third = static_cast<size_t>(indices[firstIndex + 2]) * kFloatsPerVertex;
    Vec3d edge1{};
    Vec3d edge2{};
    for (size_t axis = 0; axis < 3; ++axis) {
        edge1[axis] = static_cast<double>(vertices[second + axis]) - vertices[base + axis];
        edge2[axis] = static_cast<double>(vertices[third + axis]) - vertices[base + axis];
    }
    const double crossX = edge1[1] * edge2[2] - edge1[2] * edge2[1];
    const double crossY = edge1[2] * edge2[0] - edge1[0] * edge2[2];
    const double crossZ = edge1[0] * edge2[1] - edge1[1] * edge2[0];
    return 0.5 * std::sqrt(crossX * crossX + crossY * crossY + crossZ * crossZ);
}

AreaTable buildAreaTable(const std::vector<float>& vertices, const std::vector<unsigned int>& indices) {
    const size_t triangleCount = indices.size() / 3;
    AreaTable table;
    table.cumulative.resize(triangleCount);
    for (size_t triangle = 0; triangle < triangleCount; ++triangle) {
        table.total += triangleArea(vertices, indices, triangle * 3);
        table.cumulative[triangle] = table.total;
    }
    return table;
}

SurfacePoint pickSurfacePoint(const AreaTable& table, size_t sampleIndex, size_t sampleCount, XorShiftSampler& sampler) {
    const double pick = (static_cast<double>(sampleIndex) + sampler.next()) / static_cast<double>(sampleCount) * table.total;
    const size_t found = static_cast<size_t>(std::lower_bound(table.cumulative.begin(), table.cumulative.end(), pick) - table.cumulative.begin());
    const double radius = std::sqrt(sampler.next());
    const double angle = sampler.next();
    return {std::min(found, table.cumulative.size() - 1), 1.0 - radius, radius * (1.0 - angle), radius * angle};
}

Vec3d blendAttribute(const std::vector<float>& vertices, const std::vector<unsigned int>& indices, const SurfacePoint& point, size_t attributeOffset) {
    const size_t first = point.triangle * 3;
    const size_t base0 = static_cast<size_t>(indices[first]) * kFloatsPerVertex + attributeOffset;
    const size_t base1 = static_cast<size_t>(indices[first + 1]) * kFloatsPerVertex + attributeOffset;
    const size_t base2 = static_cast<size_t>(indices[first + 2]) * kFloatsPerVertex + attributeOffset;
    Vec3d blended{};
    for (size_t axis = 0; axis < 3; ++axis) {
        blended[axis] = point.weight0 * vertices[base0 + axis] + point.weight1 * vertices[base1 + axis] + point.weight2 * vertices[base2 + axis];
    }
    return blended;
}
}

std::vector<float> Mesh::normalSamples(std::size_t count) const {
    constexpr uint64_t kSeed = 0x9E3779B97F4A7C15ull;
    constexpr size_t kNormalOffset = 3;
    constexpr double kMinNormalLength = 1e-8;

    std::vector<float> samples(count * 4, 0.0f);
    if (indices.size() < 3 || count == 0) { return samples; }

    const AreaTable table = buildAreaTable(vertices, indices);
    if (table.total <= 0.0) { return samples; }

    XorShiftSampler sampler(kSeed);
    for (size_t sampleIndex = 0; sampleIndex < count; ++sampleIndex) {
        const SurfacePoint point = pickSurfacePoint(table, sampleIndex, count, sampler);
        const Vec3d normal = blendAttribute(vertices, indices, point, kNormalOffset);
        const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length <= kMinNormalLength) continue;
        for (size_t axis = 0; axis < 3; ++axis) { samples[sampleIndex * 4 + axis] = static_cast<float>(normal[axis] / length); }
    }
    return samples;
}

float Mesh::surfaceArea() const {
    double area = 0.0;
    for (size_t firstIndex = 0; firstIndex + 2 < indices.size(); firstIndex += 3) { area += triangleArea(vertices, indices, firstIndex); }
    return static_cast<float>(area);
}

bool Mesh::isRound() const {
    constexpr size_t kSampleCount = 512;
    constexpr uint64_t kSeed = 0xD1B54A32D192ED03ull;
    constexpr double kMaxRelativeDeviation = 0.03;

    if (indices.size() < 3) { return false; }
    const AreaTable table = buildAreaTable(vertices, indices);
    if (table.total <= 0.0) { return false; }

    XorShiftSampler sampler(kSeed);
    std::vector<Vec3d> points(kSampleCount);
    Vec3d centroid{};
    for (size_t sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex) {
        points[sampleIndex] = blendAttribute(vertices, indices, pickSurfacePoint(table, sampleIndex, kSampleCount, sampler), 0);
        for (size_t axis = 0; axis < 3; ++axis) { centroid[axis] += points[sampleIndex][axis] / static_cast<double>(kSampleCount); }
    }

    double meanDistance = 0.0;
    std::vector<double> distances(kSampleCount);
    for (size_t sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex) {
        double squaredDistance = 0.0;
        for (size_t axis = 0; axis < 3; ++axis) {
            const double offset = points[sampleIndex][axis] - centroid[axis];
            squaredDistance += offset * offset;
        }
        distances[sampleIndex] = std::sqrt(squaredDistance);
        meanDistance += distances[sampleIndex] / static_cast<double>(kSampleCount);
    }
    if (meanDistance <= 0.0) { return false; }

    double variance = 0.0;
    for (double distance : distances) { variance += (distance - meanDistance) * (distance - meanDistance) / static_cast<double>(kSampleCount); }
    return std::sqrt(variance) / meanDistance < kMaxRelativeDeviation;
}

Mesh Mesh::makeImpostor() const {
    if (vertices.size() < kFloatsPerVertex) { return Mesh(vertices, indices); }

    constexpr float kHalfBase = kImpostorInradius * kSqrtThree;
    const float offsets[3][2] = {{0.0f, 2.0f * kImpostorInradius}, {-kHalfBase, -kImpostorInradius}, {kHalfBase, -kImpostorInradius}};

    std::vector<float> impostorVertices;
    impostorVertices.reserve(3 * kFloatsPerVertex);
    for (const auto& corner : offsets) { impostorVertices.insert(impostorVertices.end(), {corner[0], corner[1], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}); }

    return Mesh(std::move(impostorVertices), std::vector<unsigned int>{0, 1, 2});
}

std::vector<Mesh> Mesh::generateLODChain() const {
    static constexpr std::array<float, kLodLevelCount - 2> kRatios = {0.50f, 0.25f, 0.12f, 0.05f, 0.015f};

    std::vector<Mesh> lods;
    lods.reserve(kLodLevelCount - 1);
    for (float ratio : kRatios) {
        lods.push_back(indices.size() > kMinSimplifiableIndices ? simplify(ratio, kLodTargetError) : Mesh(vertices, indices));
    }
    lods.push_back(makeImpostor());
    return lods;
}
