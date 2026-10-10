#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "mesh.glsl"
#include "material.glsl"
#include "surface_api.glsl"

layout(std430) readonly buffer VisibleObjectBuffer {
    uvec2 visibleEntries[];
};

layout(std430) readonly buffer CullSphereBuffer {
    vec4 cullSpheres[];
};

layout(std430) readonly buffer CullOrientationBuffer {
    uint cullOrientations[];
};

layout(std430) readonly buffer NormalSampleBuffer {
    vec4 normalSamples[];
};

#ifdef TRANSPARENT
layout(std430) readonly buffer AlphaBuffer {
    uint packedAlphas[];
};
#endif

#ifdef SHADOW_PASS
layout(std140) uniform ShadowPass {
    mat4 shadowViewProjection;
    vec4 shadowPassParams;
};
#else
layout(location = 0) out vec3 out_worldPos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) flat out uint out_entity;
layout(location = 4) flat out uint out_material;
layout(location = 5) out vec3 out_objectPos;
layout(location = 6) flat out float out_alpha;
layout(location = 7) flat out float out_pointRound;
layout(location = 8) flat out float out_pointSize;
#endif

const uint NORMAL_SAMPLES = 64u;
const uint MAX_SAMPLE_ATTEMPTS = 32u;

vec3 rotateByQuat(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

vec3 sampleVisibleNormal(uint base, vec3 viewInObject, uint seed) {
    for (uint attempt = 0u; attempt < MAX_SAMPLE_ATTEMPTS; ++attempt) {
        uint r = hashU(seed + attempt * 0x9e3779b9u);
        vec3 candidate = normalSamples[base + (r >> 16u) % NORMAL_SAMPLES].xyz;
        float acceptance = float(r & 0xFFFFu) * (1.0 / 65536.0);
        if (acceptance < dot(candidate, viewInObject)) return candidate;
    }
    return vec3(0.0);
}

void main() {
    uvec2 entry = visibleEntries[gl_VertexIndex];
    uint objectId = entry.x & 0x1FFFFFFFu;
    uint materialId = entry.y & 0x7FFFu;
    uint maskBit = entry.y & 0x8000u;
    uint meshSlot = uint(gl_InstanceIndex) / LOD_LEVEL_COUNT * LOD_LEVEL_COUNT;
    vec4 boundingSphere = cullSpheres[objectId];
    float shape = normalSamples[meshSlot * NORMAL_SAMPLES].w;
#ifdef SHADOW_PASS
    vec4 clip = shadowViewProjection * vec4(boundingSphere.xyz, 1.0);
    gl_Position = clip;
    float pointScale = shadowPassParams.z > 0.5 ? shadowPassParams.y / max(clip.w, 1e-4) : shadowViewProjection[0][0];
    gl_PointSize = max(1.0, abs(shape) * boundingSphere.w * pointScale * shadowPassParams.x);
#else
    vec4 orientation = normalize(unpackSnorm4x8(cullOrientations[objectId]));
    vec3 toViewer = normalize(viewPos - boundingSphere.xyz);
    vec3 viewInObject = rotateByQuat(vec4(-orientation.xyz, orientation.w), toViewer);
    uint seed = objectId * 2654435761u + 0x9e3779b9u;
    vec3 sampledNormal = sampleVisibleNormal(meshSlot * NORMAL_SAMPLES, viewInObject, seed);
    vec3 shadedNormal = dot(sampledNormal, sampledNormal) > 0.0 ? rotateByQuat(orientation, sampledNormal) : toViewer;
    vec4 clip = viewProjection * vec4(boundingSphere.xyz, 1.0);
    float pointSize = max(1.0, abs(shape) * boundingSphere.w * projection[1][1] * screenParams.y / max(clip.w, 0.001));
    out_worldPos = boundingSphere.xyz;
    out_normal = shadedNormal;
    out_uv = vec2(0.5);
    out_entity = objectId;
    out_material = materialId | maskBit;
    out_objectPos = vec3(0.0);
#ifdef TRANSPARENT
    out_alpha = float((packedAlphas[objectId >> 2u] >> ((objectId & 3u) * 8u)) & 0xFFu) * (1.0 / 255.0);
#else
    out_alpha = 1.0;
#endif
    out_pointRound = shape < 0.0 ? 1.0 : 0.0;
    out_pointSize = pointSize;
    gl_Position = clip;
    gl_PointSize = pointSize;
#endif
}
