#version 430 core

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
vec2 texCoord;

uniform sampler2D texture0;

uniform bool normalMapInit;
uniform sampler2D texture2;

uniform bool roughnessMapInit;
uniform sampler2D texture3;

uniform sampler2D texture4;

// Lights
#define PI 3.14159265358979323846

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct Light
{
    int type;
    bool enabled;
    vec3 position;
    vec3 relativePosition;
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
    SurfaceMaterial surfaceMaterial;
};

// Output fragment color
out vec4 finalColor;
out vec3 fragLightDir;
out vec3 fragViewDir;

// Helper Functions
mat3 CalcTBN(vec3 T, vec3 B, vec3 N)
{
    return mat3(normalize(T), normalize(B), normalize(N));
}

vec3 TangentSpaceNormal(vec3 normalMap, mat3 TBN) {
    return normalize(normalMap * 2.0 - 1.0) * TBN;
}

vec3 CalculateFresnelReflection(vec3 baseReflectance, vec3 viewDirection, vec3 halfVector) {
    float cosTheta = max(dot(viewDirection, halfVector), 0.0);
    float fresnel = pow(1.0 - cosTheta, 5.0);
    return mix(baseReflectance, vec3(1.0), fresnel);
}


vec4 toneMap(vec4 hdrColor, float exposure) {
    vec3 mapped = hdrColor.rgb / (hdrColor.rgb + vec3(1.0));
    mapped = pow(mapped, vec3(1.0 / exposure));
    return vec4(mapped, 1.0);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float d = NdotH * NdotH * (a * a - 1.0) + 1.0;
    return a * a / (PI * d * d);
}

// Geometry function for GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}


// Cook-Torrance BRDF
vec3 CookTorrance(vec3 L, vec3 V, vec3 N, vec3 albedo, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F = FresnelSchlick(VdotH, vec3(0.04));
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);

    vec3 specular = F * D * G / max(4.0 * NdotL * NdotV, 0.001); // Add epsilon to prevent division by zero

    return (albedo / PI + specular) * NdotL;
}


vec4 CalculateDiffuseLighting(vec3 fragPosition, vec3 norm) {
    vec3 diffuseComponent = colDiffuse.rgb;
    vec3 ambientComponent = colDiffuse.rgb * ambientLight.rgb * surfaceMaterial.DiffuseIntensity;
    vec3 resultColor = ambientComponent * diffuseComponent;
    vec4 diffuseColor = vec4(resultColor, 1.0);

    return diffuseColor;
}


vec4 CalculateDirectionalLight(Light light, vec3 viewDir, vec3 norm, float roughness, vec3 fragPosition, vec3 texColor) {
    vec3 lightDir = normalize(-light.direction);
    float NdotL = max(dot(norm, lightDir), 0.0);

    vec3 H = normalize(normalize(light.position - fragPosition) + viewDir);

    // Diffuse term
    vec4 diffuseTerm = colDiffuse * surfaceMaterial.DiffuseIntensity * NdotL;

    // Specular term (Cook-Torrance)
    float NdotV = max(dot(norm, viewDir), 0.0);
    float NdotH = max(dot(norm, H), 0.0);
    float VdotH = max(dot(viewDir, H), 0.0);
    vec3 F = FresnelSchlick(VdotH, surfaceMaterial.baseReflectance);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);
    vec3 specular = F * D * G * light.specularStrength / max(4.0 * NdotL * NdotV, 0.001);

    // Final light contribution
    vec3 lightContribution = (diffuseTerm.rgb / PI + specular) * light.color.rgb * light.intensity * texColor;

    return vec4(lightContribution, colDiffuse.a);
}



vec4 CalculatePointLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec3 texColor) {
    // Light direction
    vec3 lightDir = normalize(light.position - fragPosition);
    
    // Halfway vector
    vec3 H = normalize(lightDir + viewDir);
    
    // Distance and attenuation
    float lightToFragDist = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * lightToFragDist * lightToFragDist);

    // Diffuse lighting
    float NdotL = max(dot(norm, lightDir), 0.0);
    vec4 diffuseTerm = colDiffuse * surfaceMaterial.DiffuseIntensity * NdotL;

    // Specular lighting
    float NdotV = max(dot(norm, viewDir), 0.0);
    float NdotH = max(dot(norm, H), 0.0);
    float VdotH = max(dot(viewDir, H), 0.0);
    vec3 F = FresnelSchlick(VdotH, surfaceMaterial.baseReflectance);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);
    vec3 specular = F * D * G * light.specularStrength / max(4.0 * NdotL * NdotV, 0.001);

    // Combine diffuse and specular
    vec3 lightContribution = (diffuseTerm.rgb / PI + specular) * light.color.rgb * attenuation * light.intensity * texColor;

    return vec4(lightContribution, 1.0);
}


vec4 CalculateSpotLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec3 ambient, vec2 texCoord, vec3 texColor) {
    vec3 lightToPoint = light.position - fragPosition;
    vec3 lightDir = normalize(lightToPoint);
    float spot = smoothstep(0.6, 0.8, dot(lightDir, light.direction));

    float lightToFragDist = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * lightToFragDist * lightToFragDist);

    float NdotL = max(dot(norm, lightDir), 0.0);
    vec4 diffuseTerm = colDiffuse * surfaceMaterial.DiffuseIntensity * NdotL;

    vec3 H = normalize(lightDir + viewDir);
    float NdotH = max(dot(norm, H), 0.0);
    float VdotH = max(dot(viewDir, H), 0.0);
    vec3 F = FresnelSchlick(VdotH, surfaceMaterial.baseReflectance);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(max(dot(norm, viewDir), 0.0), roughness);
    vec3 specular = F * D * G * light.specularStrength / max(4.0 * NdotL * max(dot(norm, viewDir), 0.0), 0.001);

    vec3 lightContribution = (diffuseTerm.rgb / PI + specular) * light.color.rgb * attenuation * spot * light.intensity * texColor;

    return vec4(lightContribution, colDiffuse.a);
}

vec4 CalculateLighting(vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec2 texCoord, SurfaceMaterial material, float roughness, vec4 texColor) {
    vec4 result = vec4(0.0);

    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];
        
        if (!light.enabled) continue;

        if (light.type == LIGHT_DIRECTIONAL) {
            // Calculate directional light
            result += CalculateDirectionalLight(light, viewDir, fragNormal, material.Roughness, fragPosition, texColor.rgb);
        } else if (light.type == LIGHT_POINT) {
            // Calculate point light
            result += CalculatePointLight(light, viewDir, fragNormal, material.Roughness, 1.0, fragPosition, texColor.rgb);
        } else if (light.type == LIGHT_SPOT) {
            // Calculate spot light
            result += CalculateSpotLight(light, viewDir, fragNormal, material.Roughness, 1.0, fragPosition, vec3(0.0), texCoord, texColor.rgb);
        }
    }

    vec4 diffuseLight = CalculateDiffuseLighting(fragPosition, fragNormal);
    result += diffuseLight;
    
    vec4 toneMappedResult = toneMap(result, 0.5);

    float ao = texture(texture4, texCoord).r;
    if (ao != 0.0)
        result *= ao;

    return vec4(toneMappedResult.rgb, result);
}


void main() {
    texCoord = fragTexCoord * tiling;
    vec4 texColor = texture(texture0, texCoord);

    // Calculate tangent space
    vec3 dp1 = dFdx(fragPosition);
    vec3 dp2 = dFdy(fragPosition);
    vec2 duv1 = dFdx(fragTexCoord);
    vec2 duv2 = dFdy(fragTexCoord);

    vec3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 bitangent = normalize(dp2 * duv1.x - dp1 * duv2.x);

    mat3 TBN = mat3(tangent, bitangent, normalize(fragNormal));
    vec3 normalMap = vec3(0.0);
    vec3 norm = fragNormal;

    // Calculate normal
    if (normalMapInit) {
        normalMap = texture(texture2, texCoord).rgb * 2.0 - 1.0;
        norm = normalize(TBN * normalMap);
    } else {
        norm = normalize(fragNormal);
    }

    // Calculate roughness
    float roughness = roughnessMapInit ? texture(texture3, texCoord).r : surfaceMaterial.Roughness;

    // Calculate view direction
    vec3 viewDir = normalize(viewPos - fragPosition);

    // Calculate lighting
    vec3 lighting = CalculateLighting(fragPosition, norm, viewDir, texCoord, surfaceMaterial, roughness, texColor).rgb;

    // Calculate final color
    vec4 result = vec4(colDiffuse.rgb / 1.8 - ambientLight.rgb * 0.5, colDiffuse.a);
    vec3 ambient = vec3(0);

    finalColor = mix(result, vec4(lighting, colDiffuse.a), step(0.0, length(lighting)));
}
