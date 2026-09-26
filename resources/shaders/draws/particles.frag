#version 450
#extension GL_GOOGLE_include_directive : require
#include "lit.glsl"

layout(location = 0) in vec3 in_color;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 pc = gl_PointCoord * 2.0 - 1.0;
    float falloff = exp(-dot(pc, pc) * 3.0);
    out_color = vec4(litExpose(in_color) * falloff, 0.0);
}
