#ifndef LIT_SHADOWS_GLSL
#define LIT_SHADOWS_GLSL

#include "scene.glsl"

uniform sampler2DArrayShadow u_shadowMap;
uniform sampler2DArray u_shadowDepth;
uniform sampler2DArrayShadow u_localShadowMap;

const int PCF_SAMPLES = 24;
const int BLOCKER_SAMPLES = 12;
const int LOCAL_PCF_SAMPLES = 12;

float shadowNoise(vec2 pixel) { return interleavedGradientNoise(pixel, 0.0); }

vec3 cascadeCoord(int cascade, vec3 worldPos) {
    vec4 clip = cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    return vec3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}

float cascadeVisibility(int cascade, vec3 worldPos, vec3 N, float NoL, float noise) {
    vec4 info = cascadeInfo[cascade];
    float texelWorld = info.x;
    float depthRange = info.y;
    float slope = sqrt(max(1.0 - NoL * NoL, 0.0)) / max(NoL, 0.05);
    vec3 offsetPos = worldPos + N * texelWorld * shadowParams.w * (0.6 + 0.8 * slope);
    vec3 coord = cascadeCoord(cascade, offsetPos);
    if (any(lessThan(coord.xy, vec2(0.0))) || any(greaterThan(coord.xy, vec2(1.0))) || coord.z >= 1.0) return 1.0;
    float texel = shadowParams.y;
    float bias = shadowParams2.y * texelWorld / max(depthRange, 1e-3) * (1.0 + slope);
    float reference = coord.z - bias;
    float phi = hash12(floor(coord.xy / texel) + info.z * 17.0) * TWO_PI;
    float filterRadius = 1.5;
    if (shadowParams2.w > 0.5) {
        float lightSizeTexels = shadowParams.z * 0.0093 * depthRange / texelWorld;
        float searchRadius = clamp(lightSizeTexels * 0.5, 2.0, 24.0);
        float blockerSum = 0.0;
        float blockers = 0.0;
        for (int i = 0; i < BLOCKER_SAMPLES; ++i) {
            vec2 offset = vogelDisk(i, BLOCKER_SAMPLES, phi) * searchRadius * texel;
            float d = textureLod(u_shadowDepth, vec3(coord.xy + offset, info.z), 0.0).r;
            if (d < reference) {
                blockerSum += d;
                blockers += 1.0;
            }
        }
        if (blockers < 0.5) return 1.0;
        float blockerDepth = blockerSum / blockers;
        float penumbraWorld = (reference - blockerDepth) * depthRange;
        filterRadius = clamp(penumbraWorld * shadowParams.z * 0.0093 / texelWorld * 2.0, 1.0, 20.0);
    } else {
        filterRadius = 1.5 * max(shadowParams.z, 0.3);
    }
    float sum = 0.0;
    for (int i = 0; i < PCF_SAMPLES; ++i) {
        vec2 offset = vogelDisk(i, PCF_SAMPLES, phi) * filterRadius * texel;
        sum += texture(u_shadowMap, vec4(coord.xy + offset, info.z, reference));
    }
    return sum / float(PCF_SAMPLES);
}

float sunShadow(vec3 worldPos, vec3 N, float NoL, vec2 pixel) {
    if (shadowParams.x < 0.5) return 1.0;
    float viewDepth = -(view * vec4(worldPos, 1.0)).z;
    int count = int(shadowParams2.x + 0.5);
    int cascade = -1;
    for (int c = 0; c < count; ++c) {
        if (viewDepth < cascadeSplits[c]) {
            cascade = c;
            break;
        }
    }
    if (cascade < 0) return 1.0;
    float noise = shadowNoise(pixel);
    float visibility = cascadeVisibility(cascade, worldPos, N, NoL, noise);
    float cascadeStart = cascade == 0 ? 0.0 : cascadeSplits[cascade - 1];
    float cascadeEnd = cascadeSplits[cascade];
    float blend = saturate((viewDepth - mix(cascadeStart, cascadeEnd, 0.88)) / max(cascadeEnd * 0.12, 1e-3));
    if (blend > 0.0 && cascade + 1 < count) {
        visibility = mix(visibility, cascadeVisibility(cascade + 1, worldPos, N, NoL, noise), blend);
    } else if (blend > 0.0) {
        visibility = mix(visibility, 1.0, blend);
    }
    return visibility;
}

float localShadow(int base, bool point, vec3 lightPos, vec3 worldPos, vec3 N, vec3 L, float NoL, float noise) {
    if (base < 0) return 1.0;
    int slot = base;
    if (point) {
        vec3 d = worldPos - lightPos;
        vec3 a = abs(d);
        slot += a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0 : 1) : a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
    }
    mat4 m = localShadowMatrices[slot];
    float w = (m * vec4(worldPos, 1.0)).w;
    float tanHalf = 1.0 / length(vec3(m[0][0], m[1][0], m[2][0]));
    float texelWorld = 2.0 * max(w, 1e-3) * tanHalf * localShadowParams.y;
    float slope = sqrt(max(1.0 - NoL * NoL, 0.0)) / max(NoL, 0.05);
    vec3 offsetPos = worldPos + N * texelWorld * localShadowParams.w * (0.6 + 0.8 * min(slope, 4.0)) + L * texelWorld * localShadowParams2.x * (1.0 + min(slope, 4.0));
    vec4 clip = m * vec4(offsetPos, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z >= 1.0) return 1.0;
    float radius = (1.0 + localShadowParams.z) * localShadowParams.y;
    float phi = hash12(floor(uv / localShadowParams.y) + float(slot) * 17.0) * TWO_PI;
    float sum = 0.0;
    for (int i = 0; i < LOCAL_PCF_SAMPLES; ++i) {
        vec2 offset = vogelDisk(i, LOCAL_PCF_SAMPLES, phi) * radius;
        sum += texture(u_localShadowMap, vec4(clamp(uv + offset, vec2(0.0), vec2(1.0)), float(slot), ndc.z));
    }
    return sum / float(LOCAL_PCF_SAMPLES);
}

#endif
