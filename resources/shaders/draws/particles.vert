#version 450
#extension GL_GOOGLE_include_directive : require
#include "lit.glsl"

struct Particle {
    vec4 position;
    vec4 velocity;
};

layout(std430) readonly buffer Particles {
    Particle particles[];
};

layout(location = 0) out vec3 out_color;

void main() {
    Particle p = particles[gl_VertexIndex];
    vec4 clip = viewProjection * vec4(p.position.xyz, 1.0);
    gl_Position = clip;
    float idealSize = 40.0 / max(clip.w, 0.1);
    float size = clamp(idealSize, 3.0, 12.0);
    gl_PointSize = size;
    float coverage = min(idealSize * idealSize / (size * size), 1.0);
    float speed = length(p.velocity.xyz);
    out_color = mix(vec3(0.2, 0.6, 1.0), vec3(1.0, 0.5, 0.15), saturate(speed * 0.25)) * (2000.0 + 6000.0 * p.position.w) * coverage;
}
