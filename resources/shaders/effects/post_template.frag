#version 450
#extension GL_GOOGLE_include_directive : require
#include "lit.glsl"

uniform sampler2D u_color;
uniform sampler2D u_depth;
uniform sampler2D u_gbuffer0;
uniform sampler2D u_gbuffer1;

layout(location = 0) out vec4 out_color;

vec3 litColor(vec2 uv) { return textureLod(u_color, uv, 0.0).rgb; }
float litDepth(vec2 uv) { return textureLod(u_depth, uv, 0.0).r; }
float litLinearDepth(vec2 uv) { return linearViewDepth(litDepth(uv)); }
vec3 litWorldPosition(vec2 uv) { return worldFromDepth(uv, litDepth(uv)); }
vec3 litNormal(vec2 uv) { return decodeNormal(textureLod(u_gbuffer1, uv, 0.0).rg); }
vec3 litBaseColor(vec2 uv) { return textureLod(u_gbuffer0, uv, 0.0).rgb; }
bool litIsSky(vec2 uv) { return litDepth(uv) >= 1.0; }
vec2 litTexel() { return screenParams.zw; }
#ifdef LIT_LDR_EFFECT
bool litLdr() { return true; }
#else
bool litLdr() { return false; }
#endif

#pragma lit_user_code

void main() {
    vec2 uv = gl_FragCoord.xy * screenParams.zw;
#ifdef LIT_LDR_EFFECT
    vec3 display = saturate(effect(uv, litColor(uv)));
    out_color = vec4(display, dot(display, vec3(0.299, 0.587, 0.114)));
#else
    out_color = vec4(effect(uv, litColor(uv)), 1.0);
#endif
}
