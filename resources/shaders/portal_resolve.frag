#version 450

// Replaces the parent's depth and G-buffer inside a portal quad with the child view's
// depth and G-buffer, expressed in the parent's (virtual) space. Every screen-space effect
// of the parent then sees the room behind the portal as ordinary geometry and never the
// portal plane itself. The plane depth is only written later by portal_composite.frag, for
// the passes that need it as an occluder (transparency).

layout(std140) uniform PortalDepthData {
    mat4 projection;
    mat4 childInvProjection;
    mat4 viewProjection;
    mat4 invView;
    mat4 normalToParent;
    vec4 center;
    vec4 axisU;
    vec4 axisV;
    vec4 normal;
    vec4 camera;
    vec4 params;
};

uniform sampler2D u_childDepth;
uniform sampler2D u_sceneDepth;
uniform sampler2D u_childGBuffer0;
uniform sampler2D u_childGBuffer1;
uniform usampler2D u_childGBuffer2;

layout(location = 0) out vec4 out_gbuffer0;
layout(location = 1) out vec4 out_gbuffer1;
layout(location = 2) out uvec4 out_gbuffer2;

vec2 octWrap(vec2 v) { return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }

vec2 encodeNormal(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.z >= 0.0 ? n.xy : octWrap(n.xy);
    return e * 0.5 + 0.5;
}

vec3 decodeNormal(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy / params.zw;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    vec3 dir = normalize((invView * vec4(ndc.x / params.x, ndc.y / params.y, -1.0, 0.0)).xyz);
    float facing = dot(normal.xyz, dir);
    float side = dot(normal.xyz, camera.xyz - center.xyz);
    if (side > 0.0 && facing >= 0.0) discard;
    float denom = abs(facing) < 1e-6 ? 1e-6 : facing;
    float t = dot(normal.xyz, center.xyz - camera.xyz) / denom;
    if (t <= 0.0) discard;
    vec3 q = camera.xyz + dir * t - center.xyz;
    if (abs(dot(q, axisU.xyz) * axisU.w) > 1.0 || abs(dot(q, axisV.xyz) * axisV.w) > 1.0) discard;
    vec4 clip = viewProjection * vec4(camera.xyz + dir * t, 1.0);
    float planeDepth = clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
    if (texelFetch(u_sceneDepth, pixel, 0).r > planeDepth + 1e-5) discard;  // reversed Z: larger = nearer

    vec4 g0 = texelFetch(u_childGBuffer0, pixel, 0);
    vec4 g1 = texelFetch(u_childGBuffer1, pixel, 0);
    uvec4 g2 = texelFetch(u_childGBuffer2, pixel, 0);
    vec3 n = normalize(mat3(normalToParent) * decodeNormal(g1.rg));
    out_gbuffer0 = g0;
    out_gbuffer1 = vec4(encodeNormal(n), g1.ba);
    out_gbuffer2 = g2;

    float childDepth = texelFetch(u_childDepth, pixel, 0).r;
    if (childDepth <= 0.0) {
        gl_FragDepth = 0.0;
        return;
    }
    vec4 view = childInvProjection * vec4(ndc, childDepth, 1.0);
    view /= view.w;
    vec4 parent = projection * view;
    gl_FragDepth = clamp(min(parent.z / parent.w, planeDepth - 4e-7), 0.0, 1.0);
}
