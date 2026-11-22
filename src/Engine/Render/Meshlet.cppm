module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

// Cluster LOD data: meshlets (<= 64 vertices, <= 124 triangles) plus a hierarchical LOD DAG built
// at import time. Each simplification step merges a group of adjacent clusters, simplifies the
// group with its outer border locked (so neighbouring groups at different detail still share the
// exact same border vertices: no cracks) and splits the result into new clusters.
//
// Every cluster stores two LOD spheres with errors: `self` (the error of the simplification that
// produced it, 0 at full detail) and `parent` (the error of the simplification that consumed it,
// +max for roots). A cluster is drawn when its projected self error is within the threshold and the
// projected parent error is not. Errors are monotonic (parent >= self) and parent spheres contain the
// self spheres of the clusters they replace, so the decision is consistent inside a group and every
// view selects exactly one closed cut through the DAG. The GPU does the same test per cluster in
// resources/shaders/meshlet_cull.comp.
export module Engine.meshlet;

export inline constexpr uint32_t kClusterMaxVertices = 64;
export inline constexpr uint32_t kClusterMaxTriangles = 124;
export inline constexpr float kClusterRootError = std::numeric_limits<float>::max();

// 96 bytes, mirrors `Cluster` in resources/shaders/include/meshlet.glsl (std430).
export struct MeshCluster {
    float boundsCenter[3] = {0.0f, 0.0f, 0.0f};
    float boundsRadius = 0.0f;
    float coneAxis[3] = {0.0f, 0.0f, 1.0f};
    float coneCutoff = 1.0f;  // >= 1 disables backface cone culling
    float selfCenter[3] = {0.0f, 0.0f, 0.0f};
    float selfRadius = 0.0f;
    float parentCenter[3] = {0.0f, 0.0f, 0.0f};
    float parentRadius = 0.0f;
    float selfError = 0.0f;
    float parentError = kClusterRootError;
    uint32_t level = 0;
    uint32_t group = 0;
    uint32_t vertexOffset = 0;    // into ClusterMesh::vertices (mesh-local vertex indices)
    uint32_t triangleOffset = 0;  // into ClusterMesh::triangles (3 x 8-bit local indices per entry)
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
};
static_assert(sizeof(MeshCluster) == 96, "MeshCluster must match the GPU layout");

// Clusters are stored in creation order: level 0 (full detail) first, then every simplification
// pass. The GPU uses the per-level error bounds to skip levels that cannot be selected for an
// instance at its distance range.
export struct ClusterLevel {
    uint32_t firstCluster = 0;
    uint32_t clusterCount = 0;
    float minSelfError = 0.0f;
    float maxParentError = 0.0f;
};

export struct ClusterMesh {
    std::vector<MeshCluster> clusters;
    std::vector<uint32_t> vertices;
    std::vector<uint32_t> triangles;
    std::vector<ClusterLevel> levels;
    float lodCenter[3] = {0.0f, 0.0f, 0.0f};
    float lodExtent = 0.0f;  // max over clusters of |sphere centre - lodCenter| + radius (self and finite parent spheres)

    [[nodiscard]] bool empty() const { return clusters.empty(); }
    [[nodiscard]] uint64_t triangleCount(uint32_t level) const;
};

export struct ClusterBuildOptions {
    uint32_t maxVertices = kClusterMaxVertices;
    uint32_t maxTriangles = kClusterMaxTriangles;
    uint32_t groupSize = 4;
    uint32_t maxLevels = 24;
    float coneWeight = 0.25f;
    float minReduction = 0.85f;  // a group simplification that keeps more than this fraction is rejected
};

// Builds clusters and the LOD DAG. `vertices` holds `floatsPerVertex` floats per vertex with the
// position in the first three. Returns an empty mesh for empty input.
export ClusterMesh buildClusterMesh(std::span<const float> vertices, size_t floatsPerVertex, std::span<const uint32_t> indices, const ClusterBuildOptions& options = {});

// Projected error in pixels of an LOD sphere seen from `camera` (all in mesh space). `errorScale`
// is pixels per unit at distance 1 (projection[1][1] * viewportHeight / 2).
export float clusterProjectedError(const float center[3], float radius, float error, const float camera[3], float errorScale, float nearPlane);

export bool clusterSelected(const MeshCluster& cluster, const float camera[3], float errorScale, float threshold, float nearPlane);

// CPU reference of the GPU LOD cut: indices of the selected clusters.
export std::vector<uint32_t> selectClusterCut(const ClusterMesh& mesh, const float camera[3], float errorScale, float threshold, float nearPlane = 0.01f);

// Structural checks: ranges, local indices, error monotonicity and sphere containment.
export bool validateClusterMesh(const ClusterMesh& mesh, size_t vertexCount, std::string* error = nullptr);

// Per frame counters read back from the GPU (renderer), summed over camera views or shadow views.
export struct ClusterViewStats {
    uint64_t instances = 0;
    uint64_t workItems = 0;
    uint64_t clustersTested = 0;
    uint64_t clustersSelected = 0;
    uint64_t clustersDrawn = 0;
    uint64_t trianglesDrawn = 0;
    uint64_t draws = 0;
    uint64_t overflows = 0;
};

export struct ClusterRenderStats {
    bool enabled = false;
    uint32_t clusterMeshes = 0;
    uint64_t residentClusters = 0;
    uint64_t residentTriangles = 0;
    uint64_t frame = 0;
    ClusterViewStats camera;
    ClusterViewStats shadow;
};
