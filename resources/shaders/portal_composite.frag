#version 450

layout(std140) uniform PortalSurfaceData {
    mat4 viewProjection;
    mat4 invView;
    vec4 center;
    vec4 axisU;
    vec4 axisV;
    vec4 normal;
    vec4 camera;
    vec4 params;
    vec4 color;
};

uniform sampler2D u_child;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = gl_FragCoord.xy / params.zw;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    vec3 dir = normalize((invView * vec4(ndc.x / params.x, ndc.y / params.y, -1.0, 0.0)).xyz);
    vec3 origin = camera.xyz;
    float facing = dot(normal.xyz, dir);
    float side = dot(normal.xyz, origin - center.xyz);
    if (side > 0.0 && facing >= 0.0) discard;
    float denom = abs(facing) < 1e-6 ? 1e-6 : facing;
    float t = dot(normal.xyz, center.xyz - origin) / denom;
    vec3 q = origin + dir * t - center.xyz;
    if (abs(dot(q, axisU.xyz) * axisU.w) > 1.0 || abs(dot(q, axisV.xyz) * axisV.w) > 1.0) discard;
    float depth = 0.0;
    if (t > 0.0) {
        vec4 clip = viewProjection * vec4(origin + dir * t, 1.0);
        depth = clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
    }
    gl_FragDepth = depth;
    out_color = camera.w > 0.5 ? vec4(color.rgb, 1.0) : vec4(texelFetch(u_child, ivec2(gl_FragCoord.xy), 0).rgb, 1.0);
}
