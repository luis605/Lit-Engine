#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

struct TransformComponent {
    mat4 localMatrix;
    mat4 worldMatrix;
};

layout(std430, binding = 2) buffer ObjectBuffer {
    TransformComponent transforms[];
};

layout (std140, binding = 0) uniform SceneData {
    mat4 projection;
    mat4 view;
    vec3 lightPos;
    vec3 viewPos;
    vec3 lightColor;
    vec4 frustumPlanes[6];
} sceneData;

layout(std430)
layout(binding = 5) readonly buffer VisibleLargeObjectBuffer {
    uint visibleLargeObjects[];
};

void main()
{
    uint baseInstance = gl_BaseInstance;
    uint objectId = visibleLargeObjects[baseInstance + gl_InstanceID];
    mat4 modelMatrix = transforms[objectId].worldMatrix;
    gl_Position = sceneData.projection * sceneData.view * modelMatrix * vec4(aPos, 1.0);
}
