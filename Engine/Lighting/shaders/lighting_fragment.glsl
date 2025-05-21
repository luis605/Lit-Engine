/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core
#pragma optimize(on)

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragNormal;

layout(location = 0) out vec4 finalColor;
layout(location = 1) out vec4 finalNormal;


layout(location = 13) uniform vec3 viewPos;

vec3 fragView;

// PBR Textures
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform sampler2D texture4;
uniform sampler2D texture5;
uniform sampler2D brdfLUT;
uniform samplerCube irradiance;

// Constants
#define PI 3.14159265359
#define INV_PI 0.31830988618
#define INV_EIGHT 0.125
#define EPSILON 1e-5
const float MIN_ROUGHNESS = 0.045;
const vec3 DEFAULT_DIELECTRIC_F0 = vec3(0.04);

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
    vec4 params;  // params.x = attenuation coeff, params.y = intensity, params.z = SPOT inner cone angle cos
                // params.w = SPOT outer cone angle cos. For point lights, params.w is radius
};

layout(std430, binding = 0) buffer LightsBuffer {
    Light lights[];
};

layout(location = 14) uniform int lightsCount;

float saturate(const float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturate(const vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

float pow5(const float value) {
    float x = value * value;
    return x * x * value;
}

float DistributionGGX(const float NdotH, const float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float NdotH2 = NdotH * NdotH;

    const float num = a2;
    float den = (NdotH2 * (a2 - 1.0) + 1.0);
    den = PI * den * den;
    return num / max(den, EPSILON);
}

float GeometrySchlickGGX(const float NdotV, const float roughness) {
    const float r = (roughness + 1.0);
    const float k = (r * r) * INV_EIGHT;
    const float num = NdotV;
    const float den = NdotV * (1.0 - k) + k;
    return num / max(den, EPSILON);
}

float GetSpotlightFactor(const vec3 spotParams /*theta, outerConeCos, innerConeCos*/) {
    const float lo = min(spotParams.y, spotParams.z);
    const float hi = max(spotParams.y, spotParams.z);

    return smoothstep(lo, hi, spotParams.x);
}

vec3 CalculateDirectLighting(const int lightIndex, const vec3 N, const vec3 F0, const vec3 albedo,
                             const vec3 specularEnv, const vec3 pbrParams /*roughness, metalness, NdotV*/) {
    const Light light = lights[lightIndex];

    const float isDir   = float(light.type == LIGHT_DIRECTIONAL);
    const float isPoint = float(light.type == LIGHT_POINT);
    const float isSpot  = float(light.type == LIGHT_SPOT);

    const vec3  rawDir = mix(light.position - fragPosition, -light.direction, isDir);
    const vec3  L      = normalize(rawDir);
    const float NdotL  = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    const vec3  H     = normalize(fragView + L);
    const float HdotV = max(dot(H, fragView), 0.0);

    const float radius  = light.params.w * isPoint;

    mediump const float distance  = length(light.position - fragPosition);
    lowp const    float fragDist  = length(viewPos - fragPosition);
    mediump       float distAtt   = 1.0 / (1.0 + light.params.x * distance * distance);

    if (radius > 0.0) {
        float distOverRadiusSq = (distance * distance) / (radius * radius);
        float factor = distOverRadiusSq * distOverRadiusSq;
        float smoothFactor = saturate(1.0 - factor);
        distAtt *= smoothFactor * smoothFactor;
    }

    mediump const float spotFactor = mix(
        1.0,
        GetSpotlightFactor(vec3(dot(-L, light.direction), light.params.w, light.params.z)),
        isSpot
    );

    mediump const float attenuation = mix(distAtt * spotFactor, 1.0, isDir);
    mediump const vec3 lightColor   = light.color.rgb * light.params.y;

    const vec3  F = F0 + (vec3(1.0) - F0) * pow5(clamp(1.0 - HdotV, 0.0, 1.0));
    const vec3 kS = F;
    const vec3 kD = (vec3(1.0) - kS) * (1.0 - pbrParams.y);
    const vec3 diff     = kD * albedo * INV_PI;

    if ((radius > 0.0 && distance > radius) || fragDist > 100.0) {
        vec3 Ldir   = normalize(isDir > 0.5 ? -light.direction : rawDir);
        float NL    = max(dot(N, Ldir), 0.0);
        vec3 diffuse = albedo * lightColor * NL * attenuation;

        vec3 ambient = texture(irradiance, N).rgb * albedo * 0.1;

        return (diffuse + ambient) * 0.3;
    }

    const float D = DistributionGGX(max(dot(N, H), 0.0), pbrParams.x);
    const float ggxV = GeometrySchlickGGX(pbrParams.z, pbrParams.x);
    const float ggxL = GeometrySchlickGGX(NdotL, pbrParams.x);
    const float G = ggxV * ggxL; // Geometry smith inlined

    const float denom   = 1.0 / (4.0 * pbrParams.z * NdotL + EPSILON);
    const vec3 specBRDF = D * G * F * denom;

    const vec3 Lo = (diff + specBRDF * specularEnv) * lightColor * NdotL * attenuation;

    return Lo;
}

vec3 CalculateSpecularIBL(const vec3 F0, const float roughness, const vec3 N, const float NdotV, const vec3 specularEnv) {
    mediump const vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    return specularEnv * (F0 * brdf.x + brdf.y);
}

vec3 toneMapACES(mediump const vec3 color) {
    mediump const float a = 2.51;
    mediump const float b = 0.03;
    mediump const float c = 2.43;
    mediump const float d = 0.59;
    mediump const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

mediump float invSqrtFast(float x) {
    mediump float xhalf = 0.5 * x;
    int i = floatBitsToInt(x);
    i = 0x5f3759df - (i >> 1);
    x = intBitsToFloat(i);
    return x * (1.5 - xhalf * x * x);
}

// [ INSERT GENERATED CODE BELOW ]

void main() {
#ifdef TILING
    mediump const vec2 texCoord = fragTexCoord * tiling;
#else
    mediump const vec2 texCoord = fragTexCoord;
#endif

    vec3 N       = fragNormal;
    fragView     = normalize(viewPos - fragPosition);

#ifdef NORMAL
    const vec3 dp1   = dFdx(fragPosition);
    const vec3 dp2   = dFdy(fragPosition);
    const vec2 duv1  = dFdx(texCoord);
    const vec2 duv2  = dFdy(texCoord);

    const vec3 dPds  = dp1 * duv2.t - dp2 * duv1.t;
    const vec3 dPdt  = dp2 * duv1.s - dp1 * duv2.s;

    const vec3 T     = normalize(dPds - N * dot(N, dPds));
    const vec3 B     = normalize(cross(N, T));
    const mat3 TBN   = mat3(T, B, N);

    const vec2 nxy = texture(texture2, texCoord).rg * 2.0 - 1.0;
    vec3 nm = calcNormalMap(vec4(nxy, 1.0, 1.0)).rgb;
    const float xyLenSq = dot(nm.xy, nm.xy);
    nm.z = invSqrtFast(max(1.0 - xyLenSq, EPSILON));



    N = normalize(TBN * nm);
#endif


    vec4 albedoSample = vec4(1.0);
#ifdef ALBEDO
    albedoSample = calcDiffuseMap(texture(texture0, texCoord));
#endif
    vec3 albedo = albedoSample.rgb;
    float alpha = albedoSample.a;

    float roughness = 0.5;
#ifdef ROUGHNESS
    roughness = calcRoughnessMap(texture(texture3, texCoord)).r;
#endif
    roughness = max(roughness, MIN_ROUGHNESS);

    float metalness = 0.0;
#ifdef METALNESS
    metalness = calcMetallicMap(texture(texture1, texCoord)).r;
#endif

    mediump float ao = 1.0;
#ifdef AO
    ao = calcAmbientOcclusionMap(texture(texture4, texCoord)).r;
#endif

    const float specularIntensity = 1.0;

    const vec3 dielectricF0 = DEFAULT_DIELECTRIC_F0 * specularIntensity;
    const vec3 F0 = mix(dielectricF0, albedo, metalness);

    const float Ni = dot(N, -fragView);
    const vec3  R  = -fragView - 2.0 * Ni * N;

    const vec3 specularEnv = texture(irradiance, R).rgb;
    const float NdotV = saturate(dot(N, fragView));

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < lightsCount; ++i) {
        Lo += CalculateDirectLighting(i, N, F0, albedo, specularEnv, vec3(roughness, metalness, NdotV));
    }

    const vec3 F_ibl = F0 + (vec3(1.0) - F0) * pow5(clamp(1.0 - NdotV, 0.0, 1.0));

    const vec3 kD_ibl = (vec3(1.0) - F_ibl) * (1.0 - metalness);
    const vec3 diffuseIBL = texture(irradiance, N).rgb * albedo * kD_ibl;

    const vec3 specularIBL = CalculateSpecularIBL(F0, roughness, N, NdotV, specularEnv);

    const vec3 ambient = (diffuseIBL + specularIBL) * ao;
    Lo += ambient;

#ifdef EMISSION_MAP
    Lo += calcEmissionMap(texture(texture5, texCoord));
#endif

    mediump const vec3 hdrColor = toneMapACES(exposure * Lo);

    finalColor = vec4(hdrColor, alpha);
    finalNormal = vec4(N,1);
}