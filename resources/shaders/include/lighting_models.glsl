#ifndef LIT_LIGHTING_MODELS_GLSL
#define LIT_LIGHTING_MODELS_GLSL

#include "shading.glsl"

#ifndef LIT_STANDARD
#define LIT_STANDARD 0
#define LIT_UNLIT 1
#define LIT_TOON 2
#endif

struct LitToon {
    float bands;
    float rim;
    float rimWidth;
};

int litDecodeModel(float packedModel) { return int(packedModel * 3.0 + 0.5); }

LitToon litDecodeToon(uvec2 packedToon) { return LitToon(float(max(packedToon.x, 1u)), float(packedToon.y >> 4u) / 15.0, float(packedToon.y & 15u) / 15.0); }

float litToonStep(float x, float bands) { return min(floor(saturate(x) * bands + 0.5) / bands, 1.0); }

vec3 litToonLight(ShadingData s, vec3 L, vec3 lightColor, float visibility, float bands) {
    float lit = litToonStep(saturate(dot(s.N, L)) * visibility, bands);
    if (lit <= 0.0) return vec3(0.0);
    vec3 H = normalize(s.V + L);
    float shininess = 2.0 / max(s.roughness * s.roughness, 1e-3);
    float highlight = step(0.5, pow(saturate(dot(s.N, H)), shininess)) * (1.0 - s.perceptualRoughness);
    return (s.diffuseColor * lit + vec3(highlight)) * INV_PI * lightColor;
}

vec3 litToonDirect(ShadingData s, LitToon toon, vec2 pixel, float viewDepth, out float visibility) {
    vec3 color = vec3(0.0);
    visibility = 0.0;
    if (sunIlluminance.w > 0.0) {
        float NoL = dot(s.N, sunDirection.xyz);
        if (NoL > 0.0) visibility = sunShadow(s.position, s.N, max(NoL, 0.05), pixel);
        color += litToonLight(s, sunDirection.xyz, sunIlluminance.rgb, visibility, toon.bands);
    }
    uint base = clusterIndex(pixel, viewDepth) * CLUSTER_STRIDE;
    uint count = min(clusterLights[base], CLUSTER_STRIDE - 1u);
    for (uint i = 0u; i < count; ++i) {
        GpuLight light = lights[clusterLights[base + 1u + i]];
        vec3 toLight = light.positionRange.xyz - s.position;
        float d2 = dot(toLight, toLight);
        vec3 L = toLight * inversesqrt(max(d2, 1e-8));
        float attenuation = distanceAttenuation(d2, light.params.z);
        if (light.colorType.w > 1.5) attenuation *= spotAttenuation(L, light);
        if (attenuation > 0.0) color += litToonLight(s, L, light.colorType.rgb * attenuation, 1.0, toon.bands);
    }
    return color;
}

vec3 litToonAmbient(ShadingData s, LitToon toon, float sunVisibility) {
    vec3 irradiance = irradianceSH(s.N);
    vec3 color = s.diffuseColor * irradiance * s.occlusion;
    float edge = 1.0 - s.NoV;
    float threshold = 1.0 - toon.rimWidth;
    float rim = toon.rim * smoothstep(threshold - 0.04, threshold + 0.04, edge);
    vec3 sunRim = sunIlluminance.w > 0.0 ? sunIlluminance.rgb * INV_PI * mix(0.25, 1.0, sunVisibility) : vec3(0.0);
    return color + rim * (irradiance + sunRim) * mix(vec3(1.0), s.baseColor, 0.5);
}

#endif
