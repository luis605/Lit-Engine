#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(std140) uniform SceneData {
    mat4 projection;
    mat4 view;
    vec3 lightPos;
    vec3 viewPos;
    vec3 lightColor;
    vec4 frustumPlanes[6];
    vec4 dirLightDir;
    vec4 dirLightColor;
    vec4 pointLight0Pos;
    vec4 pointLight0Color;
    vec4 pointLight1Pos;
    vec4 pointLight1Color;
    vec4 screenParams;
};

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
    uint visibleIndices[];
};

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_fragPos;
layout(location = 2) flat out float out_billboard;

void main() {
    uint objectId = visibleIndices[gl_InstanceIndex] & 0x1FFFFFFFu;
    mat4 model = loadWorld(worldRows[objectId]);
    // cofactor matrix == inverse-transpose up to a scale that is normalised away; the determinant sign keeps mirrored transforms correct
    mat3 m3 = mat3(model);
    mat3 cofactor = mat3(cross(m3[1], m3[2]), cross(m3[2], m3[0]), cross(m3[0], m3[1]));
    float detSign = dot(m3[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;

    vec4 worldPos = model * vec4(in_position, 1.0);
    out_fragPos = worldPos.xyz;
    out_billboard = 0.0;
    out_normal = normalize(cofactor * in_normal) * detSign;

    gl_Position = projection * view * worldPos;
}