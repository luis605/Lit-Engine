#ifndef LIT_BRDF_GLSL
#define LIT_BRDF_GLSL

#include "common.glsl"

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f + 1e-7);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5 / (lambdaV + lambdaL + 1e-5);
}

float D_GGX_Anisotropic(float at, float ab, float ToH, float BoH, float NoH) {
    float a2 = at * ab;
    vec3 d = vec3(ab * ToH, at * BoH, a2 * NoH);
    float d2 = dot(d, d);
    float b2 = a2 / max(d2, 1e-7);
    return a2 * b2 * b2 * INV_PI;
}

float V_SmithGGXCorrelated_Anisotropic(float at, float ab, float ToV, float BoV, float ToL, float BoL, float NoV, float NoL) {
    float lambdaV = NoL * length(vec3(at * ToV, ab * BoV, NoV));
    float lambdaL = NoV * length(vec3(at * ToL, ab * BoL, NoL));
    return 0.5 / (lambdaV + lambdaL + 1e-5);
}

vec3 F_Schlick(vec3 f0, float f90, float VoH) { return f0 + (vec3(f90) - f0) * pow5(1.0 - VoH); }
float F_Schlick(float f0, float f90, float VoH) { return f0 + (f90 - f0) * pow5(1.0 - VoH); }

float Fd_Burley(float NoV, float NoL, float LoH, float roughness) {
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    return F_Schlick(1.0, f90, NoL) * F_Schlick(1.0, f90, NoV) * INV_PI;
}

float D_Charlie(float roughness, float NoH) {
    float invAlpha = 1.0 / max(roughness, 1e-3);
    float cos2h = NoH * NoH;
    float sin2h = max(1.0 - cos2h, 0.0078125);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / TWO_PI;
}

float V_Neubelt(float NoV, float NoL) { return saturate(1.0 / (4.0 * (NoL + NoV - NoL * NoV) + 1e-5)); }

float V_Kelemen(float LoH) { return 0.25 / max(LoH * LoH, 1e-4); }

vec3 gtaoMultiBounce(float visibility, vec3 albedo) {
    vec3 a = 2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c = 2.7552 * albedo + 0.6903;
    return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}

float specularOcclusionLagarde(float NoV, float ao, float roughness) { return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao); }

vec3 importanceSampleGGX(vec2 xi, float a) {
    float phi = TWO_PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

vec2 hammersley(uint i, uint n) {
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(bits) * 2.3283064365386963e-10);
}

#endif
