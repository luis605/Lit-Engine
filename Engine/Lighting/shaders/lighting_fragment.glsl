#version 460 core

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
//in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse; // Entity Color
uniform vec4 ambientLight;

uniform vec3 viewPos;


#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1

struct Light
{
    int type;
    bool enabled;
    vec3 position;
    vec3 relative_position;
    vec3 target;
    vec4 color;
    float attenuation;
    bool isChild;
};


layout(std430, binding = 0) buffer LightsBuffer
{
    Light lights[];
};

uniform int lightsCount;  // Passed from CPU

// Output fragment color
out vec4 finalColor;

void main() {
    vec3 norm = normalize(fragNormal);
    vec3 lightDir;
    float diff;
    float spec;
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir;
    vec3 diffuse;
    vec3 specular;
    vec3 result = ambientLight.rgb;

    int specularStrength = 1;
    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];

        lightDir = normalize(light.position - fragPosition);
        diff = max(dot(norm, lightDir), 0.0);
        diffuse = diff * light.color.rgb;
        result += diffuse;

        reflectDir = reflect(-lightDir, norm);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        specular = specularStrength * spec * light.color.rgb;
        result += specular;
    }

    finalColor = vec4(result, colDiffuse.a);
}
