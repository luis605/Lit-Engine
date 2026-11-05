#ifndef LIT_SKY_VISIBILITY_GLSL
#define LIT_SKY_VISIBILITY_GLSL

#include "scene.glsl"
#include "reflection.glsl"

const int CEILING_STEPS = 8;
const float CEILING_REACH = 16.0;
const float CEILING_THICKNESS = 20.0;
const float CEILING_STRENGTH = 0.95;

const float WORLD_SKY_OFFSET_CELLS = 2.0;

uniform sampler3D u_skyVolume;

float worldSkyVisibility(vec3 worldPos, vec3 N) {
    if (!voxelsValid()) return 1.0;
    vec3 cellSize = voxelSize.xyz / voxelSize.w;
    float cellLength = max(cellSize.x, max(cellSize.y, cellSize.z));
    vec3 gridUv = (worldPos + N * cellLength * WORLD_SKY_OFFSET_CELLS - voxelMin.xyz) / voxelSize.xyz;
    if (any(lessThan(gridUv, vec3(0.0))) || any(greaterThan(gridUv, vec3(1.0)))) return 1.0;
    return textureLod(u_skyVolume, gridUv, 0.0).r;
}

float skyVisibility(vec3 worldPos, vec3 N, float viewDepth, float noise) {
    float upFacing = saturate(N.y * 0.5 + 0.5);
    float blocked = 0.0;
    vec3 start = worldPos + N * (0.02 + viewDepth * 0.002);
    for (int step = 0; step < CEILING_STEPS; ++step) {
        float t = (float(step) + noise) / float(CEILING_STEPS);
        vec3 position = start + vec3(0.0, 1.0, 0.0) * (CEILING_REACH * t + 0.2);
        vec4 clip = viewProjection * vec4(position, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = clamp(vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5), vec2(0.0), vec2(1.0));
        float sceneDepth = textureLod(u_depth, uv, 0.0).r;
        if (sceneDepth >= 1.0) continue;
        float difference = -(view * vec4(position, 1.0)).z - linearViewDepth(sceneDepth);
        float hit = smoothstep(0.05, 0.3, difference) * (1.0 - smoothstep(CEILING_THICKNESS * 0.5, CEILING_THICKNESS, difference));
        blocked = max(blocked, hit);
    }
    float screenOpen = 1.0 - blocked * CEILING_STRENGTH * mix(0.6, 1.0, upFacing);
    return min(screenOpen, worldSkyVisibility(worldPos, N));
}

#endif
