#ifndef LIT_MESHLET_GLSL
#define LIT_MESHLET_GLSL

// Cluster LOD shared declarations. CPU mirror: Engine.meshlet (src/Engine/Render/Meshlet.cppm) and
// src/Engine/Render/RendererMeshlets.cpp. Offsets inside Cluster and ClusterLevel are global (rebased
// at upload), so the buffers below are flat arrays over every cluster mesh.

const uint MESHLET_GROUP_SIZE = 64u;
const uint MESHLET_FLAG_FRUSTUM = 1u;
const uint MESHLET_FLAG_HIZ = 2u;
const uint MESHLET_FLAG_CONE = 4u;
const uint MESHLET_FLAG_RETEST_OUTPUT = 8u;
const uint MESHLET_STAT_INSTANCES = 0u;
const uint MESHLET_STAT_WORK_ITEMS = 1u;
const uint MESHLET_STAT_TESTED = 2u;
const uint MESHLET_STAT_SELECTED = 3u;
const uint MESHLET_STAT_DRAWN = 4u;
const uint MESHLET_STAT_TRIANGLES = 5u;
const uint MESHLET_STAT_DRAWS = 6u;
const uint MESHLET_STAT_OVERFLOW = 7u;
const uint MESHLET_MAX_DISPATCH_X = 65535u;

struct Cluster {
    vec4 bounds;        // culling sphere (mesh space): centre, radius
    vec4 cone;          // backface cone: axis, cutoff (>= 1 disables)
    vec4 selfSphere;    // LOD sphere of the simplification that produced this cluster
    vec4 parentSphere;  // LOD sphere of the simplification that consumed it
    vec4 errors;        // x self error, y parent error (FLT_MAX for roots), z/w level and group bits
    uvec4 ranges;       // vertex offset, triangle offset, vertex count, triangle count
};

struct ClusterMeshInfo {
    vec4 lodCenter;     // xyz centre, w max extent of every LOD sphere around it
    uvec4 info;         // chunk offset, chunk count, base vertex, unused
};

// Up to 64 consecutive clusters of one DAG level. The sphere encloses every culling and LOD sphere
// of its clusters, so [distance - r, distance + r] bounds each cluster's LOD distance.
struct ClusterChunk {
    vec4 sphere;
    float minSelfError;
    float maxParentError;
    uint firstCluster;
    uint clusterCount;
};

struct MeshletWorldRows {
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

mat4 meshletWorld(MeshletWorldRows w) {
    return mat4(vec4(w.r0.x, w.r1.x, w.r2.x, 0.0), vec4(w.r0.y, w.r1.y, w.r2.y, 0.0), vec4(w.r0.z, w.r1.z, w.r2.z, 0.0), vec4(w.r0.w, w.r1.w, w.r2.w, 1.0));
}

layout(std140) uniform MeshletConstants {
    uint u_maxDraws;
    uint u_views;
    uint u_viewBuckets;
    uint u_shaderCount;
    uint u_flags;
    uint u_maxWorkItems;
    uint u_indexCapacity;
    uint u_drawsPerSlot;
    float u_errorThreshold;
    uint u_twoSidedMask;
    uint u_statsBase;
    uint u_retestCapacity;
};

// Pixels of error per unit of mesh-space error at distance 1 (projection[1][1] * height / 2).
float meshletErrorScale() { return projection[1][1] * screenParams.y * 0.5; }

float meshletProjectedError(vec3 center, float radius, float error, float scale) {
    float distance = max(length(center - viewPos) - radius, nearPlane);
    return error * scale / distance;
}

#endif
