#version 450

uniform sampler2D u_accumulation;
uniform sampler2D u_revealage;

layout(location = 0) out vec4 out_color;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float revealage = texelFetch(u_revealage, pixel, 0).r;
    if (revealage >= 0.9999) discard;
    vec4 accumulation = texelFetch(u_accumulation, pixel, 0);
    out_color = vec4(accumulation.rgb / clamp(accumulation.a, 1e-4, 5e4), 1.0 - revealage);
}
