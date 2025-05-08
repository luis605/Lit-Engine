/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

precision mediump float;

// Input vertex attributes
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Input uniform values
uniform vec3 viewPos;

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
uniform samplerCube irradiance;

// Constants
#define PI 3.14159265359
#define EPSILON 1e-5
#define ENERGY_CONSERVATION_THRESHOLD 0.995
#define INV_PI 0.31830988618
#define EPSILON 1e-5
#define INV_4 0.25
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
    vec3 direction;
    vec4 color;
    vec4 aisr; // aisr.x = attenuation, aisr.y = intensity, aisr.z = specular Strength, aisr.w = radius
};

layout(std430, binding = 0) buffer LightsBuffer {
    Light lights[];
};

uniform int lightsCount;

out vec4 finalColor;

lowp float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

lowp vec3 saturate(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

lowp float pow5(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    return x4 * x; // x^5
}

lowp vec3 Fresnel(float cosTheta, vec3 F0) {
    return fma((vec3(1.0) - F0), vec3(pow5(1.0 - cosTheta)), F0);
}

lowp float DistributionGGX(lowp float NdotH2, lowp float roughness2) {
    lowp float denom = fma(fma(NdotH2, roughness2 - 1.0, 1.0), NdotH2, 1.0);
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

mediump vec3 CookTorrance(vec3 L, vec3 V, vec3 N, vec3 T, vec3 B, float roughness, vec3 F0, float specularAO) {
    // Early exit if specular AO is too low
    mediump float specularAO_mask = step(EPSILON, specularAO);
    mediump vec3 H = normalize(L + V);

    // dots.x = NdotL, dots.y = NdotV, dots.z = NdotH, dots.w = VdotH
    mediump vec4 dots = vec4(dot(N, L), dot(N, V), dot(N, H), dot(V, H));

    mediump float common_div = dots.x * dots.y;
    mediump float commonDiv_mask = step(EPSILON, common_div);
    if (specularAO_mask * commonDiv_mask == 0.0)
        return vec3(0.0);

    // Compute half-vector and related dot products
    mediump float NdotH2 = dots.z * dots.z;
    mediump float roughness2 = roughness * roughness;

    // Fresnel term using the half-vector
    lowp vec3 fresnel = Fresnel(dots.w, F0);

    // Choose between anisotropic and isotropic distributions
    lowp float anisotropyMask = step(EPSILON, roughness);
    lowp float D_aniso = DistributionGGXAnisotropic(NdotH2, H, T, B, roughness, 0.0);
    lowp float D_iso = DistributionGGX(NdotH2, roughness2);

    // Geometry term
    lowp float k = fma(roughness, 0.5, 0.5);
    mediump vec2 DG = vec2(
        mix(D_iso, D_aniso, anisotropyMask),
        GeometrySmith(dots.x, dots.y, k)
    );

    // Main specular calculation
    mediump vec3 specular = (fresnel * DG.x * DG.y * specularAO * INV_4) / max(common_div, EPSILON);

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

vec4 CalculateDiffuseLighting(vec3 norm, vec3 lightDir, vec4 lightColor, vec4 texColor) {
    float NdotL = max(dot(norm, lightDir), 0.0);
    return lightColor * NdotL / PI * texColor;
}

vec4 toneMapFilmic(vec4 hdrColor) {
    vec3 x = max(vec3(0.0), hdrColor.rgb * exposure - vec3(0.004));
    vec3 numerator = x * fma(vec3(6.2), x, vec3(0.5));
    vec3 denominator = fma(x, fma(vec3(6.2), x, vec3(1.7)), vec3(0.06));
    vec3 result = numerator / denominator;

    return vec4(result, hdrColor.a);
}

vec4 toneMapACES(vec4 hdrColor) {
    vec3 x = hdrColor.rgb * exposure;
    vec3 result = ((2.51 * x + 0.03) * x) / (((2.43 * x + 0.59) * x) + 0.14);
    return vec4(clamp(result, 0.0, 1.0), hdrColor.a);
}

vec4 CalculateLight(Light light, vec3 viewDir, vec3 norm, vec3 fragPosition, vec4 texColor, vec3 F0,
                    vec3 T, vec3 B, float roughness, float specular) {

    float attenuation = 1.0;
    float spot = 1.0;

    vec3 toLight = light.position - fragPosition;
    vec3 lightDir = normalize(mix(toLight, -light.direction, step(light.type, LIGHT_DIRECTIONAL)));

    float lightToFragDist = length(toLight);
    float attenuationFactor = PhysicalLightAttenuation(lightToFragDist, light.aisr.w, light.aisr.x);
    float spotFactor = smoothstep(0.6, 0.8, dot(lightDir, light.direction));

    float attenuationMix = mix(spotFactor, 1.0, step(light.type, LIGHT_POINT));
    attenuation = mix(attenuationFactor * attenuationMix, 1.0, step(light.type, LIGHT_DIRECTIONAL));

    vec4 diffuse = CalculateDiffuseLighting(norm, lightDir, light.color, texColor);
    vec3 specularFactor = CookTorrance(lightDir, viewDir, norm, T, B, roughness, F0, light.aisr.z * specular);

    vec3 finalColor = (diffuse.rgb + specularFactor) * (attenuation * spot * light.aisr.y);
    return vec4(finalColor, 1.0);
}

vec4 CalculateLighting(vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec2 texCoord,
                      vec4 texColor, vec3 T, vec3 B, float roughness, float specular, float metalness) {

    vec3 F0 = mix(vec3(0.04), texColor.rgb, metalness);
    float oneMinusMetalness = 1.0 - metalness;

    vec4 result = vec4(0);//texColor;
    result.a = 1;

    vec3 multiBounce = (vec3(1.0) - F0) * oneMinusMetalness;
    result.rgb += multiBounce;

    for (int i = 0; i < lightsCount; i++) {
        result += CalculateLight(lights[i], viewDir, fragNormal, fragPosition, texColor, F0, T, B, roughness, specular);
    }

    return result; //toneMapACES(result);
}

float Q_rsqrt(float number) {
    float x2 = number * 0.5;
    float y  = number;
    uint i = floatBitsToUint(y);
    i = 0x5f3759dfu - (i >> 1);
    y = uintBitsToFloat(i);
    y = y * (1.5 - (x2 * y * y));  // one iteration of Newton's method
    return y;
}

// [ INSERT GENERATED CODE BELOW ]

void main() {
#ifdef TILING
    mediump vec2 texCoord = fragTexCoord * tiling;
#else
    mediump vec2 texCoord = fragTexCoord;
#endif

    highp vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 dp1 = dFdx(fragPosition);
    vec3 dp2 = dFdy(fragPosition);
    vec2 duv1 = dFdx(fragTexCoord);
    vec2 duv2 = dFdy(fragTexCoord);

    vec3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 bitangent = normalize(dp2 * duv1.x - dp1 * duv2.x);

#ifdef ALBEDO
    vec4 texColor = texture(texture0, texCoord);
#else
    vec4 texColor = vec4(vec3(.2), 1);
#endif

#ifdef NORMAL
    mat3 TBN = mat3(tangent, bitangent, fragNormal);

    vec2 nxy = texture(texture2, texCoord).rg * 2.0 - 1.0;
    vec3 nm = calcNormalMap(vec4(nxy, 1.0, 1.0)).rgb;
    nm.z = Q_rsqrt(max(1.0 - dot(nm.xy, nm.xy), 0.0));

    vec3 normal = normalize(TBN * nm);
#else
    vec3 normal = normalize(fragNormal);
#endif

#ifdef ROUGHNESS
    float roughness = texture(texture3, texCoord).r;
#else
    float roughness = 0.5;
#endif

#ifdef SPECULAR
    float specular = texture(texture5, texCoord).r;
#else
    float specular = 0.5;
#endif

#ifdef METALNESS
    float metalness = texture(texture6, texCoord).r;
#else
    float metalness = 0.5;
#endif

    texColor.rgb *= texture(irradiance, normal).rgb;

    vec4 lighting = CalculateLighting(fragPosition, normal, viewDir, texCoord, texColor, tangent, bitangent, roughness, specular, metalness);

#ifdef AMBIENT_OCCLUSION
    lighting.rgb *= texture(texture4, texCoord).r;
#endif

    finalColor = vec4(texture(irradiance, normal).rgb, 1);
//    finalColor = lighting;
}