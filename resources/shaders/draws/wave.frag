#version 450
#extension GL_GOOGLE_include_directive : require
#include "lit.glsl"

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_position;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(in_normal);
    float light = max(dot(n, sunDirection.xyz), 0.0);
    vec3 albedo = vec3(0.05, 0.35, 0.55);
    vec3 radiance = albedo * INV_PI * (sunIlluminance.rgb * light + vec3(2500.0));
    out_color = vec4(litExpose(radiance), 1.0);
}
