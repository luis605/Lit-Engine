#version 450

layout(std140) uniform PortalDepthData {
    mat4 projection;
    mat4 childInvProjection;
    mat4 viewProjection;
    mat4 invView;
    vec4 center;
    vec4 axisU;
    vec4 axisV;
    vec4 normal;
    vec4 camera;
    vec4 params;
};

uniform sampler2D u_childDepth;
uniform sampler2D u_sceneDepth;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    bool maskMode = camera.w > 0.5;
    vec2 uv = gl_FragCoord.xy / params.zw;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    vec3 dir = normalize((invView * vec4(ndc.x / params.x, ndc.y / params.y, -1.0, 0.0)).xyz);
    float facing = dot(normal.xyz, dir);
    float side = dot(normal.xyz, camera.xyz - center.xyz);
    if (!maskMode && side > 0.0 && facing >= 0.0) discard;
    float denom = abs(facing) < 1e-6 ? 1e-6 : facing;
    float t = dot(normal.xyz, center.xyz - camera.xyz) / denom;
    if (maskMode) {
        if (t > 0.0) {
            vec3 mq = camera.xyz + dir * t - center.xyz;
            if (abs(dot(mq, axisU.xyz) * axisU.w) <= 1.0 && abs(dot(mq, axisV.xyz) * axisV.w) <= 1.0) discard;
        }
        gl_FragDepth = 1.0;
        return;
    }
    if (t <= 0.0) discard;
    vec3 q = camera.xyz + dir * t - center.xyz;
    if (abs(dot(q, axisU.xyz) * axisU.w) > 1.0 || abs(dot(q, axisV.xyz) * axisV.w) > 1.0) discard;
    vec4 clip = viewProjection * vec4(camera.xyz + dir * t, 1.0);
    float planeDepth = clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
    if (texelFetch(u_sceneDepth, pixel, 0).r < planeDepth - 1e-5) discard;
    float childDepth = texelFetch(u_childDepth, pixel, 0).r;
    if (childDepth >= 1.0) {
        gl_FragDepth = 1.0;
        return;
    }
    vec4 view = childInvProjection * vec4(ndc, childDepth, 1.0);
    view /= view.w;
    vec4 parent = projection * view;
    gl_FragDepth = clamp(max(parent.z / parent.w, planeDepth + 4e-7), 0.0, 1.0);
}
