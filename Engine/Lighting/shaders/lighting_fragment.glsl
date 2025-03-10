#version 460 core

precision mediump float;

// Input vertex attributes
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Input uniform values
uniform vec4 colDiffuse;
uniform vec4 ambientLight;
uniform vec3 viewPos;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform vec2 tiling = vec2(1.0, 1.0);

// PBR Textures
uniform bool diffuseMapReady;
uniform bool normalMapReady;
uniform bool roughnessMapReady;
uniform bool aoMapReady;
uniform bool heightMapReady;
uniform bool metallicMapReady;
uniform bool emissiveMapReady;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform sampler2D texture4;
uniform sampler2D texture5;
uniform sampler2D texture6;

// Constants
#define PI 3.14159265359
#define EPSILON 1e-5
#define ENERGY_CONSERVATION_THRESHOLD 0.995
#define INV_PI 0.31830988618
#define EPSILON 1e-5
#define INV_4 0.25
#define POW5_CONST 5.0
const float MIN_ROUGHNESS = 0.001;
const float HALF = 0.5;
const vec3 ONE = vec3(1.0);

// Exposure
layout(std430, binding = 1) buffer ExposureBuffer {
    float exposure;
};

// Lights
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct Light {
    int type;
    vec3 position;
    vec3 relativePosition;
    vec3 target;
    vec3 direction;
    vec4 color;
    float attenuation;
    float intensity;
    float specularStrength;
    float radius;
    float innerCutoff;
    float outerCutoff;
};

layout(std430, binding = 0) buffer LightsBuffer {
    Light lights[];
};

uniform int lightsCount;

// Material properties
struct SurfaceMaterial {
    float specularIntensity;
    float albedoIntensity;
    float roughness;
    float metalness;
    float clearCoat;
    float clearCoatRoughness;
    float subsurface;
    float anisotropy;
    float transmission;
    float ior;
    float thickness;
    float heightScale;
    float emissiveIntensity;
    float aoStrength;
};

layout(std140) uniform MaterialBlock {
    SurfaceMaterial surfaceMaterial;
};

out vec4 finalColor;

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturate(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

lowp float pow5(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    return x4 * x; // x^5
}

vec3 Fresnel(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow5(1.0 - cosTheta);
}

float DistributionGGX(float NdotH2, float roughness2) {
    float denom = fma(NdotH2, roughness2 - 1.0, 1.0) * NdotH2 + 1.0;
    return roughness2 * INV_PI * denom * denom;
}

highp float GeometrySmith(float NdotL, float NdotV, float k) {
    NdotL = clamp(NdotL, 0.0, 1.0);
    NdotV = clamp(NdotV, 0.0, 1.0);
    float invK = 1.0 - k;
    float visL = NdotL / (NdotL * invK + k);
    float visV = NdotV / (NdotV * invK + k);
    return visL * visV;
}

highp float DistributionGGXAnisotropic(float NdotH2, vec3 H, vec3 T, vec3 B, float roughness, float anisotropy) {
    float at = max(roughness * (1.0 + anisotropy), MIN_ROUGHNESS);
    float ab = max(roughness * (1.0 - anisotropy), MIN_ROUGHNESS);
    float at_ab = at * ab;
    float dotTH = dot(T, H);
    float dotBH = dot(B, H);
    float v2 = fma(ab * ab, dotTH * dotTH, fma(at * at, dotBH * dotBH, at_ab * NdotH2));

    return at_ab * at_ab * INV_PI / max(v2 * v2, EPSILON);
}

highp vec3 CookTorrance(vec3 L, vec3 V, vec3 N, vec3 T, vec3 B, float roughness, vec3 F0, float specularAO) {
    // Early exit if specular AO is too low
    float specularAO_mask = step(EPSILON, specularAO);
    vec3 H = normalize(L + V);

    // dots.x = NdotL, dots.y = NdotV, dots.z = NdotH, dots.w = VdotH
    vec4 dots = vec4(dot(N, L), dot(N, V), dot(N, H), dot(V, H));

    float common_div = dots.x * dots.y;
    float commonDiv_mask = step(EPSILON, common_div);
    if (specularAO_mask * commonDiv_mask == 0.0)
        return vec3(0.0);

    // Compute half-vector and related dot products
    float NdotH2 = dots.z * dots.z;
    float roughness2 = roughness * roughness;

    // Calculate k parameters for geometry term
    float k = roughness2 * HALF;
    float k_clear = (surfaceMaterial.clearCoatRoughness * surfaceMaterial.clearCoatRoughness) * HALF;

    // Fresnel term using the half-vector
    vec3 fresnel = Fresnel(dots.w, F0);

    // Choose between anisotropic and isotropic distributions
    float anisotropyMask = step(EPSILON, abs(surfaceMaterial.anisotropy));
    float D_aniso = DistributionGGXAnisotropic(NdotH2, H, T, B, roughness, surfaceMaterial.anisotropy);
    float D_iso = DistributionGGX(NdotH2, roughness2);

    // Geometry term
    vec2 DG = vec2(
        mix(D_iso, D_aniso, anisotropyMask),
        GeometrySmith(dots.x, dots.y, k)
    );

    // Main specular calculation
    vec3 specular = (fresnel * DG.x * DG.y * specularAO * INV_4) / max(common_div, EPSILON);

    // Clear coat specular branch
    // clearCoatData.x = clear_d, clearCoatData.y = clear_g, clearCoatData.z = mix_val
    vec3 clearCoatData = vec3(
        DistributionGGX(NdotH2, k_clear),
        GeometrySmith(dots.x, dots.y, k_clear),
        mix(fresnel.r, 0.04, step(surfaceMaterial.subsurface, 0.0))
    );

    vec3 clearCoatSpecular = vec3(clearCoatData.x * clearCoatData.y * clearCoatData.z) * specularAO;

    // Blend between primary and clear coat specular
    specular = mix(specular, clearCoatSpecular, surfaceMaterial.clearCoat);

    float maxSpecular = max(dot(specular, vec3(1.0)), EPSILON);
    return specular * min(1.0, ENERGY_CONSERVATION_THRESHOLD / maxSpecular);
}

float PhysicalLightAttenuation(float distance, float radius, float attenuation) {
    float smoothRadius = max(radius, 0.01);
    float distanceSquared = distance * distance;
    float att = 1.0 / fma(attenuation, distanceSquared, 1.0);

    float radiusRatio = distanceSquared / (smoothRadius * smoothRadius);
    float t = fma(radiusRatio, 1.0 / 0.2, -0.8 / 0.2);

    return att * saturate(1.0 - t);
}

vec3 calculateChromaticAberration(vec3 refractedDir, float ior) {
    vec3 wavelengthOffsets = vec3(0.0, 0.01, 0.02);
    vec3 color;
    color.r = refract(refractedDir, vec3(0.0, 0.0, 1.0), ior + wavelengthOffsets.r).x;
    color.g = refract(refractedDir, vec3(0.0, 0.0, 1.0), ior + wavelengthOffsets.g).x;
    color.b = refract(refractedDir, vec3(0.0, 0.0, 1.0), ior + wavelengthOffsets.b).x;
    return color;
}

vec3 calculateVolumetricScattering(vec3 lightDir, vec3 viewDir, float thickness) {
    float cosTheta = dot(lightDir, -viewDir);
    vec3 scatteringCoeff = vec3(0.8, 0.5, 0.2);

    return exp(fma(-thickness, (1.0 - cosTheta), 0.0) * scatteringCoeff);
}

vec4 CalculateDiffuseLighting(vec3 norm, vec3 lightDir, vec4 lightColor, float albedoIntensity, vec4 texColor) {
    float NdotL = max(dot(norm, lightDir), 0.0);
    return lightColor * colDiffuse * NdotL * albedoIntensity / PI * texColor;
}

vec4 CalculateAmbientLighting(float roughness, vec4 texColor) {
    float occlusion = 1.0;
    float ambientFactor = mix(0.2, 1.0, 1.0 - roughness);
    return vec4(ambientLight.rgb * colDiffuse.rgb * texColor.rgb * occlusion * ambientFactor, colDiffuse.a * texColor.a);
}

vec4 toneMapFilmic(vec4 hdrColor) {
    vec3 x = max(vec3(0.0), hdrColor.rgb * exposure - vec3(0.004));
    vec3 result = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    return vec4(result, hdrColor.a);
}

vec4 toneMapACES(vec4 hdrColor) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    vec3 x = hdrColor.rgb * exposure;
    vec3 result = (x * (a * x + b)) / (x * (c * x + d) + e);
    return vec4(clamp(result, 0.0, 1.0), hdrColor.a);
}

vec4 CalculateLight(Light light, vec3 viewDir, vec3 norm, vec3 fragPosition, vec4 texColor, vec3 F0,
                    SurfaceMaterial material, vec3 T, vec3 B, float roughness) {

    float attenuation = 1.0;
    float spot = 1.0;

    vec3 toLight = light.position - fragPosition;
    vec3 lightDir = normalize(mix(toLight, -light.direction, step(light.type, LIGHT_DIRECTIONAL)));

    float lightToFragDist = length(toLight);
    float attenuationFactor = PhysicalLightAttenuation(lightToFragDist, light.radius, light.attenuation);
    float spotFactor = smoothstep(0.6, 0.8, dot(lightDir, light.direction));

    attenuation = mix(attenuationFactor * mix(spotFactor, 1.0, step(light.type, LIGHT_POINT)), 1.0, step(light.type, LIGHT_DIRECTIONAL));

    vec4 diffuse = CalculateDiffuseLighting(norm, lightDir, light.color, material.albedoIntensity, texColor);
    vec3 specular = CookTorrance(lightDir, viewDir, norm, T, B, roughness, F0, light.specularStrength * material.specularIntensity);

    return vec4(vec3((diffuse.rgb + specular) * attenuation * spot * light.intensity), 1);
}

vec4 CalculateLighting(vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec2 texCoord,
                      SurfaceMaterial material, vec4 texColor, vec3 T, vec3 B, float roughness) {

    vec3 F0 = mix(vec3(0.04), texColor.rgb, material.metalness);
    float oneMinusMetalness = 1.0 - material.metalness;

    vec4 result = CalculateAmbientLighting(roughness, texColor);

    vec3 multiBounce = (vec3(1.0) - F0) * oneMinusMetalness;
    result.rgb += multiBounce * ambientLight.rgb * 0.1;

    for (int i = 0; i < lightsCount; i++) {
        result += CalculateLight(lights[i], viewDir, fragNormal, fragPosition, texColor, F0, material, T, B, roughness);
    }

    return toneMapACES(result);
}

// [ INSERT GENERATED CODE BELOW ]

void main() {
    mediump vec2 texCoord = fragTexCoord * tiling;
    highp vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 dp1 = dFdx(fragPosition);
    vec3 dp2 = dFdy(fragPosition);
    vec2 duv1 = dFdx(fragTexCoord);
    vec2 duv2 = dFdy(fragTexCoord);

    vec3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 bitangent = normalize(dp2 * duv1.x - dp1 * duv2.x);

    vec4 texColor = diffuseMapReady ? texture(texture0, texCoord) : vec4(1.0);

    vec3 norm = fragNormal;
    if (normalMapReady) {
        vec3 normalMap = texture(texture2, texCoord).rgb;
        mat3 TBN = mat3(tangent, bitangent, fragNormal);
        norm = normalize(TBN * (normalMap * 2.0 - 1.0));
    }

    float roughness = roughnessMapReady ? texture(texture3, texCoord).r : surfaceMaterial.roughness;
    float ao = aoMapReady ? texture(texture4, texCoord).r : 1.0;

    vec4 lighting = CalculateLighting(fragPosition, norm, viewDir, texCoord, surfaceMaterial, texColor, tangent, bitangent, roughness);

    if (surfaceMaterial.transmission > 0.0) {
        vec3 transmissionColor = calculateChromaticAberration(viewDir, surfaceMaterial.ior);
        vec3 scattering = calculateVolumetricScattering(-viewDir, norm, surfaceMaterial.thickness);
        lighting.rgb = mix(lighting.rgb, transmissionColor * scattering, surfaceMaterial.transmission);
    }

    if (aoMapReady && surfaceMaterial.aoStrength > 0.0) {
        lighting.rgb *= mix(1.0, ao, surfaceMaterial.aoStrength);
    }

    finalColor = lighting;
}