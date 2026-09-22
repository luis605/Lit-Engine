#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "mesh.glsl"

layout(std430) readonly buffer VisibleObjectBuffer {
    uvec2 visibleEntries[];
};

layout(std430) readonly buffer MaterialBuffer {
    vec4 materials[];
};

layout(location = 5) flat out vec4 out_material;

layout(std430) readonly buffer CullSphereBuffer {
    vec4 cullSpheres[];
};

layout(std430) readonly buffer CullOrientationBuffer {
    uint cullOrientations[];
};

layout(std430) readonly buffer NormalSampleBuffer {
    vec4 normalSamples[];
};

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_fragPos;
layout(location = 2) flat out float out_billboard;
layout(location = 3) flat out float out_pointRound;
layout(location = 4) flat out float out_pointSize;

const uint NORMAL_SAMPLES = 64u;
uint hashStep(uint h) {
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

vec3 rotate(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main() {
    uvec2 entry = visibleEntries[gl_VertexIndex];
    uint objectId = entry.x & 0x1FFFFFFFu;
    out_material = materials[entry.y];
    uint meshSlot = uint(gl_InstanceIndex) / LOD_LEVEL_COUNT * LOD_LEVEL_COUNT;
    vec4 boundingSphere = cullSpheres[objectId];
    vec4 orientation = normalize(unpackSnorm4x8(cullOrientations[objectId]));

    vec3 toViewer = normalize(viewPos - boundingSphere.xyz);
    vec3 viewInObject = rotate(vec4(-orientation.xyz, orientation.w), toViewer);
    uint base = meshSlot * NORMAL_SAMPLES;
    uint seed = objectId * 2654435761u + 0x9e3779b9u;
    float shape = normalSamples[base].w;

    uint state = hashStep(seed);
    float totalWeight = 0.0;
    uint chosen = 0u;
    for (uint k = 0u; k < NORMAL_SAMPLES; ++k) {
        float weight = max(dot(normalSamples[base + k].xyz, viewInObject), 0.0);
        totalWeight += weight;
        state = state * 747796405u + 2891336453u;
        float u = float(state >> 8u) * (1.0 / 16777216.0);
        if (u * totalWeight < weight) chosen = k;
    }
    vec3 shadedNormal = totalWeight > 0.0 ? rotate(orientation, normalSamples[base + chosen].xyz) : toViewer;

    vec4 clip = projection * (view * vec4(boundingSphere.xyz, 1.0));
    out_fragPos = boundingSphere.xyz;
    out_normal = shadedNormal;
    out_billboard = 0.0;
    float pointSize = max(1.0, abs(shape) * boundingSphere.w * projection[1][1] * screenParams.y / max(clip.w, 0.001));
    out_pointRound = shape < 0.0 ? 1.0 : 0.0;
    out_pointSize = pointSize;
    gl_Position = clip;
    gl_PointSize = pointSize;
}
