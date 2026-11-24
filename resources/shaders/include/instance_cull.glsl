#ifndef LIT_INSTANCE_CULL_GLSL
#define LIT_INSTANCE_CULL_GLSL

// Shared by instance_cells.comp (one thread per pool cell) and instance_cull.comp (one workgroup per accepted cell, one
// thread per instance). Modes (one per pipeline): INSTANCE_EARLY (camera, previous Hi-Z, instances visible last frame),
// INSTANCE_RETEST (camera, current Hi-Z, the others), INSTANCE_CASCADE (one sun cascade), INSTANCE_LOCAL (a batch of
// local shadow map views). Camera modes read the view from SceneData, so portal views cull with their own camera,
// clipped frustum and Hi-Z history.

#include "renderable.glsl"
#include "scene.glsl"
#include "mesh.glsl"
#include "instancing.glsl"

#if defined(INSTANCE_EARLY) || defined(INSTANCE_RETEST)
#define INSTANCE_CAMERA
#include "hiz.glsl"
#endif

const uint ICULL_OCCLUSION = 1u;
const uint ICULL_CELLS = 2u;
const uint ICULL_STATS = 4u;
const uint ICULL_FORCED_NONE = 0xFFFFFFFFu;
const float SHADOW_LOD_SCALE = 0.5;

// Stats words per group (base u_stats.x): cells tested, cells accepted, instances tested, visible, occluded, overflow, per-LOD visible (7).
const uint ISTAT_CELLS = 0u;
const uint ISTAT_CELLS_ACCEPTED = 1u;
const uint ISTAT_TESTED = 2u;
const uint ISTAT_VISIBLE = 3u;
const uint ISTAT_OCCLUDED = 4u;
const uint ISTAT_OVERFLOW = 5u;
const uint ISTAT_LOD = 6u;

layout(std140) uniform InstanceCullConstants {
    uvec4 u_counts;  // x pool cells, y visible entry capacity, z opaque shader count, w bucket slots
    uvec4 u_config;  // x first visibility word of the pool, y forced LOD (ICULL_FORCED_NONE: none), z view buckets, w ICULL_* flags
    vec4 u_params;   // x LOD bias, y cascade texels per unit, z distance scale, w cell list capacity
    vec4 u_planes[6];
    vec4 u_localSpheres[8];
    vec4 u_localCones[8];
    uvec4 u_localSlots[8];
    uvec4 u_localInfo;  // x local casters, y first view, z view count
    vec4 u_localOrigins[24];
    uvec4 u_stats;      // x stats word base
};

layout(std430) buffer InstanceStatsBuffer {
    uint instanceStats[];
};

struct InstanceOwnerRows {
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

layout(std430) readonly buffer WorldMatrixBuffer {
    InstanceOwnerRows worldRows[];
};

mat4 ownerWorld(uint entity) {
    InstanceOwnerRows w = worldRows[entity];
    return mat4(vec4(w.r0.x, w.r1.x, w.r2.x, 0.0), vec4(w.r0.y, w.r1.y, w.r2.y, 0.0), vec4(w.r0.z, w.r1.z, w.r2.z, 0.0), vec4(w.r0.w, w.r1.w, w.r2.w, 1.0));
}

bool insidePlanes(vec3 center, float radius) {
#ifdef INSTANCE_CASCADE
    for (int i = 0; i < 6; ++i) {
        if (dot(u_planes[i].xyz, center) + u_planes[i].w < -radius) return false;
    }
#else
    for (int i = 0; i < 6; ++i) {
        if (dot(frustumPlanes[i].xyz, center) + frustumPlanes[i].w < -radius) return false;
    }
#endif
    return true;
}

// Local shadow faces touched by a sphere (as localMaskFor in cull.comp).
uint localMaskFor(vec3 center, float radius) {
    uint mask = 0u;
    for (uint i = 0u; i < u_localInfo.x; ++i) {
        vec4 sphere = u_localSpheres[i];
        vec3 d = center - sphere.xyz;
        float dist2 = dot(d, d);
        float reach = sphere.w + radius;
        if (dist2 > reach * reach) continue;
        uvec4 slot = u_localSlots[i];
        vec4 cone = u_localCones[i];
        if (slot.z != 0u) {
            float along = dot(d, cone.xyz);
            float across = sqrt(max(dist2 - along * along, 0.0));
            float sinA = sqrt(max(1.0 - cone.w * cone.w, 0.0));
            if (along < -radius || cone.w * across - sinA * along > radius) continue;
            mask |= (slot.y & 1u) << slot.x;
        } else {
            float t = cone.w;
            float k = inversesqrt(1.0 + t * t);
            vec3 a = abs(d);
            uint faces = 0u;
            if ((t * d.x - a.y) * k >= -radius && (t * d.x - a.z) * k >= -radius) faces |= 1u;
            if ((-t * d.x - a.y) * k >= -radius && (-t * d.x - a.z) * k >= -radius) faces |= 2u;
            if ((t * d.y - a.x) * k >= -radius && (t * d.y - a.z) * k >= -radius) faces |= 4u;
            if ((-t * d.y - a.x) * k >= -radius && (-t * d.y - a.z) * k >= -radius) faces |= 8u;
            if ((t * d.z - a.x) * k >= -radius && (t * d.z - a.y) * k >= -radius) faces |= 16u;
            if ((-t * d.z - a.x) * k >= -radius && (-t * d.z - a.y) * k >= -radius) faces |= 32u;
            mask |= (faces & slot.y) << slot.x;
        }
    }
    uint views = u_localInfo.z >= 32u ? 0xFFFFFFFFu : ((1u << u_localInfo.z) - 1u);
    return (mask >> u_localInfo.y) & views;
}

bool batchUsable(InstanceBatchGpu batch) {
    uint need = INSTANCE_BATCH_ACTIVE | INSTANCE_BATCH_VISIBLE;
#if defined(INSTANCE_CASCADE) || defined(INSTANCE_LOCAL)
    need |= INSTANCE_BATCH_SHADOWS;
#endif
    return (batch.info.w & need) == need;
}

float drawDistanceOf(InstanceBatchGpu batch) { return batch.lod.z * u_params.z; }

void addStat(uint word, uint value) {
    if ((u_config.w & ICULL_STATS) != 0u && value != 0u) atomicAdd(instanceStats[u_stats.x + word], value);
}

#endif
