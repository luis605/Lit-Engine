#version 460 core

layout (std140, binding = 0) uniform SceneData {
    mat4 projection;
    mat4 view;
    vec3 lightPos;
    vec3 viewPos;
    vec3 lightColor;
} sceneData;

out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;

void main()
{
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * sceneData.lightColor;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(sceneData.lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * sceneData.lightColor;

    float specularStrength = 0.5;
    vec3 viewDir = normalize(sceneData.viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * sceneData.lightColor;

    float n = fract(sin(gl_PrimitiveID * 12.9898) * 43758.5453);
    vec3 color = vec3(fract(n * 1.1), fract(n * 2.2), fract(n * 3.3));

    vec3 result = (ambient + diffuse + specular) * color;
    FragColor = vec4(result, 0.5);
}
