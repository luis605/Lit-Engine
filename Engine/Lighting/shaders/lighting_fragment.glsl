#version 430 core

precision mediump float;

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Input uniform values
uniform vec4 colDiffuse;
uniform vec4 ambientLight;
uniform vec3 viewPos;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform vec2 tiling;

// PBR Textures
uniform bool diffuseMapReady;
uniform bool normalMapReady;
uniform bool roughnessMapReady;
uniform sampler2D texture0;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform sampler2D texture4;

// Lights
#define PI 3.1415926
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct Light {
    int type;
    bool enabled;
    vec3 position;
    vec3 relativePosition;
    vec3 target;
    vec3 direction;
    vec4 color;
    float attenuation;
    float intensity;
    float specularStrength;
};

layout(std430, binding = 0) buffer LightsBuffer {
    Light lights[];
};

uniform int lightsCount;

// Material properties
struct SurfaceMaterial {
    float SpecularIntensity;
    float DiffuseIntensity;
    float Roughness;
    float Metalness;
};

layout(std140) uniform MaterialBlock {
    SurfaceMaterial surfaceMaterial;
};

// Output fragment color
out vec4 finalColor;

// Helper Functions
vec3 FresnelSchlick(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float NdotL, float NdotV, float roughness) {
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    return ggx1 * ggx2;
}

vec3 CookTorrance(vec3 L, vec3 V, vec3 N, float roughness, vec3 F0) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F = FresnelSchlick(VdotH, F0, roughness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotL, NdotV, roughness);

    return F * D * G / max(4.0 * NdotL * NdotV, 0.001);
}

vec4 CalculateDiffuseLighting(vec3 norm, vec3 lightDir, vec4 lightColor, float diffuseIntensity) {
    float NdotL = max(dot(norm, lightDir), 0.0);
    return lightColor * colDiffuse * NdotL * diffuseIntensity / PI;
}

vec4 CalculateAmbientLighting(vec4 texColor, float diffuseIntensity) {
    float ambientOcclusion = 1.0; // Placeholder for AO factor
    return ambientLight * texColor * ambientOcclusion * diffuseIntensity;
}

vec4 toneMapFilmic(vec4 hdrColor) {
    vec3 x = max(vec3(0.0), hdrColor.rgb - vec3(0.004));
    vec3 result = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    return vec4(result, hdrColor.a);
}

vec4 CalculateLight(Light light, vec3 viewDir, vec3 norm, vec3 fragPosition, vec4 texColor, vec3 F0, SurfaceMaterial material) {
    vec3 lightDir;
    float attenuation = 1.0;
    float spot = 1.0;

    if (light.type == LIGHT_DIRECTIONAL) {
        lightDir = normalize(-light.direction);
    } else {
        lightDir = normalize(light.position - fragPosition);

        float lightToFragDist = length(light.position - fragPosition);
        attenuation = 1.0 / (1.0 + light.attenuation * lightToFragDist * lightToFragDist);
        if (light.type == LIGHT_SPOT) {
            spot = smoothstep(0.6, 0.8, dot(lightDir, light.direction));
        }
    }

    vec4 diffuse = CalculateDiffuseLighting(norm, lightDir, light.color, material.DiffuseIntensity);
    vec3 specular = CookTorrance(lightDir, viewDir, norm, material.Roughness, F0) * light.specularStrength * material.SpecularIntensity;

    return (diffuse + vec4(specular, 1.0)) * attenuation * spot * light.intensity;
}

vec4 CalculateLighting(vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec2 texCoord, SurfaceMaterial material, vec4 texColor) {
    vec4 result = vec4(0.0);
    vec3 F0 = mix(vec3(0.04), texColor.rgb, material.Metalness);

    for (int i = 0; i < lightsCount; i++) {
        if (!lights[i].enabled) continue;
        result += CalculateLight(lights[i], viewDir, fragNormal, fragPosition, texColor, F0, material);
    }

    result += CalculateAmbientLighting(texColor, material.DiffuseIntensity);
    return toneMapFilmic(result);
}

void main() {
    vec2 texCoord = fragTexCoord * tiling;

    vec4 texColor = vec4(1);
    if (diffuseMapReady) texColor = texture(texture0, texCoord);

    vec3 dp1 = dFdx(fragPosition);
    vec3 dp2 = dFdy(fragPosition);
    vec2 duv1 = dFdx(fragTexCoord);
    vec2 duv2 = dFdy(fragTexCoord);

    vec3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 bitangent = normalize(dp2 * duv1.x - dp1 * duv2.x);

    vec3 norm = fragNormal;
    if (normalMapReady) {
        mat3 TBN = mat3(normalize(tangent), normalize(bitangent), normalize(fragNormal));
        vec3 normalMap = texture(texture2, texCoord).rgb;
        norm = normalize(TBN * (normalMap * 2.0 - 1.0));
    }

    float roughness = roughnessMapReady ? texture(texture3, texCoord).r : surfaceMaterial.Roughness;
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec4 lighting = CalculateLighting(fragPosition, norm, viewDir, texCoord, surfaceMaterial, texColor);

    finalColor = lighting;
}
