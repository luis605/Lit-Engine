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
};

layout(std430) readonly buffer WorldMatrixBuffer {
    mat4 worldMatrices[];
};

layout(std430) readonly buffer NormalMatrixBuffer {
    mat3x4 normalMatrices[];
};

layout(std430) readonly buffer VisibleObjectBuffer {
    uint visibleIndices[];
};

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_fragPos;

void main() {
    uint objectId = visibleIndices[gl_InstanceIndex];
    mat4 model = worldMatrices[objectId];
    mat3 normalMatrix = mat3(
        normalMatrices[objectId][0].xyz,
        normalMatrices[objectId][1].xyz,
        normalMatrices[objectId][2].xyz
    );

    vec4 worldPos = model * vec4(in_position, 1.0);
    out_fragPos = worldPos.xyz;
    out_normal = normalize(normalMatrix * in_normal);

    gl_Position = projection * view * worldPos;
}