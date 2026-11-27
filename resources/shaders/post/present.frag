#version 450

uniform sampler2D u_ldr;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(texelFetch(u_ldr, ivec2(gl_FragCoord.xy), 0).rgb, 1.0);
}
