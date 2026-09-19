#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

layout(std430, binding = 2) readonly buffer WorldMatrixBuffer {
    mat4 worldMatrices[];
};

struct NormalMatrix {
    vec4 col0;
    vec4 col1;
    vec4 col2;
};

layout(std430, binding = 6) readonly buffer NormalMatrixBuffer {
    NormalMatrix normalMatrices[];
};

layout (std140, binding = 0) uniform SceneData {
    mat4 projection;
    mat4 view;
    vec3 lightPos;
    vec3 viewPos;
    vec3 lightColor;
    vec4 frustumPlanes[6];
} sceneData;

layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 Normal;

struct VisibleTransparentObject {
    uint objectId;
    float distance;
};

layout(std430, binding = 5) readonly buffer VisibleTransparentObjectBuffer {
    VisibleTransparentObject visibleObjects[];
};

void main()
{
    uint objectId = visibleObjects[gl_InstanceIndex].objectId;
    mat4 modelMatrix = worldMatrices[objectId];
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    NormalMatrix nm = normalMatrices[objectId];
    Normal = mat3(nm.col0.xyz, nm.col1.xyz, nm.col2.xyz) * aNormal;
    gl_Position = sceneData.projection * sceneData.view * worldPos;
}
