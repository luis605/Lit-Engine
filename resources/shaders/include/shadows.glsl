#ifndef LIT_SHADOWS_GLSL
#define LIT_SHADOWS_GLSL

#include "scene.glsl"

uniform sampler2DArrayShadow u_shadowMap;
uniform sampler2DArray u_shadowDepth;

const int PCF_SAMPLES = 16;
const int BLOCKER_SAMPLES = 8;

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
    float phi = noise * TWO_PI;
    float filterRadius = 1.5;
    if (shadowParams2.w > 0.5) {
        float lightSizeTexels = shadowParams.z * 0.0093 * depthRange / texelWorld;
        float searchRadius = clamp(lightSizeTexels * 0.5, 2.0, 24.0);
        float blockerSum = 0.0;
        float blockers = 0.0;
        for (int i = 0; i < BLOCKER_SAMPLES; ++i) {
            vec2 offset = vogelDisk(i, BLOCKER_SAMPLES, phi) * searchRadius * texel;
            float d = textureLod(u_shadowDepth, vec3(coord.xy + offset, float(cascade)), 0.0).r;
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
        sum += texture(u_shadowMap, vec4(coord.xy + offset, float(cascade), reference));
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
    float noise = interleavedGradientNoise(pixel, frameIndex());
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

#endif
