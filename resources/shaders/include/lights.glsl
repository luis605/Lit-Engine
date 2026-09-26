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

layout(std430) readonly buffer ClusterBuffer {
    uint clusterLights[];
};

const uint CLUSTER_X = 16u;
const uint CLUSTER_Y = 9u;
const uint CLUSTER_Z = 24u;
const uint CLUSTER_STRIDE = 128u;

uint clusterSlice(float viewDepth) {
    float slice = log(max(viewDepth, nearPlane) / nearPlane) * clusterParams.x;
    return min(uint(max(slice, 0.0)), CLUSTER_Z - 1u);
}

uint clusterIndex(vec2 pixel, float viewDepth) {
    uvec2 tile = min(uvec2(pixel / vec2(clusterParams.zw)), uvec2(CLUSTER_X - 1u, CLUSTER_Y - 1u));
    return (clusterSlice(viewDepth) * CLUSTER_Y + tile.y) * CLUSTER_X + tile.x;
}

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
