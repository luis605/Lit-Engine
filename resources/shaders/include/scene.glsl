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
    vec4 vsmParams;  // x virtual shadows on, y atlas size (texels), z atlas pages per side, w resolution scale
    vec4 vsmParams2; // x finest mip (0..4)
    vec4 originParams; // xyz world origin mod 4096 (pattern phase), w world origin y (altitude)
    vec4 cellParams;   // x portal cell of this view (-1: local lights are not filtered by cell), y first portal light + 1 (0: none),
                       // z > 0: sun cascades are shared (pick the finest cascade containing the point, up to this view depth)
    vec4 cellTint;     // rgb lighting grade of this view's cell (sun and ambient), w unused
};

float projectedDiameterPx(float radius, float dist) {
    return radius * projection[1][1] * screenParams.y / max(dist, 1e-4);
}

float sceneTime() { return timeParams.x; }
float frameIndex() { return timeParams.z; }
float farPlane() { return timeParams.w; }
// Floating origin: world = local + origin. Procedural world-space patterns sample local + originPhase() (origin modulo 4096, so float
// precision holds and a rebase does not move the pattern when its period divides 4096); altitude-dependent effects use skyViewPos().
vec3 originPhase() { return originParams.xyz; }
vec3 skyViewPos() { return viewPos + vec3(0.0, originParams.w, 0.0); }
int debugViewMode() { return int(exposureParams.w + 0.5); }

// Reversed Z: depth 1 is the near plane, 0 the far plane (sky and cleared pixels hold 0).
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

float linearViewDepth(float depth) {
    vec4 v = invProjection * vec4(0.0, 0.0, depth, 1.0);
    return -v.z / v.w;
}

vec2 projectToUv(vec3 world, mat4 vp, out float depth) {
    vec4 clip = vp * vec4(world, 1.0);
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    depth = ndc.z;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

#endif
