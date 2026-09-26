#ifndef LIT_SHADING_GLSL
#define LIT_SHADING_GLSL

#include "brdf.glsl"
#include "scene.glsl"
#include "material.glsl"
#include "lights.glsl"
#include "shadows.glsl"

layout(std430) readonly buffer IrradianceBuffer {
    vec4 shCoefficients[9];
};

uniform samplerCube u_prefilteredEnv;
uniform sampler2D u_dfgLut;

const float ENV_MIP_COUNT = 6.0;
const float ENV_DECODE = 1000.0;

struct ShadingData {
    vec3 position;
    vec3 N;
    vec3 V;
    vec3 R;
    float NoV;
    vec3 baseColor;
    float metallic;
    float perceptualRoughness;
    float roughness;
    vec3 diffuseColor;
    vec3 f0;
    vec3 dfg;
    vec3 energyCompensation;
    float occlusion;
    float specularOcclusion;
    float clearcoat;
    float clearcoatPerceptualRoughness;
    float clearcoatRoughness;
    float anisotropy;
    vec3 T;
    vec3 B;
    vec3 sheenColor;
    float sheenRoughness;
    float sheenScaling;
    vec3 subsurfaceColor;
    float subsurface;
    float thickness;
    float transmission;
    float ior;
};

ShadingData prepareShading(vec3 position, vec3 N, vec3 baseColor, float metallic, float perceptualRoughness, float occlusion, float tangentAngle, uint materialId) {
    MaterialData m = materials[materialId];
    ShadingData s;
    s.position = position;
    s.N = N;
    s.V = normalize(viewPos - position);
    s.NoV = max(dot(N, s.V), 1e-4);
    s.R = reflect(-s.V, N);
    s.baseColor = baseColor;
    s.metallic = metallic;
    s.perceptualRoughness = clamp(perceptualRoughness, 0.045, 1.0);
    s.roughness = s.perceptualRoughness * s.perceptualRoughness;
    float reflectance = m.pbr.z;
    s.f0 = vec3(0.16 * reflectance * reflectance) * (1.0 - metallic) + baseColor * metallic;
    s.diffuseColor = baseColor * (1.0 - metallic);
    s.dfg = textureLod(u_dfgLut, vec2(s.NoV, s.perceptualRoughness), 0.0).rgb;
    s.energyCompensation = renderFlags.z > 0.5 ? 1.0 + s.f0 * (1.0 / max(s.dfg.x + s.dfg.y, 1e-3) - 1.0) : vec3(1.0);
    s.occlusion = occlusion;
    s.specularOcclusion = specularOcclusionLagarde(s.NoV, occlusion, s.roughness);
    s.clearcoat = m.coat.x;
    s.clearcoatPerceptualRoughness = clamp(m.coat.y, 0.089, 1.0);
    s.clearcoatRoughness = s.clearcoatPerceptualRoughness * s.clearcoatPerceptualRoughness;
    s.anisotropy = m.coat.z;
    vec3 t, b;
    orthonormalBasis(N, t, b);
    float angle = (tangentAngle + m.coat.w) * TWO_PI;
    s.T = normalize(t * cos(angle) + b * sin(angle));
    s.B = cross(N, s.T);
    s.sheenColor = m.sheen.rgb;
    s.sheenRoughness = clamp(m.sheen.w, 0.07, 1.0);
    s.sheenScaling = 1.0 - max3(s.sheenColor) * textureLod(u_dfgLut, vec2(s.NoV, s.sheenRoughness), 0.0).b;
    s.subsurfaceColor = m.subsurface.rgb;
    s.subsurface = m.subsurface.w;
    s.thickness = m.transmission.z;
    s.transmission = m.transmission.x;
    s.ior = m.transmission.y;
    return s;
}

vec3 specularLobe(ShadingData s, vec3 L, vec3 H, float NoL, float NoH, float LoH) {
    float D;
    float V;
    if (abs(s.anisotropy) > 0.01) {
        float at = max(s.roughness * (1.0 + s.anisotropy), 0.002);
        float ab = max(s.roughness * (1.0 - s.anisotropy), 0.002);
        D = D_GGX_Anisotropic(at, ab, dot(s.T, H), dot(s.B, H), NoH);
        V = V_SmithGGXCorrelated_Anisotropic(at, ab, dot(s.T, s.V), dot(s.B, s.V), dot(s.T, L), dot(s.B, L), s.NoV, NoL);
    } else {
        D = D_GGX(NoH, s.roughness);
        V = V_SmithGGXCorrelated(s.NoV, NoL, s.roughness);
    }
    vec3 F = F_Schlick(s.f0, saturate(dot(s.f0, vec3(16.5))), LoH);
    return D * V * F * s.energyCompensation;
}

vec3 surfaceShading(ShadingData s, vec3 L, vec3 lightColor, float visibility) {
    vec3 H = normalize(s.V + L);
    float NoLRaw = dot(s.N, L);
    float NoL = saturate(NoLRaw);
    float NoH = saturate(dot(s.N, H));
    float LoH = saturate(dot(L, H));
    vec3 color = vec3(0.0);
    if (NoL > 0.0) {
        vec3 Fr = specularLobe(s, L, H, NoL, NoH, LoH);
        vec3 Fd = s.diffuseColor * Fd_Burley(s.NoV, NoL, LoH, s.roughness);
        color = (Fd + Fr) * NoL;
        if (max3(s.sheenColor) > 0.0) {
            vec3 Fs = s.sheenColor * D_Charlie(s.sheenRoughness, NoH) * V_Neubelt(s.NoV, NoL);
            color = color * s.sheenScaling + Fs * NoL;
        }
        if (s.clearcoat > 0.0) {
            float Fc = F_Schlick(0.04, 1.0, LoH) * s.clearcoat;
            float Frc = D_GGX(NoH, s.clearcoatRoughness) * V_Kelemen(LoH) * Fc;
            color = color * (1.0 - Fc) + vec3(Frc * NoL);
        }
    }
    color *= visibility;
    if (s.subsurface > 0.0) {
        float wrap = 0.5;
        float NoLWrapped = saturate((NoLRaw + wrap) / ((1.0 + wrap) * (1.0 + wrap)));
        float scatterVoH = saturate(dot(s.V, -L));
        float forwardScatter = exp2(scatterVoH * 12.0 - 12.0);
        float backScatter = saturate(NoLRaw * s.thickness + (1.0 - s.thickness)) * 0.5;
        float transmitted = mix(backScatter, 1.0, forwardScatter) * (1.0 - s.thickness);
        color = mix(color, s.diffuseColor * INV_PI * NoLWrapped * visibility, s.subsurface * 0.5);
        color += s.subsurfaceColor * s.subsurface * transmitted * INV_PI * mix(visibility, 1.0, 0.5);
    }
    return color * lightColor;
}

vec3 evaluatePunctual(ShadingData s, GpuLight light) {
    vec3 toLight = light.positionRange.xyz - s.position;
    float d2 = dot(toLight, toLight);
    vec3 L = toLight * inversesqrt(max(d2, 1e-8));
    float attenuation = distanceAttenuation(d2, light.params.z);
    if (light.colorType.w > 1.5) attenuation *= spotAttenuation(L, light);
    if (attenuation <= 0.0) return vec3(0.0);
    return surfaceShading(s, L, light.colorType.rgb * attenuation, 1.0);
}

vec3 evaluateLights(ShadingData s, vec2 pixel, float viewDepth) {
    uint cluster = clusterIndex(pixel, viewDepth);
    uint base = cluster * CLUSTER_STRIDE;
    uint count = min(clusterLights[base], CLUSTER_STRIDE - 1u);
    vec3 color = vec3(0.0);
    for (uint i = 0u; i < count; ++i) color += evaluatePunctual(s, lights[clusterLights[base + 1u + i]]);
    return color;
}

vec3 evaluateSun(ShadingData s, vec2 pixel, out float visibility) {
    vec3 L = sunDirection.xyz;
    float NoL = dot(s.N, L);
    visibility = 0.0;
    if (sunIlluminance.w <= 0.0) return vec3(0.0);
    if (NoL > 0.0 || s.subsurface > 0.0) visibility = sunShadow(s.position, s.N, max(NoL, 0.05), pixel);
    return surfaceShading(s, L, sunIlluminance.rgb, visibility);
}

vec3 irradianceSH(vec3 n) {
    vec3 r = shCoefficients[0].rgb * 0.282095;
    r += shCoefficients[1].rgb * 0.488603 * n.y;
    r += shCoefficients[2].rgb * 0.488603 * n.z;
    r += shCoefficients[3].rgb * 0.488603 * n.x;
    r += shCoefficients[4].rgb * 1.092548 * n.x * n.y;
    r += shCoefficients[5].rgb * 1.092548 * n.y * n.z;
    r += shCoefficients[6].rgb * 0.315392 * (3.0 * n.z * n.z - 1.0);
    r += shCoefficients[7].rgb * 1.092548 * n.x * n.z;
    r += shCoefficients[8].rgb * 0.546274 * (n.x * n.x - n.y * n.y);
    return max(r, vec3(0.0)) * ENV_DECODE * skyGradient[1].w;
}

vec3 prefilteredRadiance(vec3 R, float perceptualRoughness) { return textureLod(u_prefilteredEnv, R, perceptualRoughness * (ENV_MIP_COUNT - 1.0)).rgb * ENV_DECODE * skyGradient[1].w; }

vec3 evaluateIBL(ShadingData s, vec4 ssr) {
    vec3 irradiance = irradianceSH(s.N);
    vec3 R = s.R;
    if (abs(s.anisotropy) > 0.01) {
        vec3 direction = s.anisotropy >= 0.0 ? s.B : s.T;
        vec3 t = cross(direction, s.V);
        vec3 bent = cross(t, direction);
        float bend = abs(s.anisotropy) * saturate(5.0 * s.perceptualRoughness);
        R = reflect(-s.V, normalize(mix(s.N, bent, bend)));
    }
    vec3 radiance = prefilteredRadiance(R, s.perceptualRoughness);
    radiance = mix(radiance, ssr.rgb, saturate(ssr.a));
    vec3 FssEss = s.f0 * s.dfg.x + s.dfg.y;
    float Ess = s.dfg.x + s.dfg.y;
    float Ems = 1.0 - Ess;
    vec3 Favg = s.f0 + (1.0 - s.f0) / 21.0;
    vec3 Fms = renderFlags.z > 0.5 ? FssEss * Favg / (1.0 - Ems * Favg) : vec3(0.0);
    vec3 specular = (FssEss * radiance + Fms * Ems * irradiance) * s.specularOcclusion;
    vec3 kD = s.diffuseColor * max(1.0 - FssEss - Fms * Ems, vec3(0.0));
    vec3 diffuse = kD * irradiance * gtaoMultiBounce(s.occlusion, s.diffuseColor);
    vec3 color = diffuse + specular;
    if (max3(s.sheenColor) > 0.0) color = color * s.sheenScaling + s.sheenColor * prefilteredRadiance(s.R, s.sheenRoughness) * textureLod(u_dfgLut, vec2(s.NoV, s.sheenRoughness), 0.0).b * s.specularOcclusion;
    if (s.clearcoat > 0.0) {
        float Fc = F_Schlick(0.04, 1.0, s.NoV) * s.clearcoat;
        color = color * (1.0 - Fc) + prefilteredRadiance(s.R, s.clearcoatPerceptualRoughness) * Fc * s.specularOcclusion;
    }
    if (s.subsurface > 0.0) color += s.subsurfaceColor * s.subsurface * irradianceSH(-s.V) * (1.0 - s.thickness) * 0.5;
    return color;
}

#endif
