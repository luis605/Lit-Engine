#ifndef LIT_GI_UPSAMPLE_GLSL
#define LIT_GI_UPSAMPLE_GLSL

#include "scene.glsl"

uniform sampler2D u_gi;
uniform sampler2D u_aoDepth;

vec3 upsampleGi(ivec2 pixel, float centerDepth) {
    ivec2 halfSize = textureSize(u_aoDepth, 0);
    vec2 position = vec2(pixel) * 0.5;
    ivec2 base = ivec2(floor(position));
    vec2 f = position - vec2(base);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    float tolerance = max(centerDepth * 0.03, 1e-3);
    for (int i = 0; i < 4; ++i) {
        ivec2 offset = ivec2(i & 1, i >> 1);
        ivec2 p = min(base + offset, halfSize - 1);
        float bilinear = (offset.x == 0 ? 1.0 - f.x : f.x) * (offset.y == 0 ? 1.0 - f.y : f.y);
        float w = (bilinear + 1e-3) * exp(-abs(texelFetch(u_aoDepth, p, 0).r - centerDepth) / tolerance);
        sum += texelFetch(u_gi, p, 0).rgb * w;
        total += w;
    }
    return sum / max(total, 1e-6);
}

#endif
