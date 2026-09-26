#ifndef LIT_CUSTOM_GLSL
#define LIT_CUSTOM_GLSL

#include "scene.glsl"

layout(std140) uniform LitParams {
    vec4 litParams[4];
    vec4 litTimeInfo;
};

layout(std430) readonly buffer ExposureBuffer {
    vec4 exposureData;
};

float litTime() { return litTimeInfo.x; }
float litDeltaTime() { return litTimeInfo.y; }
uint litFrame() { return uint(litTimeInfo.z); }
float litExposure() { return exposureData.x; }
vec3 litExpose(vec3 radiance) { return radiance * exposureData.x; }

#endif
