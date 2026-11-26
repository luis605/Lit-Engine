#version 450
#extension GL_GOOGLE_include_directive : require

#include "portal_shape.glsl"

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
    vec4 shape;
    vec4 edge;
    vec4 rim;
    vec4 misc;
};

uniform sampler2D u_child;

layout(location = 0) out vec4 out_color;

vec3 fetchChild(vec2 pixel) {
    ivec2 limit = ivec2(params.zw) - 1;
    return texelFetch(u_child, clamp(ivec2(pixel), ivec2(0), limit), 0).rgb;
}

void main() {
    vec3 origin = camera.xyz;
    vec3 dir = portalViewRay(gl_FragCoord.xy, invView, params);
    float facing = dot(normal.xyz, dir);
    float t;
    int mode = portalRayMode(origin, dir, -invView[2].xyz, misc.z, center, normal, t);
    if (mode == 0) discard;
    vec2 p;
    float sd;
    float distancePixels;
    vec2 outward;
    float alpha = portalCoverage(gl_FragCoord.xy, origin, invView, params, center, axisU, axisV, normal, shape, edge, p, sd, distancePixels, outward);
    if (alpha <= 0.0) discard;
    float depth = 1.0;
    if (mode == 1) {
        vec4 clip = viewProjection * vec4(origin + dir * t, 1.0);
        depth = clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
    }
    gl_FragDepth = depth;

    float band = 1.0 - smoothstep(0.0, max(edge.y, 1e-4), -sd);
    vec3 result;
    if (camera.w > 0.5) {
        result = color.rgb;
    } else {
        float phase = misc.x * misc.y;
        vec2 inward = -outward;
        vec2 tangent = vec2(-outward.y, outward.x);
        float bend = portalNoise(p * 7.0 + vec2(phase, -0.7 * phase)) - 0.35;
        float slide = portalNoise(p.yx * 9.0 - vec2(0.5 * phase, phase)) - 0.5;
        vec2 refraction = (inward * bend + tangent * slide) * edge.z * band;
        vec2 split = inward * edge.w * band;
        vec2 pixel = gl_FragCoord.xy + refraction;
        result = vec3(fetchChild(pixel + split).r, fetchChild(pixel).g, fetchChild(pixel - split).b);
    }
    float fresnel = pow(1.0 - clamp(abs(facing), 0.0, 1.0), 5.0) * rim.w;
    result += rim.rgb * edge.x * (band * band + fresnel);
    out_color = vec4(result, alpha);
}
