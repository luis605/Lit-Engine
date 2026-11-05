#ifndef LIT_REFLECTION_GLSL
#define LIT_REFLECTION_GLSL

#include "scene.glsl"

const int MAX_PROBES = 8;
const float PROBE_MIP_COUNT = 6.0;
const float REFLECT_ENV_SCALE = 1.0e-3;
const float REFLECT_ENV_DECODE = 1000.0;
const int FEATURE_SSR = 1;
const int FEATURE_PROBES = 2;
const int FEATURE_PLANAR = 4;
const int FEATURE_WORLD = 8;
const float VOXEL_AMBIENT_MIP = 5.0;
const float VOXEL_START_BIAS = 1.0e-3;
const float MIN_DIRECTION = 1.0e-5;

layout(std140) uniform ReflectionData {
    vec4 reflectMode;
    vec4 reflectCounts;
    vec4 planeCenter;
    vec4 planeNormal;
    vec4 planeAxisU;
    vec4 voxelMin;
    vec4 voxelSize;
    vec4 probeCenter[MAX_PROBES];
    vec4 probeExtent[MAX_PROBES];
};

uniform sampler3D u_voxels;

bool reflectionFeature(int feature) { return (int(reflectMode.x + 0.5) & feature) != 0; }
float reflectionIntensity() { return reflectMode.y; }
float reflectionMaxRoughness() { return reflectMode.z; }
int reflectionVoxelSteps() { return int(reflectMode.w + 0.5); }
int probeCount() { return int(reflectCounts.x + 0.5); }
bool planarActive() { return reflectCounts.y > 0.5; }
bool voxelsValid() { return reflectCounts.z > 0.5; }
int planarDivisor() { return max(int(reflectCounts.w + 0.5), 1); }

vec3 safeDirection(vec3 direction) {
    return mix(direction, vec3(MIN_DIRECTION), lessThan(abs(direction), vec3(MIN_DIRECTION)));
}

vec3 boxProject(vec3 position, vec3 direction, vec3 center, vec3 halfExtent) {
    vec3 safe = safeDirection(direction);
    vec3 first = (center + halfExtent - position) / safe;
    vec3 second = (center - halfExtent - position) / safe;
    vec3 farthest = max(first, second);
    float wallDistance = max(min(farthest.x, min(farthest.y, farthest.z)), 0.0);
    return normalize(position + direction * wallDistance - center);
}

float probeInfluence(vec3 position, int index) {
    vec3 margin = probeExtent[index].xyz - abs(position - probeCenter[index].xyz);
    return saturate(min(margin.x, min(margin.y, margin.z)) / max(probeExtent[index].w, 1.0e-3));
}

int strongestProbe(vec3 position, out float weight) {
    int best = -1;
    weight = 0.0;
    int count = min(probeCount(), MAX_PROBES);
    for (int index = 0; index < count; ++index) {
        float candidate = probeInfluence(position, index);
        if (candidate > weight) {
            weight = candidate;
            best = index;
        }
    }
    return best;
}

struct VoxelHit {
    bool hit;
    vec3 albedo;
    vec3 normal;
    float travel;
    float span;
    ivec3 cell;
};

VoxelHit traceVoxels(vec3 origin, vec3 direction, float maxDistance, int maxSteps) {
    VoxelHit result;
    result.hit = false;
    result.albedo = vec3(0.0);
    result.normal = -direction;
    result.travel = 0.0;
    result.span = 0.0;
    result.cell = ivec3(0);
    float resolution = voxelSize.w;
    vec3 cellSize = voxelSize.xyz / resolution;
    vec3 safe = safeDirection(direction);
    vec3 boundsLow = (voxelMin.xyz - origin) / safe;
    vec3 boundsHigh = (voxelMin.xyz + voxelSize.xyz - origin) / safe;
    vec3 nearT = min(boundsLow, boundsHigh);
    vec3 farT = max(boundsLow, boundsHigh);
    float tEnter = max(max(nearT.x, max(nearT.y, nearT.z)), 0.0);
    float tExit = min(min(farT.x, min(farT.y, farT.z)), maxDistance);
    if (tEnter >= tExit) return result;
    vec3 gridStart = (origin + direction * (tEnter + VOXEL_START_BIAS) - voxelMin.xyz) / cellSize;
    ivec3 cell = clamp(ivec3(floor(gridStart)), ivec3(0), ivec3(int(resolution) - 1));
    vec3 gridDirection = safe / cellSize;
    ivec3 stepSign = ivec3(sign(gridDirection));
    vec3 tDelta = abs(1.0 / gridDirection);
    vec3 nextBoundary = vec3(cell) + max(vec3(stepSign), vec3(0.0));
    vec3 tMax = tEnter + (nextBoundary - gridStart) / gridDirection;
    bool skipping = texelFetch(u_voxels, cell, 0).a > 0.5;
    float travel = tEnter;
    for (int iteration = 0; iteration < maxSteps; ++iteration) {
        vec4 texel = texelFetch(u_voxels, cell, 0);
        bool solid = texel.a > 0.5;
        if (solid && !skipping) {
            result.hit = true;
            result.albedo = texel.rgb;
            result.travel = travel;
            result.span = min(tDelta.x, min(tDelta.y, tDelta.z));
            result.cell = cell;
            return result;
        }
        if (!solid) skipping = false;
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            travel = tMax.x;
            cell.x += stepSign.x;
            tMax.x += tDelta.x;
            result.normal = vec3(-float(stepSign.x), 0.0, 0.0);
        } else if (tMax.y < tMax.z) {
            travel = tMax.y;
            cell.y += stepSign.y;
            tMax.y += tDelta.y;
            result.normal = vec3(0.0, -float(stepSign.y), 0.0);
        } else {
            travel = tMax.z;
            cell.z += stepSign.z;
            tMax.z += tDelta.z;
            result.normal = vec3(0.0, 0.0, -float(stepSign.z));
        }
        if (travel > tExit || any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(int(resolution))))) break;
    }
    return result;
}

const int REFINE_ITERATIONS = 5;
const float SURFACE_ALPHA = 0.5;
const float EDGE_FADE_CELLS = 4.0;

float voxelAlpha(vec3 position) {
    return textureLod(u_voxels, (position - voxelMin.xyz) / voxelSize.xyz, 0.0).a;
}

float refineVoxelHit(vec3 origin, vec3 direction, VoxelHit hit) {
    float low = max(hit.travel - hit.span, 0.0);
    float high = hit.travel + hit.span;
    for (int iteration = 0; iteration < REFINE_ITERATIONS; ++iteration) {
        float middle = 0.5 * (low + high);
        if (voxelAlpha(origin + direction * middle) < SURFACE_ALPHA) low = middle;
        else high = middle;
    }
    return 0.5 * (low + high);
}

vec3 voxelSurfaceNormal(vec3 position, vec3 fallback) {
    vec3 step = voxelSize.xyz / voxelSize.w;
    vec3 gradient = vec3(
        voxelAlpha(position + vec3(step.x, 0.0, 0.0)) - voxelAlpha(position - vec3(step.x, 0.0, 0.0)),
        voxelAlpha(position + vec3(0.0, step.y, 0.0)) - voxelAlpha(position - vec3(0.0, step.y, 0.0)),
        voxelAlpha(position + vec3(0.0, 0.0, step.z)) - voxelAlpha(position - vec3(0.0, 0.0, step.z)));
    float magnitude = length(gradient);
    return magnitude > 1.0e-3 ? -gradient / magnitude : fallback;
}

float voxelEdgeFade(vec3 position) {
    vec3 cellSize = voxelSize.xyz / voxelSize.w;
    vec3 margin = min(position - voxelMin.xyz, voxelMin.xyz + voxelSize.xyz - position) / (cellSize * EDGE_FADE_CELLS);
    return saturate(min(margin.x, min(margin.y, margin.z)));
}

vec3 voxelRadianceScaled(vec3 albedo, vec3 normal, vec3 ambientScaled) {
    float sunTerm = sunIlluminance.w > 0.0 ? saturate(dot(normal, sunDirection.xyz)) * INV_PI * REFLECT_ENV_SCALE : 0.0;
    return albedo * (ambientScaled + sunIlluminance.rgb * sunTerm);
}

#endif
