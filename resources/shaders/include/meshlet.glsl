#ifndef LIT_MESHLET_GLSL
#define LIT_MESHLET_GLSL

const uint MESHLET_GROUP_SIZE = 64u;
const uint MESHLET_FLAG_FRUSTUM = 1u;
const uint MESHLET_FLAG_HIZ = 2u;
const uint MESHLET_FLAG_CONE = 4u;
const uint MESHLET_FLAG_RETEST_OUTPUT = 8u;
const uint MESHLET_FLAG_VIEW_LOD = 16u;
const uint MESHLET_FLAG_HIERARCHY = 32u;
const uint MESHLET_FLAG_CASTER_CULL = 64u;
const float MESHLET_CASTER_UNIFORM_SCALE = 0.999;
const int MESHLET_CASTER_PLANES = 12;
const uint MESHLET_STAT_INSTANCES = 0u;
const uint MESHLET_STAT_WORK_ITEMS = 1u;
const uint MESHLET_STAT_TESTED = 2u;
const uint MESHLET_STAT_SELECTED = 3u;
const uint MESHLET_STAT_DRAWN = 4u;
const uint MESHLET_STAT_TRIANGLES = 5u;
const uint MESHLET_STAT_DRAWS = 6u;
const uint MESHLET_STAT_OVERFLOW = 7u;
const uint MESHLET_STAT_NODES = 8u;
const uint MESHLET_MAX_DISPATCH_X = 65535u;
const uint MESHLET_MAX_LOD_VIEWS = 24u;
const uint MESHLET_NODE_LEAF = 0x80000000u;
const uint MESHLET_RETEST_NODE = 1u;

struct Cluster {
    vec4 bounds;
    vec4 cone;
    vec4 selfSphere;
    vec4 parentSphere;
    vec4 errors;
    uvec4 ranges;
};

struct ClusterMeshInfo {
    vec4 lodCenter;
    uvec4 info;
};

struct ClusterNode {
    vec4 cullSphere;
    vec4 lodSphere;
    float minSelfError;
    float maxParentError;
    uint first;
    uint count;
};

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
    uint u_queueCapacity;
    uint u_orthoMask;
    uint u_casterExemptMask;
    uint u_lodPad;
    vec4 u_lodViews[MESHLET_MAX_LOD_VIEWS];
    vec4 u_casterPlanes[MESHLET_CASTER_PLANES];
    vec4 u_casterDirection;
};

bool meshletCasterCulled(uint shader) {
    return (u_flags & MESHLET_FLAG_CASTER_CULL) != 0u && ((u_casterExemptMask >> shader) & 1u) == 0u;
}

bool meshletCasterInside(vec3 center, float radius) {
    for (int i = 0; i < MESHLET_CASTER_PLANES; ++i) {
        if (dot(u_casterPlanes[i].xyz, center) + u_casterPlanes[i].w < -radius) return false;
    }
    return true;
}

float meshletErrorScale() { return projection[1][1] * screenParams.y * 0.5; }

float meshletProjectedError(vec3 center, float radius, float error, float scale) {
    float distance = max(length(center - viewPos) - radius, nearPlane);
    return error * scale / distance;
}

struct MeshletLod {
    vec3 origin;
    float scale;
    bool orthographic;
};

MeshletLod meshletLod(uint view, float objectScale) {
    MeshletLod lod;
    if ((u_flags & MESHLET_FLAG_VIEW_LOD) != 0u) {
        uint index = min(view, MESHLET_MAX_LOD_VIEWS - 1u);
        vec4 v = u_lodViews[index];
        lod.origin = v.xyz;
        lod.scale = v.w * objectScale;
        lod.orthographic = ((u_orthoMask >> index) & 1u) != 0u;
    } else {
        lod.origin = viewPos;
        lod.scale = meshletErrorScale() * objectScale;
        lod.orthographic = false;
    }
    return lod;
}

float meshletLodError(MeshletLod lod, vec3 center, float radius, float error) {
    if (lod.orthographic) return error * lod.scale;
    return error * lod.scale / max(length(center - lod.origin) - radius, nearPlane);
}

bool meshletNodeInCut(MeshletLod lod, vec3 center, float radius, float minSelfError, float maxParentError) {
    if (lod.orthographic) return minSelfError * lod.scale <= u_errorThreshold && maxParentError * lod.scale > u_errorThreshold;
    float distance = length(center - lod.origin);
    float nearest = max(distance - radius, nearPlane);
    float farthest = max(distance + radius, nearPlane);
    return minSelfError * lod.scale / farthest <= u_errorThreshold && maxParentError * lod.scale / nearest > u_errorThreshold;
}

#endif
