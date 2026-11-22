#ifndef LIT_LIGHTS_GLSL
#define LIT_LIGHTS_GLSL

#include "scene.glsl"

struct GpuLight {
    vec4 positionRange;
    vec4 colorType;
    vec4 directionCos;
    vec4 params;
};

layout(std430) readonly buffer LightBuffer {
    GpuLight lights[];
};

// 3D clustered light lists (light_cull.comp). Layout of ClusterBuffer, in uints:
//   [2c], [2c+1]            offset and count of cluster c's index list (offset relative to CLUSTER_INDEX_BASE)
//   [CLUSTER_ALLOC_COUNTER] index allocation cursor, [CLUSTER_VISIBLE_COUNTER] frustum-visible light count
//   [CLUSTER_INDEX_BASE..]  light indices, variable length per cluster
#ifndef CLUSTER_BUFFER_WRITABLE
layout(std430) readonly buffer ClusterBuffer {
    uint clusterLights[];
};
#endif

const uint CLUSTER_X = 32u;
const uint CLUSTER_Y = 18u;
const uint CLUSTER_Z = 32u;
const uint CLUSTER_COUNT = CLUSTER_X * CLUSTER_Y * CLUSTER_Z;
const uint CLUSTER_MAX_LIGHTS = 512u;
const uint CLUSTER_ALLOC_COUNTER = CLUSTER_COUNT * 2u;
const uint CLUSTER_VISIBLE_COUNTER = CLUSTER_COUNT * 2u + 1u;
const uint CLUSTER_INDEX_BASE = CLUSTER_COUNT * 2u + 16u;

// Light params.w: >= 0 ranked local shadow view base, <= -2 virtual shadow map face base (-2 - base), -1 unshadowed.
bool lightHasVirtualShadow(GpuLight light) { return light.params.w < -1.5; }
uint lightVirtualFace(GpuLight light) { return uint(-light.params.w - 2.0 + 0.5); }

uint clusterSlice(float viewDepth) {
    float slice = log(max(viewDepth, nearPlane) / nearPlane) * clusterParams.x;
    return min(uint(max(slice, 0.0)), CLUSTER_Z - 1u);
}

uint clusterIndex(vec2 pixel, float viewDepth) {
    uvec2 tile = min(uvec2(pixel / vec2(clusterParams.zw)), uvec2(CLUSTER_X - 1u, CLUSTER_Y - 1u));
    return (clusterSlice(viewDepth) * CLUSTER_Y + tile.y) * CLUSTER_X + tile.x;
}

#ifndef CLUSTER_BUFFER_WRITABLE
// x: first index into clusterLights, y: light count.
uvec2 clusterRange(uint cluster) { return uvec2(CLUSTER_INDEX_BASE + clusterLights[cluster * 2u], min(clusterLights[cluster * 2u + 1u], CLUSTER_MAX_LIGHTS)); }
#endif

float distanceAttenuation(float distanceSquare, float invRangeSquare) {
    float factor = distanceSquare * invRangeSquare;
    float smoothFactor = saturate(1.0 - factor * factor);
    return smoothFactor * smoothFactor / max(distanceSquare, 1e-4);
}

float spotAttenuation(vec3 L, GpuLight light) {
    float cd = dot(-L, light.directionCos.xyz);
    float attenuation = saturate(cd * light.params.x + light.params.y);
    return attenuation * attenuation;
}

#endif
