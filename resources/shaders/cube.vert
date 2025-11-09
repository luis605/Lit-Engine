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

out vec3 FragPos;
out vec3 Normal;

layout(std430, binding = 5) readonly buffer VisibleObjectBuffer {
    uint visibleObjects[];
};

void main()
{
    uint baseInstance = gl_BaseInstance;
    uint objectId = visibleObjects[baseInstance + gl_InstanceID];
    mat4 modelMatrix = transforms[objectId].worldMatrix;
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(modelMatrix))) * aNormal;
    gl_Position = sceneData.projection * sceneData.view * worldPos;
}