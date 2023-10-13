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
uniform vec2 tiling;

// pbr
uniform sampler2D texture0;

uniform bool normalMapInit;
uniform sampler2D texture2;

uniform bool roughnessMapInit;
uniform sampler2D texture3;

uniform sampler2D texture4;

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

vec4 toneMap(vec4 hdrColor) {
    // Apply tone mapping here (e.g., Reinhard or ACES).
    return hdrColor / (hdrColor + vec4(1.0)); // Adjust as needed.
}


vec4 CalculateDirectionalLight(Light light, vec3 viewDir, vec3 norm, vec3 halfVector, vec4 specular) {
        vec3 fragLightDir = normalize(-light.direction);
        float NdotL = max(dot(norm, fragLightDir), 0.0);

        vec3 fresnel = CalculateFresnelReflection(surface_material.baseReflectance, viewDir, halfVector);

        return (colDiffuse * surface_material.DiffuseIntensity * NdotL + specular) * light.color * vec4(fresnel, 1.0) * light.intensity;
}

vec4 CalculatePointLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec4 specular) {
    vec3 lightDir = normalize(light.position - fragPosition);
    vec3 viewDirWorld = normalize(vec3(inverse(viewMatrix) * vec4(viewDir, 0.0))); // Transform viewDir to world space
    vec3 H = normalize(lightDir + viewDirWorld); // Calculate H using transformed viewDir
    
    float distance = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * distance * distance);

    float NdotL = max(dot(norm, lightDir), 0.0) * surface_material.DiffuseIntensity;

    float NdotH = max(dot(norm, H), 0.0);

    float roughnessFactor = max(1.0 - roughness, 0.02) * surface_material.Roughness;
    float roughnessSquared = roughnessFactor * roughnessFactor;
    float geometricTerm = 2.0 * NdotH * NdotL / max(NdotH + NdotL, 0.001);
    float k_s = (roughnessSquared + 1.0) / (8.0 * max(NdotH * NdotL, 0.001));
    float k_d = 1.0 - k_s;

    // Calculate the specular term
    float specularTerm = k_s / (NdotH * NdotH * max(4.0 * NdotL * NdotH, 0.001)) * surface_material.shininess;

    // Use the unmodified NdotL for diffuse term
    return (NdotL + specular * specularTerm * surface_material.SpecularIntensity) * light.color * attenuation;

}


vec4 CalculateSpotLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec3 ambient, vec2 texCoord, mat3 TBN) {
    vec3 lightToPoint = light.position - fragPosition;
    float spot = smoothstep(0.6, 0.8, dot(normalize(lightToPoint), light.direction)); // Adjust the thresholds for smoother transition

    float lightToFragDist = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * lightToFragDist * lightToFragDist);

    // Energy conservation: Scale down diffuse term
    float k_d = 1.0; // Adjust this value to control energy conservation
    float energyFactor = 1.0 / (k_d + (1.0 - k_d) * 0.5);

    // Apply normal mapping
    float NdotL;
    if (normalMapInit)
    {
        vec3 normalMap = texture(texture2, texCoord).rgb;
        vec3 tangent = dFdx(fragPosition);
        vec3 bitangent = dFdy(fragPosition);
        vec3 T = normalize(tangent);
        vec3 B = normalize(bitangent);
        vec3 N = normalize(fragNormal);
        mat3 TBN = mat3(T, B, N);
        vec3 sampledNormal = normalize((normalMap * 2.0 - 1.0) * TBN);
        vec3 lightDirTangent = normalize(lightToPoint * TBN);
        NdotL = max(dot(sampledNormal, lightDirTangent), 0.0) * surface_material.DiffuseIntensity;
    }
    else
    {
        // Calculate the light direction in tangent space
        vec3 lightDirTangent = normalize(lightToPoint * TBN);

        // Calculate the diffuse term using the sampled normal
        NdotL = max(dot(norm, lightDirTangent), 0.0) * surface_material.DiffuseIntensity;
    }


    vec4 diffuseTerm = colDiffuse * NdotL;

    return vec4(diffuseTerm * spot * light.intensity * attenuation * energyFactor) + vec4(colDiffuse.rgb * ambient.rgb, 1);

}

void main() {
    vec2 texCoord = fragTexCoord * tiling;
    vec4 texColor = texture(texture0, texCoord);

    vec3 tangent = dFdx(fragPosition);
    vec3 bitangent = dFdy(fragPosition);

    vec3 T = normalize(tangent);
    vec3 B = normalize(bitangent);
    vec3 N = normalize(fragNormal);
    
    mat3 TBN = CalcTBN(T, B, N);

    vec3 norm;

    if (normalMapInit) {
        norm = texture(texture2, texCoord).rgb;
        norm = norm * 2.0 - 1.0;
        norm = normalize(TBN * norm);
    } else
        norm = normalize(fragNormal);

    float ao = texture(texture4, texCoord).r;

    float roughness;
    if (roughnessMapInit)
        roughness = texture(texture3, texCoord).r;
    else
        roughness = 0.1;

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir = reflect(-viewDir, norm);

    vec3 diffuse;
    vec4 specular;
    vec4 result = colDiffuse / 1.8 - ambientLight * 0.5;
    vec3 ambient = vec3(0);


    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];

        vec3 halfVector = normalize(light.direction + viewDir);

        if (light.type == LIGHT_DIRECTIONAL && light.enabled) {
            result += CalculateDirectionalLight(light, viewDir, norm, halfVector, specular);
        }
        else if (light.type == LIGHT_POINT && light.enabled) {
            result += CalculatePointLight(light, viewDir, norm, roughness, ao, fragPosition, specular);
        }
        else if (light.type == LIGHT_SPOT && light.enabled)
        {
            result += CalculateSpotLight(light, viewDir, norm, roughness, ao, fragPosition, ambient, texCoord, TBN);
        }


    }



    result *= ao;
    result += result * texColor;


    // Apply gamma correction
    result = pow(result, vec4(2.2, 2.2, 2.2, 1.0));

    result = vec4(toneMap(result).rgb, result.a);

    // Assign the final color
    finalColor = result;
}