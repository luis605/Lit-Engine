#version 460 core

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Input uniform values
uniform vec4 colDiffuse; // Entity Color
uniform vec4 ambientLight;
uniform vec3 viewPos;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

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
mat3 CalcTBN(vec3 T, vec3 B, vec3 N)
{
    return mat3(T, B, N);
}

vec3 TangentSpaceNormal(vec3 normalMap, mat3 TBN) {
    return normalize((normalMap * 2.0 - 1.0) * TBN);
}

vec3 CalculateFresnelReflection(vec3 baseReflectance, vec3 viewDirection, vec3 halfVector) {
    float cosTheta = dot(viewDirection, halfVector);
    cosTheta = max(cosTheta, 0.0);
    float fresnel = pow(1.0 - cosTheta, 5.0);
    vec3 finalColor = baseReflectance + (vec3(1.0) - baseReflectance) * fresnel;
    return finalColor;
}

vec3 toneMap(vec3 hdrColor) {
    // Apply tone mapping here (e.g., Reinhard or ACES).
    return hdrColor / (hdrColor + vec3(1.0)); // Adjust as needed.
}



void main() {
    vec4 texColor = texture(texture0, fragTexCoord);

    vec3 tangent = dFdx(fragPosition);
    vec3 bitangent = dFdy(fragPosition);

    vec3 T = normalize(tangent);
    vec3 B = normalize(bitangent);
    vec3 N = normalize(fragNormal);
    
    mat3 TBN = CalcTBN(T, B, N);

    vec3 norm;

    if (normalMapInit) {
        norm = texture(texture2, fragTexCoord).rgb;
        norm = norm * 2.0 - 1.0;
        norm = normalize(TBN * norm);
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
            vec3 viewDirWorld = normalize(vec3(inverse(viewMatrix) * vec4(viewDir, 0.0))); // Transform viewDir to world space
            vec3 H = normalize(lightDir + viewDirWorld); // Calculate H using transformed viewDir
            
            float distance = length(light.position - fragPosition);
            float attenuation = 1.0 / (1.0 + light.attenuation * distance * distance);

            float NdotL = max(dot(norm, lightDir), 0.0);

            float NdotH = max(dot(norm, H), 0.0);

            float roughnessFactor = max(1.0 - roughness, 0.02);
            float roughnessSquared = roughnessFactor * roughnessFactor;
            float geometricTerm = 2.0 * NdotH * NdotL / max(NdotH + NdotL, 0.001);
            float k_s = (roughnessSquared + 1.0) / (8.0 * max(NdotH * NdotL, 0.001));
            float k_d = 1.0 - k_s;

            // Calculate the specular term
            float specularTerm = k_s / (NdotH * NdotH * max(4.0 * NdotL * NdotH, 0.001));

            // Use the unmodified NdotL for diffuse term
            result += (NdotL + specular * specularTerm) * light.color.rgb * attenuation;
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

            vec3 lightDirTangent = normalize(lightToPoint * TBN);

            // Calculate the diffuse term using the sampled normal
            float NdotL = max(dot(norm, lightDirTangent), 0.0);
            vec3 diffuseTerm = colDiffuse.rgb * NdotL;

            result += diffuseTerm * spot * light.intensity * attenuation * light.color.rgb * energyFactor + (colDiffuse.rgb * ambient.rgb);
        }

    }


    result *= ao;
    result += vec4(result, 1.0).rgb * texColor.rgb;


    // Apply gamma correction
    result = pow(result, vec3(2.2));

    result = toneMap(result);

    // Assign the final color
    finalColor = vec4(result, 1.0);
}