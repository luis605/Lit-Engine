#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "material.glsl"
#include "surface_api.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

struct WorldRows {
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

mat4 loadWorld(WorldRows w) {
    return mat4(vec4(w.r0.x, w.r1.x, w.r2.x, 0.0), vec4(w.r0.y, w.r1.y, w.r2.y, 0.0), vec4(w.r0.z, w.r1.z, w.r2.z, 0.0), vec4(w.r0.w, w.r1.w, w.r2.w, 1.0));
}

layout(std430) readonly buffer WorldMatrixBuffer {
    WorldRows worldRows[];
};

layout(std430) readonly buffer VisibleObjectBuffer {
    uvec2 visibleEntries[];
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
#endif

#pragma lit_user_code

void main() {
    uvec2 entry = visibleEntries[gl_InstanceIndex];
    uint objectId = entry.x & 0x1FFFFFFFu;
    uint materialId = entry.y;
    mat4 model = loadWorld(worldRows[objectId]);
    VertexData v = VertexData(in_position, in_normal, in_uv);
#ifdef LIT_USER_VERTEX
    MaterialData m = materials[materialId];
    VertexContext c = VertexContext(model, objectId, materialId, sceneTime(), m.custom0, m.custom1);
    vertex(v, c);
#endif
    vec4 worldPos = model * vec4(v.position, 1.0);
#ifdef SHADOW_PASS
    gl_Position = shadowViewProjection * worldPos;
#else
    mat3 m3 = mat3(model);
    mat3 cofactor = mat3(cross(m3[1], m3[2]), cross(m3[2], m3[0]), cross(m3[0], m3[1]));
    float detSign = dot(m3[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;
    out_worldPos = worldPos.xyz;
    out_normal = normalize(cofactor * v.normal) * detSign;
    out_uv = v.uv;
    out_entity = objectId;
    out_material = materialId;
    out_objectPos = v.position;
#ifdef TRANSPARENT
    out_alpha = float((packedAlphas[objectId >> 2u] >> ((objectId & 3u) * 8u)) & 0xFFu) * (1.0 / 255.0);
#else
    out_alpha = 1.0;
#endif
    gl_Position = viewProjection * worldPos;
#endif
}
