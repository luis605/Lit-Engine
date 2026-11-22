#include "TestSupport.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

import Engine.meshlet;
import Engine.asset;
import Engine.mesh;

// Headless tests of the cluster LOD DAG builder (Engine.meshlet) and its .litmesh cache section.

namespace {

constexpr size_t kFloats = 8;

struct Soup {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};

// Closed, welded icosphere with low-frequency displacement (so simplification has real error).
Soup makeBumpySphere(int subdivisions) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<std::array<float, 3>> points = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<uint32_t> faces = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (auto& p : points) {
        const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (float& c : p) c /= length;
    }
    for (int level = 0; level < subdivisions; ++level) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            const auto key = std::minmax(a, b);
            if (auto it = midpoints.find(key); it != midpoints.end()) return it->second;
            std::array<float, 3> m{};
            for (int axis = 0; axis < 3; ++axis) m[axis] = points[a][axis] + points[b][axis];
            const float length = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            for (float& c : m) c /= length;
            points.push_back(m);
            return midpoints[key] = static_cast<uint32_t>(points.size() - 1);
        };
        std::vector<uint32_t> next;
        for (size_t f = 0; f < faces.size(); f += 3) {
            const uint32_t a = faces[f], b = faces[f + 1], c = faces[f + 2];
            const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        faces = std::move(next);
    }
    Soup soup;
    for (const auto& p : points) {
        const float bump = 1.0f + 0.08f * std::sin(5.0f * p[0]) * std::cos(4.0f * p[1]) + 0.03f * std::sin(17.0f * p[2]);
        soup.vertices.insert(soup.vertices.end(), {p[0] * bump, p[1] * bump, p[2] * bump, p[0], p[1], p[2], 0.5f + p[0] * 0.5f, 0.5f + p[1] * 0.5f});
    }
    soup.indices = std::move(faces);
    return soup;
}

const float* position(const Soup& soup, uint32_t vertex) { return &soup.vertices[vertex * kFloats]; }

std::array<uint32_t, 3> triangle(const ClusterMesh& mesh, const MeshCluster& cluster, uint32_t index) {
    const uint32_t packed = mesh.triangles[cluster.triangleOffset + index];
    return {mesh.vertices[cluster.vertexOffset + (packed & 0xFFu)], mesh.vertices[cluster.vertexOffset + ((packed >> 8) & 0xFFu)], mesh.vertices[cluster.vertexOffset + ((packed >> 16) & 0xFFu)]};
}

float distance(const float* a, const float* b) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }

// Counts how many selected triangles use each undirected edge. A closed input must give exactly 2
// for every edge in every cut; 1 means a crack (T-junction or hole) between clusters of different LOD.
size_t openEdges(const ClusterMesh& mesh, const std::vector<uint32_t>& cut) {
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const uint32_t id : cut) {
        const MeshCluster& cluster = mesh.clusters[id];
        for (uint32_t t = 0; t < cluster.triangleCount; ++t) {
            const auto tri = triangle(mesh, cluster, t);
            for (int e = 0; e < 3; ++e) ++edges[std::minmax(tri[e], tri[(e + 1) % 3])];
        }
    }
    // Count 1 is a crack. Even counts above 2 are pinched (non-manifold) edges that only the final
    // whole-mesh simplification can create; they leave no gap, so they are not cracks.
    size_t open = 0;
    for (const auto& [edge, count] : edges) open += count % 2 != 0 ? 1 : 0;
    return open;
}

uint64_t cutTriangles(const ClusterMesh& mesh, const std::vector<uint32_t>& cut) {
    uint64_t total = 0;
    for (const uint32_t id : cut) total += mesh.clusters[id].triangleCount;
    return total;
}

void testStructure(const Soup& soup, const ClusterMesh& mesh) {
    std::string error;
    CHECK(validateClusterMesh(mesh, soup.vertices.size() / kFloats, &error));
    if (!error.empty()) std::fprintf(stderr, "validate: %s\n", error.c_str());
    CHECK(mesh.levels.size() >= 4);
    CHECK(mesh.triangleCount(0) == soup.indices.size() / 3);
    const auto top = static_cast<uint32_t>(mesh.levels.size() - 1);
    CHECK(mesh.triangleCount(top) * 8 < mesh.triangleCount(0));
    for (const MeshCluster& cluster : mesh.clusters) {
        CHECK(cluster.vertexCount <= kClusterMaxVertices);
        CHECK(cluster.triangleCount <= kClusterMaxTriangles);
    }
    // Errors grow monotonically along every parent link (also checked by validateClusterMesh).
    bool monotonic = true;
    for (const MeshCluster& cluster : mesh.clusters) monotonic = monotonic && cluster.parentError >= cluster.selfError;
    CHECK(monotonic);
    // Level error bounds used by the GPU level filter are consistent with the clusters.
    for (const ClusterLevel& level : mesh.levels) {
        for (uint32_t id = level.firstCluster; id < level.firstCluster + level.clusterCount; ++id) {
            CHECK(mesh.clusters[id].selfError >= level.minSelfError);
            CHECK(mesh.clusters[id].parentError <= level.maxParentError);
        }
    }
}

void testBounds(const Soup& soup, const ClusterMesh& mesh) {
    bool culling = true;
    bool lod = true;
    bool extent = true;
    for (const MeshCluster& cluster : mesh.clusters) {
        for (uint32_t v = 0; v < cluster.vertexCount; ++v) {
            const float* p = position(soup, mesh.vertices[cluster.vertexOffset + v]);
            culling = culling && distance(p, cluster.boundsCenter) <= cluster.boundsRadius * 1.0001f + 1e-5f;
            lod = lod && distance(p, cluster.selfCenter) <= cluster.selfRadius * 1.0001f + 1e-5f;
        }
        extent = extent && distance(cluster.selfCenter, mesh.lodCenter) + cluster.selfRadius <= mesh.lodExtent * 1.0001f;
        extent = extent && distance(cluster.boundsCenter, mesh.lodCenter) + cluster.boundsRadius <= mesh.lodExtent * 1.0001f;
    }
    CHECK(culling);
    CHECK(lod);
    CHECK(extent);

    // Cone test (same formula as meshlet_cull.comp) never rejects a cluster with a front face.
    const float cameras[][3] = {{0, 0, 4}, {3, 2, -1}, {-2.5f, -2, 1}, {0.2f, 5, 0.1f}, {1.3f, 0.1f, 0.2f}};
    size_t rejected = 0;
    bool conservative = true;
    for (const auto& camera : cameras) {
        for (const MeshCluster& cluster : mesh.clusters) {
            if (cluster.coneCutoff >= 1.0f) continue;
            const float to[3] = {cluster.boundsCenter[0] - camera[0], cluster.boundsCenter[1] - camera[1], cluster.boundsCenter[2] - camera[2]};
            const float len = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
            const float along = to[0] * cluster.coneAxis[0] + to[1] * cluster.coneAxis[1] + to[2] * cluster.coneAxis[2];
            if (along < cluster.coneCutoff * len + cluster.boundsRadius) continue;
            ++rejected;
            for (uint32_t t = 0; t < cluster.triangleCount; ++t) {
                const auto tri = triangle(mesh, cluster, t);
                const float *a = position(soup, tri[0]), *b = position(soup, tri[1]), *c = position(soup, tri[2]);
                const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                const float facing = n[0] * (camera[0] - a[0]) + n[1] * (camera[1] - a[1]) + n[2] * (camera[2] - a[2]);
                conservative = conservative && facing <= 1e-6f;
            }
        }
    }
    CHECK(rejected > 0);
    CHECK(conservative);
}

void testCuts(const ClusterMesh& mesh) {
    constexpr float kErrorScale = 1080.0f * 0.5f / 0.41421356f;  // 90 degree vertical fov, 1080 px
    const float thresholds[] = {0.25f, 1.0f, 4.0f};
    const float distances[] = {1.15f, 2.0f, 5.0f, 20.0f, 100.0f, 1000.0f, 1e6f};
    const float directions[][3] = {{0, 0, 1}, {0.6f, 0.8f, 0}, {-0.48f, 0.6f, -0.64f}};
    bool closed = true;
    bool shrinking = true;
    bool nonEmpty = true;
    for (const float threshold : thresholds) {
        for (const auto& direction : directions) {
            uint64_t previous = ~0ull;
            for (const float d : distances) {
                const float camera[3] = {direction[0] * d, direction[1] * d, direction[2] * d};
                const std::vector<uint32_t> cut = selectClusterCut(mesh, camera, kErrorScale, threshold);
                nonEmpty = nonEmpty && !cut.empty();
                const size_t open = openEdges(mesh, cut);
                if (open != 0) std::fprintf(stderr, "crack: threshold %.2f distance %.2f -> %zu open edges\n", threshold, d, open);
                closed = closed && open == 0;
                const uint64_t triangles = cutTriangles(mesh, cut);
                shrinking = shrinking && triangles <= previous;
                previous = triangles;
            }
        }
    }
    CHECK(nonEmpty);
    CHECK(closed);
    CHECK(shrinking);

    const float near[3] = {0.0f, 0.0f, 1.2f};
    const float far[3] = {0.0f, 0.0f, 1e6f};
    CHECK(cutTriangles(mesh, selectClusterCut(mesh, near, kErrorScale, 0.25f)) > cutTriangles(mesh, selectClusterCut(mesh, far, kErrorScale, 0.25f)) * 10);
    // At infinite distance only roots remain.
    for (const uint32_t id : selectClusterCut(mesh, far, kErrorScale, 1.0f)) CHECK(mesh.clusters[id].parentError == kClusterRootError);
}

void testCache(const Soup& soup) {
    namespace fs = std::filesystem;
    const std::string root = testsupport::tempFilePath("litmeshlet_", "cache");
    fs::remove_all(root);
    fs::create_directories(root);
    Mesh source(std::vector<float>(soup.vertices.begin(), soup.vertices.end()), std::vector<unsigned int>(soup.indices.begin(), soup.indices.end()));
    const ProcessedMesh processed = AssetManager::processMesh(source);
    CHECK(!processed.clusters.empty());
    CHECK(kImportSettingsVersion >= 2);
    const std::string path = AssetCache::entryPath(root, "bumpy.obj", 42, kImportSettingsVersion);
    CHECK(AssetCache::store(path, 42, kImportSettingsVersion, processed));
    ProcessedMesh loaded{Mesh(std::vector<float>{}, std::vector<unsigned int>{}), {}, {}};
    CHECK(AssetCache::fetch(path, 42, kImportSettingsVersion, loaded) == CacheOutcome::Hit);
    CHECK(loaded.clusters.clusters.size() == processed.clusters.clusters.size());
    CHECK(loaded.clusters.vertices == processed.clusters.vertices);
    CHECK(loaded.clusters.triangles == processed.clusters.triangles);
    CHECK(loaded.clusters.levels.size() == processed.clusters.levels.size());
    CHECK(loaded.clusters.lodExtent == processed.clusters.lodExtent);
    CHECK(AssetCache::fetch(path, 42, kImportSettingsVersion - 1, loaded) == CacheOutcome::Invalidated);
    fs::remove_all(root);
}

}

int main() {
    const Soup soup = makeBumpySphere(6);  // 81920 triangles
    const ClusterMesh mesh = buildClusterMesh(soup.vertices, kFloats, soup.indices);
    std::printf("clusters %zu levels %zu triangles %llu -> %llu\n", mesh.clusters.size(), mesh.levels.size(), static_cast<unsigned long long>(mesh.triangleCount(0)), static_cast<unsigned long long>(mesh.triangleCount(static_cast<uint32_t>(mesh.levels.size() - 1))));
    CHECK(!mesh.empty());
    testStructure(soup, mesh);
    testBounds(soup, mesh);
    testCuts(mesh);
    testCache(makeBumpySphere(5));
    CHECK(buildClusterMesh({}, kFloats, {}).empty());
    return testsupport::finish();
}
