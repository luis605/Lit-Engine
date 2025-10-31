#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

layout(std430)
layout(binding = 2) buffer ObjectBuffer {
    mat4 models[];
};

uniform mat4 projection;
uniform mat4 view;

out vec3 FragPos;
out vec3 Normal;

void main()
{
    mat4 modelMatrix = models[gl_BaseInstance];
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(modelMatrix))) * aNormal;
    gl_Position = projection * view * worldPos;
}