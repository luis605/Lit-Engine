#ifndef LIT_SCENE_GLSL
#define LIT_SCENE_GLSL

#include "common.glsl"

layout(std140) uniform SceneData {
    mat4 projection;
    mat4 view;
    mat4 viewProjection;
    mat4 invView;
    mat4 invProjection;
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 frustumPlanes[6];
    vec3 viewPos;
    float nearPlane;
    vec4 screenParams;
    vec4 timeParams;
    vec4 sunDirection;
    vec4 sunIlluminance;
    vec4 exposureParams;
    vec4 lightParams;
    vec4 clusterParams;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
    vec4 cascadeInfo[4];
    vec4 shadowParams;
    vec4 shadowParams2;
    vec4 atmosphereParams;
    vec4 skyParams;
    vec4 skyParams2;
    vec4 skyGradient[3];
    vec4 renderFlags;
    vec4 aoParams;
    vec4 ssrParams;
    vec4 fogParams;
    vec4 postParams;
    vec4 postParams2;
    mat4 localShadowMatrices[24];
    vec4 localShadowParams;
    vec4 localShadowParams2;
    vec4 giParams;
    vec4 moonDirection;
    vec4 celestialParams;
    vec4 cloudParams;
    vec4 skyParams3;
    vec4 maskParams;
    vec4 maskColor;
    vec4 vignetteParams;
    vec4 bleedParams;
    vec4 qualityParams;
    vec4 qualityParams2;
    vec4 vsmParams;
    vec4 vsmParams2;
    vec4 originParams;
    vec4 cellParams;
    vec4 cellTint;
    ivec4 viewRect;
    ivec4 depthRegion;
};

bool outsideDepthRegion(vec2 pixel) {
    if (depthRegion.z <= 0) return false;
    vec2 low = vec2(depthRegion.xy);
    vec2 high = vec2(depthRegion.zw);
    low += 4.0 * step(vec2(0.5), low);
    high -= 4.0 * step(high + 0.5, screenParams.xy);
    return any(lessThan(pixel, low)) || any(greaterThan(pixel, high));
}

float projectedDiameterPx(float radius, float dist) {
    return radius * projection[1][1] * screenParams.y / max(dist, 1e-4);
}

float sceneTime() { return timeParams.x; }
float frameIndex() { return timeParams.z; }
float farPlane() { return timeParams.w; }
// originPhase() is the origin modulo 4096, so float precision holds and rebases do not move patterns whose period divides 4096.
vec3 originPhase() { return originParams.xyz; }
vec3 skyViewPos() { return viewPos + vec3(0.0, originParams.w, 0.0); }
int debugViewMode() { return int(exposureParams.w + 0.5); }

bool isSkyDepth(float depth) { return depth <= 0.0; }
const float FAR_DEPTH = 0.0;

vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    vec4 world = invViewProjection * ndc;
    return world.xyz / world.w;
}

vec3 viewFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    vec4 v = invProjection * ndc;
    return v.xyz / v.w;
}

vec3 worldViewDirection(vec2 uv) {
    if (projection[3][3] > 0.5) return normalize(-invView[2].xyz);
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    vec3 ray = vec3((ndc + projection[2].xy) / vec2(projection[0][0], projection[1][1]), -1.0);
    return normalize(mat3(invView) * ray);
}

float linearViewDepth(vec2 uv, float depth) { return -viewFromDepth(uv, depth).z; }

vec2 projectToUv(vec3 world, mat4 vp, out float depth) {
    vec4 clip = vp * vec4(world, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    depth = ndc.z;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

#endif
