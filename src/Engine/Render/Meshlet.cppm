module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

export module Engine.meshlet;

export inline constexpr uint32_t kClusterMaxVertices = 64;
export inline constexpr uint32_t kClusterMaxTriangles = 124;
export inline constexpr float kClusterRootError = std::numeric_limits<float>::max();

export struct MeshCluster {
    float boundsCenter[3] = {0.0f, 0.0f, 0.0f};
    float boundsRadius = 0.0f;
    float coneAxis[3] = {0.0f, 0.0f, 1.0f};
    float coneCutoff = 1.0f;
    float selfCenter[3] = {0.0f, 0.0f, 0.0f};
    float selfRadius = 0.0f;
    float parentCenter[3] = {0.0f, 0.0f, 0.0f};
    float parentRadius = 0.0f;
    float selfError = 0.0f;
    float parentError = kClusterRootError;
    uint32_t level = 0;
    uint32_t group = 0;
    uint32_t vertexOffset = 0;
    uint32_t triangleOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
};
static_assert(sizeof(MeshCluster) == 96, "MeshCluster must match the GPU layout");

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
    float lodExtent = 0.0f;

    [[nodiscard]] bool empty() const { return clusters.empty(); }
    [[nodiscard]] uint64_t triangleCount(uint32_t level) const;
};

export struct ClusterBuildOptions {
    uint32_t maxVertices = kClusterMaxVertices;
    uint32_t maxTriangles = kClusterMaxTriangles;
    uint32_t groupSize = 4;
    uint32_t maxLevels = 24;
    float coneWeight = 0.25f;
    float minReduction = 0.85f;
};

export ClusterMesh buildClusterMesh(std::span<const float> vertices, size_t floatsPerVertex, std::span<const uint32_t> indices, const ClusterBuildOptions& options = {});

export float clusterProjectedError(const float center[3], float radius, float error, const float camera[3], float errorScale, float nearPlane);

export bool clusterSelected(const MeshCluster& cluster, const float camera[3], float errorScale, float threshold, float nearPlane);

export std::vector<uint32_t> selectClusterCut(const ClusterMesh& mesh, const float camera[3], float errorScale, float threshold, float nearPlane = 0.01f);

export struct ClusterLodView {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float errorScale = 1.0f;
    float nearPlane = 0.01f;
    bool orthographic = false;
};

export float clusterProjectedError(const ClusterLodView& view, const float center[3], float radius, float error);
export bool clusterSelected(const MeshCluster& cluster, const ClusterLodView& view, float threshold);
export std::vector<uint32_t> selectClusterCut(const ClusterMesh& mesh, const ClusterLodView& view, float threshold);

export inline constexpr uint32_t kClusterNodeLeaf = 0x80000000u;

export struct ClusterNode {
    float cullCenter[3] = {0.0f, 0.0f, 0.0f};
    float cullRadius = 0.0f;
    float lodCenter[3] = {0.0f, 0.0f, 0.0f};
    float lodRadius = 0.0f;
    float minSelfError = 0.0f;
    float maxParentError = 0.0f;
    uint32_t first = 0;
    uint32_t count = 0;

    [[nodiscard]] bool leaf() const { return (count & kClusterNodeLeaf) != 0; }
    [[nodiscard]] uint32_t size() const { return count & ~kClusterNodeLeaf; }
};
static_assert(sizeof(ClusterNode) == 48, "ClusterNode must match the GPU layout");

export struct ClusterHierarchyOptions {
    uint32_t leafSize = 64;
    uint32_t fanout = 8;
    uint32_t rootFanout = 64;
};

export struct ClusterHierarchy {
    std::vector<ClusterNode> nodes;
    uint32_t root = 0;
    uint32_t depth = 0;
    uint32_t leaves = 0;

    [[nodiscard]] bool empty() const { return nodes.empty(); }
};

export ClusterHierarchy buildClusterHierarchy(ClusterMesh& mesh, const ClusterHierarchyOptions& options = {});

export struct ClusterTraversalStats {
    uint64_t nodesVisited = 0;
    uint64_t leavesAccepted = 0;
    uint64_t clustersTested = 0;
};

export std::vector<uint32_t> selectClusterCutHierarchy(const ClusterMesh& mesh, const ClusterHierarchy& hierarchy, const ClusterLodView& view, float threshold, ClusterTraversalStats* stats = nullptr);

export bool validateClusterHierarchy(const ClusterMesh& mesh, const ClusterHierarchy& hierarchy, std::string* error = nullptr);

export bool validateClusterMesh(const ClusterMesh& mesh, size_t vertexCount, std::string* error = nullptr);

export struct ClusterViewStats {
    uint64_t instances = 0;
    uint64_t nodesTested = 0;
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
