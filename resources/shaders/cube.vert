#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

struct TransformComponent {
    mat4 localMatrix;
    mat4 worldMatrix;
};

layout(std430)
layout(binding = 2) buffer ObjectBuffer {
    TransformComponent transforms[];
};

layout (std140, binding = 0) uniform SceneData {
    mat4 projection;
    mat4 view;
    vec3 lightPos;
    vec3 viewPos;
    vec3 lightColor;
} sceneData;

out vec3 FragPos;
out vec3 Normal;

void main()
{
    mat4 modelMatrix = transforms[gl_BaseInstance].worldMatrix;
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(modelMatrix))) * aNormal;
    gl_Position = sceneData.projection * sceneData.view * worldPos;
}