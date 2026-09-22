#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "lighting.glsl"

layout(location = 0) out vec4 out_color;

void main() {
    vec3 N = surfaceNormal();
    vec3 baseColor = mix(abs(N) * 0.7 + 0.3, in_material.rgb, in_material.a);
    out_color = vec4(baseColor * surfaceLighting(N, in_fragPos), 1.0);
}
