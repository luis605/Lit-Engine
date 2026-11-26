module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Engine.meshlet;

namespace {

constexpr float kGrow = 1.0f + 1e-5f;
constexpr float kGrowAbsolute = 1e-6f;
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

class SphereBound {
public:
    void add(const float center[3], float radius) {
        m_spheres.push_back({{center[0], center[1], center[2]}, radius});
        for (int axis = 0; axis < 3; ++axis) {
            m_min[axis] = std::min(m_min[axis], center[axis] - radius);
            m_max[axis] = std::max(m_max[axis], center[axis] + radius);
        }
    }

    [[nodiscard]] Sphere finish() const {
        Sphere result;
        if (m_spheres.empty()) return result;
        for (int axis = 0; axis < 3; ++axis) result.center[axis] = (m_min[axis] + m_max[axis]) * 0.5f;
        for (const Sphere& sphere : m_spheres) result.radius = std::max(result.radius, distance3(result.center, sphere.center) + sphere.radius);
        result.radius = result.radius * kGrow + kGrowAbsolute;
        return result;
    }

private:
    std::vector<Sphere> m_spheres;
    float m_min[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float m_max[3] = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
};

void store(const Sphere& sphere, float center[3], float& radius) {
    for (int axis = 0; axis < 3; ++axis) center[axis] = sphere.center[axis];
    radius = sphere.radius;
}

struct Range {
    uint32_t begin = 0;
    uint32_t end = 0;
    [[nodiscard]] uint32_t size() const { return end - begin; }
};

struct HierarchyBuilder {
    ClusterMesh& mesh;
    ClusterHierarchyOptions options;
    ClusterHierarchy result;

    void boundClusters(ClusterNode& node, Range range) const {
        SphereBound cull;
        SphereBound lod;
        node.minSelfError = std::numeric_limits<float>::max();
        node.maxParentError = 0.0f;
        for (uint32_t id = range.begin; id < range.end; ++id) {
            const MeshCluster& cluster = mesh.clusters[id];
            cull.add(cluster.boundsCenter, cluster.boundsRadius);
            lod.add(cluster.selfCenter, cluster.selfRadius);
            lod.add(cluster.parentCenter, cluster.parentRadius);
            node.minSelfError = std::min(node.minSelfError, cluster.selfError);
            node.maxParentError = std::max(node.maxParentError, cluster.parentError);
        }
        store(cull.finish(), node.cullCenter, node.cullRadius);
        store(lod.finish(), node.lodCenter, node.lodRadius);
    }

    void boundChildren(ClusterNode& node, uint32_t first, uint32_t count) const {
        SphereBound cull;
        SphereBound lod;
        node.minSelfError = std::numeric_limits<float>::max();
        node.maxParentError = 0.0f;
        for (uint32_t child = first; child < first + count; ++child) {
            const ClusterNode& source = result.nodes[child];
            cull.add(source.cullCenter, source.cullRadius);
            lod.add(source.lodCenter, source.lodRadius);
            node.minSelfError = std::min(node.minSelfError, source.minSelfError);
            node.maxParentError = std::max(node.maxParentError, source.maxParentError);
        }
        store(cull.finish(), node.cullCenter, node.cullRadius);
        store(lod.finish(), node.lodCenter, node.lodRadius);
    }

    std::pair<Range, Range> split(Range range) {
        float lo[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        float hi[3] = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
        for (uint32_t id = range.begin; id < range.end; ++id) {
            for (int axis = 0; axis < 3; ++axis) {
                lo[axis] = std::min(lo[axis], mesh.clusters[id].boundsCenter[axis]);
                hi[axis] = std::max(hi[axis], mesh.clusters[id].boundsCenter[axis]);
            }
        }
        int axis = 0;
        for (int candidate = 1; candidate < 3; ++candidate) {
            if (hi[candidate] - lo[candidate] > hi[axis] - lo[axis]) axis = candidate;
        }
        const uint32_t leaves = (range.size() + options.leafSize - 1) / options.leafSize;
        const uint32_t middle = range.begin + std::min(range.size() - 1, options.leafSize * ((leaves + 1) / 2));
        auto first = mesh.clusters.begin() + range.begin;
        std::nth_element(first, mesh.clusters.begin() + middle, mesh.clusters.begin() + range.end, [axis](const MeshCluster& a, const MeshCluster& b) {
            for (int offset = 0; offset < 3; ++offset) {
                const int k = (axis + offset) % 3;
                if (a.boundsCenter[k] != b.boundsCenter[k]) return a.boundsCenter[k] < b.boundsCenter[k];
            }
            return a.triangleOffset < b.triangleOffset;
        });
        return {Range{range.begin, middle}, Range{middle, range.end}};
    }

    uint32_t fill(uint32_t index, Range range) {
        if (range.size() <= options.leafSize) {
            ClusterNode node;
            boundClusters(node, range);
            node.first = range.begin;
            node.count = range.size() | kClusterNodeLeaf;
            result.nodes[index] = node;
            ++result.leaves;
            return 1;
        }
        std::vector<Range> parts{range};
        while (parts.size() < options.fanout) {
            size_t widest = parts.size();
            for (size_t part = 0; part < parts.size(); ++part) {
                if (parts[part].size() > options.leafSize && (widest == parts.size() || parts[part].size() > parts[widest].size())) widest = part;
            }
            if (widest == parts.size()) break;
            const auto [left, right] = split(parts[widest]);
            parts[widest] = left;
            parts.insert(parts.begin() + static_cast<std::ptrdiff_t>(widest) + 1, right);
        }
        const auto first = static_cast<uint32_t>(result.nodes.size());
        const auto count = static_cast<uint32_t>(parts.size());
        result.nodes.resize(result.nodes.size() + count);
        uint32_t depth = 0;
        for (uint32_t part = 0; part < count; ++part) depth = std::max(depth, fill(first + part, parts[part]));
        ClusterNode node;
        boundChildren(node, first, count);
        node.first = first;
        node.count = count;
        result.nodes[index] = node;
        return depth + 1;
    }

    void build() {
        std::vector<Range> levels;
        auto band = [](const MeshCluster& cluster) {
            if (cluster.parentError >= kClusterRootError) return std::numeric_limits<int>::max();
            if (cluster.parentError <= 0.0f) return std::numeric_limits<int>::min();
            return static_cast<int>(std::floor(std::log2(cluster.parentError)));
        };
        for (const ClusterLevel& level : mesh.levels) {
            if (level.clusterCount == 0) continue;
            auto first = mesh.clusters.begin() + level.firstCluster;
            auto last = first + level.clusterCount;
            std::stable_sort(first, last, [&](const MeshCluster& a, const MeshCluster& b) { return band(a) < band(b); });
            const uint32_t end = level.firstCluster + level.clusterCount;
            uint32_t begin = level.firstCluster;
            for (uint32_t id = level.firstCluster + 1; id <= end; ++id) {
                if (id == end || band(mesh.clusters[id]) != band(mesh.clusters[id - 1])) {
                    levels.push_back({begin, id});
                    begin = id;
                }
            }
        }
        if (levels.empty()) return;
        result.nodes.resize(levels.size());
        uint32_t depth = 0;
        for (uint32_t level = 0; level < levels.size(); ++level) depth = std::max(depth, fill(level, levels[level]));
        uint32_t layerFirst = 0;
        auto layerCount = static_cast<uint32_t>(levels.size());
        while (layerCount > 1) {
            const auto nextFirst = static_cast<uint32_t>(result.nodes.size());
            for (uint32_t first = layerFirst; first < layerFirst + layerCount; first += options.rootFanout) {
                const uint32_t count = std::min(options.rootFanout, layerFirst + layerCount - first);
                ClusterNode node;
                boundChildren(node, first, count);
                node.first = first;
                node.count = count;
                result.nodes.push_back(node);
            }
            layerFirst = nextFirst;
            layerCount = static_cast<uint32_t>(result.nodes.size()) - nextFirst;
            ++depth;
        }
        result.root = layerFirst;
        result.depth = depth;
    }
};

bool nodeMayContainCut(const ClusterNode& node, const ClusterLodView& view, float threshold) {
    if (view.orthographic) return node.minSelfError * view.errorScale <= threshold && (node.maxParentError >= kClusterRootError || node.maxParentError * view.errorScale > threshold);
    const float distance = distance3(node.lodCenter, view.origin);
    const float farthest = std::max(distance + node.lodRadius, view.nearPlane);
    const float nearest = std::max(distance - node.lodRadius, view.nearPlane);
    if (node.minSelfError * view.errorScale / farthest > threshold) return false;
    return node.maxParentError >= kClusterRootError || node.maxParentError * view.errorScale / nearest > threshold;
}

bool sphereContains(const float outerCenter[3], float outerRadius, const float innerCenter[3], float innerRadius) {
    return distance3(outerCenter, innerCenter) + innerRadius <= outerRadius * (1.0f + kContainEpsilon) + kContainEpsilon;
}

}

float clusterProjectedError(const ClusterLodView& view, const float center[3], float radius, float error) {
    if (error >= kClusterRootError) return std::numeric_limits<float>::infinity();
    if (view.orthographic) return error * view.errorScale;
    const float distance = std::max(distance3(center, view.origin) - radius, view.nearPlane);
    return error * view.errorScale / distance;
}

bool clusterSelected(const MeshCluster& cluster, const ClusterLodView& view, float threshold) {
    const float self = clusterProjectedError(view, cluster.selfCenter, cluster.selfRadius, cluster.selfError);
    const float parent = clusterProjectedError(view, cluster.parentCenter, cluster.parentRadius, cluster.parentError);
    return self <= threshold && parent > threshold;
}

std::vector<uint32_t> selectClusterCut(const ClusterMesh& mesh, const ClusterLodView& view, float threshold) {
    std::vector<uint32_t> selected;
    for (uint32_t id = 0; id < mesh.clusters.size(); ++id) {
        if (clusterSelected(mesh.clusters[id], view, threshold)) selected.push_back(id);
    }
    return selected;
}

ClusterHierarchy buildClusterHierarchy(ClusterMesh& mesh, const ClusterHierarchyOptions& options) {
    HierarchyBuilder builder{mesh, options, {}};
    builder.options.leafSize = std::clamp<uint32_t>(options.leafSize, 1, 64);
    builder.options.fanout = std::clamp<uint32_t>(options.fanout, 2, 64);
    builder.options.rootFanout = std::clamp<uint32_t>(options.rootFanout, 2, 1024);
    if (mesh.empty()) return {};
    builder.build();
    return std::move(builder.result);
}

std::vector<uint32_t> selectClusterCutHierarchy(const ClusterMesh& mesh, const ClusterHierarchy& hierarchy, const ClusterLodView& view, float threshold, ClusterTraversalStats* stats) {
    std::vector<uint32_t> selected;
    if (hierarchy.empty()) return selected;
    std::vector<uint32_t> stack{hierarchy.root};
    while (!stack.empty()) {
        const ClusterNode& node = hierarchy.nodes[stack.back()];
        stack.pop_back();
        if (stats) ++stats->nodesVisited;
        if (!nodeMayContainCut(node, view, threshold)) continue;
        if (!node.leaf()) {
            for (uint32_t child = node.first + node.size(); child-- > node.first;) stack.push_back(child);
            continue;
        }
        if (stats) {
            ++stats->leavesAccepted;
            stats->clustersTested += node.size();
        }
        for (uint32_t id = node.first; id < node.first + node.size(); ++id) {
            if (clusterSelected(mesh.clusters[id], view, threshold)) selected.push_back(id);
        }
    }
    return selected;
}

bool validateClusterHierarchy(const ClusterMesh& mesh, const ClusterHierarchy& hierarchy, std::string* error) {
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (mesh.empty() != hierarchy.empty()) return fail("hierarchy and mesh disagree on emptiness");
    if (hierarchy.empty()) return true;
    if (hierarchy.root >= hierarchy.nodes.size()) return fail("root out of range");
    std::vector<uint32_t> covered(mesh.clusters.size(), 0);
    std::vector<uint32_t> visits(hierarchy.nodes.size(), 0);
    struct Item {
        uint32_t node;
        uint32_t depth;
    };
    std::vector<Item> stack{{hierarchy.root, 1}};
    uint32_t maxDepth = 0;
    std::vector<std::vector<uint32_t>> ancestors(hierarchy.nodes.size());
    while (!stack.empty()) {
        const Item item = stack.back();
        stack.pop_back();
        if (item.node >= hierarchy.nodes.size()) return fail("child index out of range");
        if (++visits[item.node] > 1) return fail("node " + std::to_string(item.node) + " reached twice");
        const ClusterNode& node = hierarchy.nodes[item.node];
        maxDepth = std::max(maxDepth, item.depth);
        std::vector<uint32_t> chain = ancestors[item.node];
        chain.push_back(item.node);
        if (!node.leaf()) {
            if (node.size() == 0) return fail("empty internal node " + std::to_string(item.node));
            for (uint32_t child = node.first; child < node.first + node.size(); ++child) {
                if (child >= hierarchy.nodes.size()) return fail("child range out of bounds at node " + std::to_string(item.node));
                const ClusterNode& sub = hierarchy.nodes[child];
                if (!sphereContains(node.cullCenter, node.cullRadius, sub.cullCenter, sub.cullRadius) || !sphereContains(node.lodCenter, node.lodRadius, sub.lodCenter, sub.lodRadius)) return fail("child sphere escapes node " + std::to_string(item.node));
                if (sub.minSelfError < node.minSelfError || sub.maxParentError > node.maxParentError) return fail("child error outside node bounds " + std::to_string(item.node));
                ancestors[child] = chain;
                stack.push_back({child, item.depth + 1});
            }
            continue;
        }
        if (node.size() == 0 || node.size() > 64) return fail("leaf size out of range at node " + std::to_string(item.node));
        if (static_cast<size_t>(node.first) + node.size() > mesh.clusters.size()) return fail("leaf range out of bounds at node " + std::to_string(item.node));
        const uint32_t level = mesh.clusters[node.first].level;
        for (uint32_t id = node.first; id < node.first + node.size(); ++id) {
            const MeshCluster& cluster = mesh.clusters[id];
            if (cluster.level != level) return fail("leaf mixes levels at node " + std::to_string(item.node));
            ++covered[id];
            for (const uint32_t ancestor : chain) {
                const ClusterNode& bound = hierarchy.nodes[ancestor];
                if (!sphereContains(bound.cullCenter, bound.cullRadius, cluster.boundsCenter, cluster.boundsRadius)) return fail("culling sphere escapes node " + std::to_string(ancestor));
                if (!sphereContains(bound.lodCenter, bound.lodRadius, cluster.selfCenter, cluster.selfRadius) || !sphereContains(bound.lodCenter, bound.lodRadius, cluster.parentCenter, cluster.parentRadius)) return fail("LOD sphere escapes node " + std::to_string(ancestor));
                if (cluster.selfError < bound.minSelfError || cluster.parentError > bound.maxParentError) return fail("cluster error outside node bounds " + std::to_string(ancestor));
            }
        }
    }
    for (uint32_t id = 0; id < covered.size(); ++id) {
        if (covered[id] != 1) return fail("cluster " + std::to_string(id) + " covered " + std::to_string(covered[id]) + " times");
    }
    if (maxDepth != hierarchy.depth) return fail("depth " + std::to_string(hierarchy.depth) + " but longest path " + std::to_string(maxDepth));
    return true;
}
