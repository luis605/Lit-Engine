#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "lighting.glsl"

layout(location = 6) flat in float in_alpha;
layout(location = 0) out vec4 out_accumulation;
layout(location = 1) out float out_revealage;

float blendWeight(float alpha, float viewDistance) {
    float falloff = 10.0 / (1e-5 + pow(viewDistance / 5.0, 2.0) + pow(viewDistance / 200.0, 6.0));
    return alpha * clamp(falloff, 1e-2, 3e3);
}

void main() {
    vec3 N = surfaceNormal();
    vec3 color = mix(vec3(0.3, 0.6, 1.0), in_material.rgb, in_material.a) * surfaceLighting(N, in_fragPos);
    float alpha = clamp(in_alpha, 0.0, 1.0);
    out_accumulation = vec4(color * alpha, alpha) * blendWeight(alpha, distance(viewPos, in_fragPos));
    out_revealage = alpha;
}
