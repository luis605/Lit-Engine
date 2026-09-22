#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "alpha.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

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

layout(std430) readonly buffer MaterialBuffer {
    vec4 materials[];
};

layout(location = 5) flat out vec4 out_material;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_fragPos;

void main() {
    uvec2 entry = visibleEntries[gl_InstanceIndex];
    uint objectId = entry.x & 0x1FFFFFFFu;
    out_material = materials[entry.y];
#ifdef TRANSPARENT
    out_alpha = entityAlpha(objectId);
#endif
    mat4 model = loadWorld(worldRows[objectId]);
    // cofactor matrix == inverse-transpose up to a scale that is normalised away; the determinant sign keeps mirrored transforms correct
    mat3 m3 = mat3(model);
    mat3 cofactor = mat3(cross(m3[1], m3[2]), cross(m3[2], m3[0]), cross(m3[0], m3[1]));
    float detSign = dot(m3[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;

    vec4 worldPos = model * vec4(in_position, 1.0);
    out_fragPos = worldPos.xyz;
    out_normal = normalize(cofactor * in_normal) * detSign;

    gl_Position = projection * view * worldPos;
}