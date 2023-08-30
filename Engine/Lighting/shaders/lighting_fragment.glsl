#version 460 core

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Input uniform values
uniform vec4 colDiffuse; // Entity Color
uniform vec4 ambientLight;
uniform vec3 viewPos;

// pbr
uniform sampler2D texture0;

uniform bool normalMapInit;
uniform sampler2D texture2;

uniform bool roughnessMapInit;
uniform sampler2D texture3;

uniform sampler2D texture4;

// Fog
vec4 fogColor = vec4(1, 0.6, 0.6, 1);
// Lights
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct Light
{
    int type;
    bool enabled;
    vec3 position;
    vec3 relative_position;
    vec3 target;
    vec4 color;
    float attenuation;
    float intensity;
    float specularStrength;
    float cutOff;
    bool isChild;
    vec3 direction;
    // Spot
    float spread;
    float penumbraFactor;
};

layout(std430, binding = 0) buffer LightsBuffer
{
    Light lights[];
};

uniform int lightsCount;

// Materials
struct SurfaceMaterial
{
    float shininess;
    float SpecularIntensity;
    float Roughness;
    float DiffuseIntensity;
    vec3 SpecularTint;
    vec3 baseReflectance;
};

layout(std140) uniform MaterialBlock {
    SurfaceMaterial surface_material;
};

// Output fragment color
out vec4 finalColor;
out vec3 fragLightDir;
out vec3 fragViewDir;

// Helper Functions
vec3 TangentSpaceNormal(vec3 normalMap, vec3 tangent, vec3 bitangent) {
    mat3 TBN = mat3(tangent, bitangent, fragNormal); // Notice fragNormal as the third axis
    return normalize(TBN * (normalMap * 2.0 - 1.0)); // Use normalMap directly
}

vec3 CalculateFresnelReflection(vec3 baseReflectance, vec3 viewDirection, vec3 halfVector) {
    float cosTheta = dot(viewDirection, halfVector);
    cosTheta = max(cosTheta, 0.0);
    float fresnel = pow(1.0 - cosTheta, 5.0);
    vec3 finalColor = baseReflectance + (vec3(1.0) - baseReflectance) * fresnel;
    return finalColor;
}

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);

    vec3 normalMap = texture(texture2, fragTexCoord).rgb;
    vec3 tangent = dFdx(fragPosition);
    vec3 bitangent = dFdy(fragPosition);
    vec3 norm;

    if (normalMapInit) {
        norm = TangentSpaceNormal(normalMap, tangent, bitangent);
    } else
        norm = normalize(fragNormal);

    float ao = texture(texture4, fragTexCoord).r;

    float roughness;
    if (roughnessMapInit)
        roughness = texture(texture3, fragTexCoord).r;
    else
        roughness = 0.1;

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir = reflect(-viewDir, norm);

    vec3 diffuse;
    vec3 specular;
    vec3 result = colDiffuse.rgb / 1.8 - ambientLight.rgb * 0.5;
    vec3 ambient = vec3(0);


    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];

        vec3 halfVector = normalize(light.direction + viewDir);

        if (light.type == LIGHT_DIRECTIONAL && light.enabled) {
            vec3 fragLightDir = normalize(-light.direction);
            float NdotL = max(dot(norm, fragLightDir), 0.0);

            vec3 fresnel = CalculateFresnelReflection(surface_material.baseReflectance, viewDir, halfVector);

            result += (colDiffuse.rgb * surface_material.DiffuseIntensity * NdotL + specular) * light.color.rgb * fresnel * light.intensity;
        }



        else if (light.type == LIGHT_POINT && light.enabled) {
            vec3 lightDir = normalize(light.position - fragPosition);
            float distance = length(light.position - fragPosition);
            float attenuation = 1.0 / (1.0 + light.attenuation * distance * distance);
            float NdotL = max(dot(norm, lightDir), 0.0);

            // Simple diffuse and specular terms
            vec3 diffuseTerm = colDiffuse.rgb * NdotL;
            vec3 specularTerm = vec3(0.0); // No specular for now

            result += (diffuseTerm + specularTerm) * light.color.rgb * attenuation * light.intensity;
        }






        else if (light.type == LIGHT_SPOT && light.enabled)
        {
            vec3 lightToPoint = light.position - fragPosition;
            float spot = smoothstep(0.6, 0.8, dot(normalize(lightToPoint), light.direction)); // Adjust the thresholds for smoother transition

            float lightToFragDist = length(light.position - fragPosition);
            float attenuation = 1.0 / (1.0 + light.attenuation * lightToFragDist * lightToFragDist);

            // Energy conservation: Scale down diffuse term
            float k_d = 1.0; // Adjust this value to control energy conservation
            float energyFactor = 1.0 / (k_d + (1.0 - k_d) * 0.5);

            // Apply normal mapping
            vec3 normalMap = texture(texture2, fragTexCoord).rgb;
            vec3 tangent = dFdx(fragPosition);
            vec3 bitangent = dFdy(fragPosition);
            vec3 T = normalize(tangent);
            vec3 B = normalize(bitangent);
            vec3 N = normalize(fragNormal);
            mat3 TBN = mat3(T, B, N);
            vec3 sampledNormal = normalize((normalMap * 2.0 - 1.0) * TBN);

            // Calculate the light direction in tangent space
            vec3 lightDirTangent = normalize(lightToPoint * TBN);

            // Calculate the diffuse term using the sampled normal
            float NdotL = max(dot(sampledNormal, lightDirTangent), 0.0);
            vec3 diffuseTerm = colDiffuse.rgb * NdotL;

            result += diffuseTerm * spot * light.intensity * attenuation * energyFactor + (colDiffuse.rgb * ambient.rgb);
        }

    }


    result *= ao;
    result += vec4(result, 1.0).rgb * texColor.rgb;

    // Apply gamma correction
    result = pow(result, vec3(2.2));

    // Assign the final color
    finalColor = vec4(result, 1.0);
}