module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <meshoptimizer.h>

module Engine.meshlet;

namespace {

constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();
constexpr float kContainEpsilon = 1e-4f;

struct Sphere {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

float distance3(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Sphere mergeSpheres(const Sphere& a, const Sphere& b) {
    const float d = distance3(a.center, b.center);
    if (d + b.radius <= a.radius) return a;
    if (d + a.radius <= b.radius) return b;
    Sphere merged;
    merged.radius = (d + a.radius + b.radius) * 0.5f;
    const float t = d > 0.0f ? (merged.radius - a.radius) / d : 0.0f;
    for (int axis = 0; axis < 3; ++axis) merged.center[axis] = a.center[axis] + (b.center[axis] - a.center[axis]) * t;
    // Float rounding: grow slightly so both inputs are contained.
    merged.radius = std::max({merged.radius, distance3(merged.center, a.center) + a.radius, distance3(merged.center, b.center) + b.radius}) * (1.0f + 1e-6f);
    return merged;
}

struct Builder {
    std::span<const float> vertices;
    size_t stride = 0;
    size_t vertexCount = 0;
    ClusterBuildOptions options;
    ClusterMesh mesh;
    std::vector<uint32_t> weld;  // vertex -> canonical vertex with the same position

    const float* position(uint32_t vertex) const { return vertices.data() + static_cast<size_t>(vertex) * stride; }

    // Splits `indices` (mesh-local vertex ids) into clusters appended to `mesh`, returning their ids.
    std::vector<uint32_t> appendClusters(const std::vector<uint32_t>& indices, uint32_t level, const Sphere* lod, float error) {
        std::vector<uint32_t> created;
        if (indices.empty()) return created;

        // Compact to the referenced vertices so meshopt's per-vertex tables stay small for subsets.
        std::vector<uint32_t> localToMesh;
        std::unordered_map<uint32_t, uint32_t> meshToLocal;
        meshToLocal.reserve(indices.size());
        std::vector<uint32_t> localIndices(indices.size());
        for (size_t i = 0; i < indices.size(); ++i) {
            auto [it, inserted] = meshToLocal.try_emplace(indices[i], static_cast<uint32_t>(localToMesh.size()));
            if (inserted) localToMesh.push_back(indices[i]);
            localIndices[i] = it->second;
        }
        std::vector<float> positions(localToMesh.size() * 3);
        for (size_t v = 0; v < localToMesh.size(); ++v) {
            const float* p = position(localToMesh[v]);
            positions[v * 3] = p[0];
            positions[v * 3 + 1] = p[1];
            positions[v * 3 + 2] = p[2];
        }

        const size_t bound = meshopt_buildMeshletsBound(localIndices.size(), options.maxVertices, options.maxTriangles);
        std::vector<meshopt_Meshlet> meshlets(bound);
        std::vector<unsigned int> meshletVertices(bound * options.maxVertices);
        std::vector<unsigned char> meshletTriangles(bound * options.maxTriangles * 3);
        const size_t count = meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), localIndices.data(), localIndices.size(), positions.data(), localToMesh.size(), sizeof(float) * 3, options.maxVertices,
                                                   options.maxTriangles, options.coneWeight);
        for (size_t m = 0; m < count; ++m) {
            const meshopt_Meshlet& meshlet = meshlets[m];
            const meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, positions.data(), localToMesh.size(), sizeof(float) * 3);
            MeshCluster cluster;
            for (int axis = 0; axis < 3; ++axis) {
                cluster.boundsCenter[axis] = bounds.center[axis];
                cluster.coneAxis[axis] = bounds.cone_axis[axis];
            }
            cluster.boundsRadius = bounds.radius;
            cluster.coneCutoff = bounds.cone_cutoff;
            cluster.level = level;
            cluster.vertexOffset = static_cast<uint32_t>(mesh.vertices.size());
            cluster.triangleOffset = static_cast<uint32_t>(mesh.triangles.size());
            cluster.vertexCount = meshlet.vertex_count;
            cluster.triangleCount = meshlet.triangle_count;
            for (uint32_t v = 0; v < meshlet.vertex_count; ++v) mesh.vertices.push_back(localToMesh[meshletVertices[meshlet.vertex_offset + v]]);
            for (uint32_t t = 0; t < meshlet.triangle_count; ++t) {
                const unsigned char* tri = &meshletTriangles[meshlet.triangle_offset + t * 3];
                mesh.triangles.push_back(static_cast<uint32_t>(tri[0]) | (static_cast<uint32_t>(tri[1]) << 8) | (static_cast<uint32_t>(tri[2]) << 16));
            }
            const Sphere self = lod ? *lod : Sphere{{bounds.center[0], bounds.center[1], bounds.center[2]}, bounds.radius};
            for (int axis = 0; axis < 3; ++axis) cluster.selfCenter[axis] = self.center[axis];
            cluster.selfRadius = self.radius;
            cluster.selfError = error;
            created.push_back(static_cast<uint32_t>(mesh.clusters.size()));
            mesh.clusters.push_back(cluster);
        }
        return created;
    }

    std::vector<uint32_t> clusterIndices(uint32_t clusterId) const {
        const MeshCluster& cluster = mesh.clusters[clusterId];
        std::vector<uint32_t> indices;
        indices.reserve(cluster.triangleCount * 3);
        for (uint32_t t = 0; t < cluster.triangleCount; ++t) {
            const uint32_t packed = mesh.triangles[cluster.triangleOffset + t];
            for (int corner = 0; corner < 3; ++corner) indices.push_back(mesh.vertices[cluster.vertexOffset + ((packed >> (corner * 8)) & 0xFFu)]);
        }
        return indices;
    }

    // Greedy grouping of adjacent clusters (shared welded vertices), seeded in creation order which
    // is spatially coherent (meshopt emits meshlets along a surface walk).
    std::vector<std::vector<uint32_t>> partition(const std::vector<uint32_t>& pending) const {
        std::vector<std::pair<uint32_t, uint32_t>> touches;  // (canonical vertex, pending slot)
        for (uint32_t slot = 0; slot < pending.size(); ++slot) {
            const MeshCluster& cluster = mesh.clusters[pending[slot]];
            for (uint32_t v = 0; v < cluster.vertexCount; ++v) touches.emplace_back(weld[mesh.vertices[cluster.vertexOffset + v]], slot);
        }
        std::sort(touches.begin(), touches.end());
        touches.erase(std::unique(touches.begin(), touches.end()), touches.end());

        std::vector<std::unordered_map<uint32_t, uint32_t>> adjacency(pending.size());
        for (size_t begin = 0; begin < touches.size();) {
            size_t end = begin + 1;
            while (end < touches.size() && touches[end].first == touches[begin].first) ++end;
            for (size_t a = begin; a < end; ++a) {
                for (size_t b = a + 1; b < end; ++b) {
                    ++adjacency[touches[a].second][touches[b].second];
                    ++adjacency[touches[b].second][touches[a].second];
                }
            }
            begin = end;
        }

        std::vector<uint32_t> groupOf(pending.size(), kInvalid);
        std::vector<std::vector<uint32_t>> groups;
        for (uint32_t seed = 0; seed < pending.size(); ++seed) {
            if (groupOf[seed] != kInvalid) continue;
            const auto groupIndex = static_cast<uint32_t>(groups.size());
            std::vector<uint32_t> members{seed};
            groupOf[seed] = groupIndex;
            std::unordered_map<uint32_t, uint32_t> frontier;
            auto absorb = [&](uint32_t member) {
                for (const auto& [neighbor, weight] : adjacency[member]) {
                    if (groupOf[neighbor] == kInvalid) frontier[neighbor] += weight;
                }
            };
            absorb(seed);
            while (members.size() < options.groupSize) {
                uint32_t best = kInvalid;
                uint32_t bestWeight = 0;
                for (const auto& [candidate, weight] : frontier) {
                    if (groupOf[candidate] != kInvalid) continue;
                    if (weight > bestWeight || (weight == bestWeight && candidate < best)) {
                        best = candidate;
                        bestWeight = weight;
                    }
                }
                if (best == kInvalid) break;
                groupOf[best] = groupIndex;
                members.push_back(best);
                frontier.erase(best);
                absorb(best);
            }
            for (uint32_t& member : members) member = pending[member];
            groups.push_back(std::move(members));
        }
        return groups;
    }

    // Simplifies one group. Returns the new clusters or nothing when the reduction is too small.
    std::vector<uint32_t> simplifyGroup(const std::vector<uint32_t>& group, uint32_t level, uint32_t groupId) {
        std::vector<uint32_t> merged;
        Sphere sphere;
        float childError = 0.0f;
        for (size_t i = 0; i < group.size(); ++i) {
            const std::vector<uint32_t> indices = clusterIndices(group[i]);
            merged.insert(merged.end(), indices.begin(), indices.end());
            const MeshCluster& child = mesh.clusters[group[i]];
            const Sphere childSphere{{child.selfCenter[0], child.selfCenter[1], child.selfCenter[2]}, child.selfRadius};
            sphere = i == 0 ? childSphere : mergeSpheres(sphere, childSphere);
            childError = std::max(childError, child.selfError);
        }

        std::vector<uint32_t> localToMesh;
        std::unordered_map<uint32_t, uint32_t> meshToLocal;
        std::vector<uint32_t> local(merged.size());
        for (size_t i = 0; i < merged.size(); ++i) {
            auto [it, inserted] = meshToLocal.try_emplace(merged[i], static_cast<uint32_t>(localToMesh.size()));
            if (inserted) localToMesh.push_back(merged[i]);
            local[i] = it->second;
        }
        std::vector<float> positions(localToMesh.size() * 3);
        for (size_t v = 0; v < localToMesh.size(); ++v) {
            const float* p = position(localToMesh[v]);
            positions[v * 3] = p[0];
            positions[v * 3 + 1] = p[1];
            positions[v * 3 + 2] = p[2];
        }

        const size_t target = std::max<size_t>(3, (merged.size() / 3 / 2) * 3);
        std::vector<uint32_t> simplified(merged.size());
        float resultError = 0.0f;
        const size_t produced = meshopt_simplify(simplified.data(), local.data(), local.size(), positions.data(), localToMesh.size(), sizeof(float) * 3, target, std::numeric_limits<float>::max(),
                                                 meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute, &resultError);
        if (produced == 0 || static_cast<float>(produced) > static_cast<float>(merged.size()) * options.minReduction) return {};
        simplified.resize(produced);
        for (uint32_t& index : simplified) index = localToMesh[index];

        const float error = std::max(resultError, childError);
        std::vector<uint32_t> created = appendClusters(simplified, level, &sphere, error);
        for (const uint32_t childId : group) {
            MeshCluster& child = mesh.clusters[childId];
            for (int axis = 0; axis < 3; ++axis) child.parentCenter[axis] = sphere.center[axis];
            child.parentRadius = sphere.radius;
            child.parentError = error;
            child.group = groupId;
        }
        for (const uint32_t id : created) mesh.clusters[id].group = groupId;
        return created;
    }

    void finish() {
        // Roots: no parent. Give them a parent sphere equal to their self sphere so GPU math stays finite.
        for (MeshCluster& cluster : mesh.clusters) {
            if (cluster.parentError != kClusterRootError) continue;
            for (int axis = 0; axis < 3; ++axis) cluster.parentCenter[axis] = cluster.selfCenter[axis];
            cluster.parentRadius = cluster.selfRadius;
        }
        double sum[3] = {0.0, 0.0, 0.0};
        for (size_t v = 0; v < vertexCount; ++v) {
            for (int axis = 0; axis < 3; ++axis) sum[axis] += position(static_cast<uint32_t>(v))[axis];
        }
        for (int axis = 0; axis < 3; ++axis) mesh.lodCenter[axis] = vertexCount ? static_cast<float>(sum[axis] / static_cast<double>(vertexCount)) : 0.0f;
        float extent = 0.0f;
        for (const MeshCluster& cluster : mesh.clusters) {
            extent = std::max(extent, distance3(cluster.selfCenter, mesh.lodCenter) + cluster.selfRadius);
            extent = std::max(extent, distance3(cluster.parentCenter, mesh.lodCenter) + cluster.parentRadius);
            extent = std::max(extent, distance3(cluster.boundsCenter, mesh.lodCenter) + cluster.boundsRadius);
        }
        mesh.lodExtent = extent;

        mesh.levels.clear();
        for (uint32_t id = 0; id < mesh.clusters.size(); ++id) {
            const MeshCluster& cluster = mesh.clusters[id];
            if (mesh.levels.empty() || mesh.levels.size() <= cluster.level) {
                mesh.levels.resize(cluster.level + 1, ClusterLevel{id, 0, std::numeric_limits<float>::max(), 0.0f});
            }
            ClusterLevel& level = mesh.levels[cluster.level];
            if (level.clusterCount == 0) level.firstCluster = id;
            ++level.clusterCount;
            level.minSelfError = std::min(level.minSelfError, cluster.selfError);
            level.maxParentError = std::max(level.maxParentError, cluster.parentError);
        }
    }
};

}

uint64_t ClusterMesh::triangleCount(uint32_t level) const {
    if (level >= levels.size()) return 0;
    uint64_t total = 0;
    for (uint32_t id = levels[level].firstCluster; id < levels[level].firstCluster + levels[level].clusterCount; ++id) total += clusters[id].triangleCount;
    return total;
}

ClusterMesh buildClusterMesh(std::span<const float> vertices, size_t floatsPerVertex, std::span<const uint32_t> indices, const ClusterBuildOptions& options) {
    Builder builder;
    builder.vertices = vertices;
    builder.stride = floatsPerVertex;
    builder.options = options;
    builder.options.maxVertices = std::clamp<uint32_t>(options.maxVertices, 3, 255);
    builder.options.maxTriangles = std::clamp<uint32_t>(options.maxTriangles, 1, 512);
    builder.options.groupSize = std::max<uint32_t>(options.groupSize, 2);
    if (floatsPerVertex < 3 || indices.size() < 3) return {};
    builder.vertexCount = vertices.size() / floatsPerVertex;
    for (const uint32_t index : indices) {
        if (index >= builder.vertexCount) return {};
    }

    // Weld by position only (attribute seams split vertices that are still adjacent for grouping).
    {
        std::vector<unsigned int> positionWeld(builder.vertexCount);
        std::vector<float> positions(builder.vertexCount * 3);
        for (size_t v = 0; v < builder.vertexCount; ++v) {
            for (int axis = 0; axis < 3; ++axis) positions[v * 3 + axis] = vertices[v * floatsPerVertex + axis];
        }
        meshopt_generateVertexRemap(positionWeld.data(), nullptr, builder.vertexCount, positions.data(), builder.vertexCount, sizeof(float) * 3);
        builder.weld.assign(positionWeld.begin(), positionWeld.end());
    }

    const std::vector<uint32_t> source(indices.begin(), indices.end() - static_cast<std::ptrdiff_t>(indices.size() % 3));
    std::vector<uint32_t> pending = builder.appendClusters(source, 0, nullptr, 0.0f);
    uint32_t groupId = 0;
    for (uint32_t level = 1; level < builder.options.maxLevels && pending.size() > 1; ++level) {
        const auto groups = builder.partition(pending);
        std::vector<uint32_t> next;
        bool progressed = false;
        for (const auto& group : groups) {
            const std::vector<uint32_t> created = builder.simplifyGroup(group, level, ++groupId);
            if (created.empty()) {
                next.insert(next.end(), group.begin(), group.end());  // retry with coarser neighbours next pass
                continue;
            }
            progressed = true;
            next.insert(next.end(), created.begin(), created.end());
        }
        if (!progressed) break;
        pending = std::move(next);
    }
    builder.finish();
    return std::move(builder.mesh);
}

float clusterProjectedError(const float center[3], float radius, float error, const float camera[3], float errorScale, float nearPlane) {
    if (error >= kClusterRootError) return std::numeric_limits<float>::infinity();
    const float distance = std::max(distance3(center, camera) - radius, nearPlane);
    return error * errorScale / distance;
}

bool clusterSelected(const MeshCluster& cluster, const float camera[3], float errorScale, float threshold, float nearPlane) {
    const float self = clusterProjectedError(cluster.selfCenter, cluster.selfRadius, cluster.selfError, camera, errorScale, nearPlane);
    const float parent = clusterProjectedError(cluster.parentCenter, cluster.parentRadius, cluster.parentError, camera, errorScale, nearPlane);
    return self <= threshold && parent > threshold;
}

std::vector<uint32_t> selectClusterCut(const ClusterMesh& mesh, const float camera[3], float errorScale, float threshold, float nearPlane) {
    std::vector<uint32_t> selected;
    for (uint32_t id = 0; id < mesh.clusters.size(); ++id) {
        if (clusterSelected(mesh.clusters[id], camera, errorScale, threshold, nearPlane)) selected.push_back(id);
    }
    return selected;
}

bool validateClusterMesh(const ClusterMesh& mesh, size_t vertexCount, std::string* error) {
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    uint32_t expectedLevel = 0;
    for (uint32_t id = 0; id < mesh.clusters.size(); ++id) {
        const MeshCluster& cluster = mesh.clusters[id];
        if (cluster.level < expectedLevel) return fail("clusters not sorted by level at " + std::to_string(id));
        expectedLevel = cluster.level;
        if (cluster.vertexCount == 0 || cluster.vertexCount > 255 || cluster.triangleCount == 0) return fail("empty or oversized cluster " + std::to_string(id));
        if (static_cast<size_t>(cluster.vertexOffset) + cluster.vertexCount > mesh.vertices.size()) return fail("vertex range out of bounds " + std::to_string(id));
        if (static_cast<size_t>(cluster.triangleOffset) + cluster.triangleCount > mesh.triangles.size()) return fail("triangle range out of bounds " + std::to_string(id));
        for (uint32_t v = 0; v < cluster.vertexCount; ++v) {
            if (mesh.vertices[cluster.vertexOffset + v] >= vertexCount) return fail("vertex index out of range in cluster " + std::to_string(id));
        }
        for (uint32_t t = 0; t < cluster.triangleCount; ++t) {
            const uint32_t packed = mesh.triangles[cluster.triangleOffset + t];
            for (int corner = 0; corner < 3; ++corner) {
                if (((packed >> (corner * 8)) & 0xFFu) >= cluster.vertexCount) return fail("local index out of range in cluster " + std::to_string(id));
            }
        }
        if (cluster.parentError < cluster.selfError) return fail("parent error below self error in cluster " + std::to_string(id));
        if (cluster.parentError != kClusterRootError) {
            const float reach = distance3(cluster.parentCenter, cluster.selfCenter) + cluster.selfRadius;
            if (reach > cluster.parentRadius * (1.0f + kContainEpsilon) + kContainEpsilon) return fail("parent sphere does not contain self sphere in cluster " + std::to_string(id));
        }
    }
    uint32_t covered = 0;
    for (const ClusterLevel& level : mesh.levels) {
        if (level.firstCluster != covered) return fail("level table has gaps");
        covered += level.clusterCount;
    }
    if (covered != mesh.clusters.size()) return fail("level table does not cover all clusters");
    return true;
}
