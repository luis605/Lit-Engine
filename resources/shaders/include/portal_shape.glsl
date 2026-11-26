float portalShapeDistance(vec2 p, vec2 halfSize, vec4 shape) {
    vec2 a = abs(p);
    int type = int(shape.x + 0.5);
    if (type == 1) {
        vec2 h = max(halfSize, vec2(1e-4));
        float k = length(a / h);
        float g = length(a / (h * h));
        return g > 1e-6 ? k * (k - 1.0) / g : -min(h.x, h.y);
    }
    float r = type == 2 ? clamp(shape.y, 0.0, min(halfSize.x, halfSize.y)) : 0.0;
    vec2 q = a - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float portalHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float portalNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(portalHash(i), portalHash(i + vec2(1.0, 0.0)), u.x), mix(portalHash(i + vec2(0.0, 1.0)), portalHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

vec3 portalViewRay(vec2 fragCoord, mat4 invView, vec4 params) {
    vec2 uv = fragCoord / params.zw;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return normalize((invView * vec4(ndc.x / params.x, ndc.y / params.y, -1.0, 0.0)).xyz);
}

int portalRayMode(vec3 origin, vec3 dir, vec3 forward, float nearDistance, vec4 center, vec4 normal, out float t) {
    float facing = dot(normal.xyz, dir);
    float denom = abs(facing) < 1e-6 ? 1e-6 : facing;
    t = dot(normal.xyz, center.xyz - origin) / denom;
    vec3 start = origin + dir * (nearDistance / max(dot(dir, forward), 1e-4));
    if (dot(normal.xyz, start - center.xyz) > -2e-4) return facing < 0.0 && t > 0.0 ? 1 : 0;
    return 2;
}

vec2 portalPlaneHit(vec3 origin, vec3 dir, vec4 center, vec4 axisU, vec4 axisV, vec4 normal, out float t) {
    float facing = dot(normal.xyz, dir);
    float denom = abs(facing) < 1e-6 ? 1e-6 : facing;
    t = dot(normal.xyz, center.xyz - origin) / denom;
    vec3 q = origin + dir * t - center.xyz;
    return vec2(dot(q, axisU.xyz) * sqrt(axisU.w), dot(q, axisV.xyz) * sqrt(axisV.w));
}

float portalCoverage(vec2 fragCoord, vec3 origin, mat4 invView, vec4 params, vec4 center, vec4 axisU, vec4 axisV, vec4 normal, vec4 shape, vec4 edge,
                     out vec2 p, out float sd, out float distancePixels, out vec2 outward) {
    vec2 halfSize = vec2(inversesqrt(max(axisU.w, 1e-12)), inversesqrt(max(axisV.w, 1e-12)));
    float t;
    float tx;
    float ty;
    p = portalPlaneHit(origin, portalViewRay(fragCoord, invView, params), center, axisU, axisV, normal, t);
    vec2 px = portalPlaneHit(origin, portalViewRay(fragCoord + vec2(1.0, 0.0), invView, params), center, axisU, axisV, normal, tx);
    vec2 py = portalPlaneHit(origin, portalViewRay(fragCoord + vec2(0.0, 1.0), invView, params), center, axisU, axisV, normal, ty);
    sd = portalShapeDistance(p, halfSize, shape);
    vec2 gradient = vec2(portalShapeDistance(px, halfSize, shape), portalShapeDistance(py, halfSize, shape)) - sd;
    float metresPerPixel = max(length(gradient), 1e-7);
    outward = gradient / metresPerPixel;
    distancePixels = sd / metresPerPixel;
    float coverage = clamp(0.5 - distancePixels / max(shape.z, 1e-3), 0.0, 1.0);
    if (shape.w > 0.0) {
        float band = 1.0 - smoothstep(0.0, max(edge.y, 1e-4), -sd);
        float threshold = shape.w * band;
        coverage *= smoothstep(threshold - 0.04, threshold + 0.04, portalNoise(p * 22.0));
    }
    return coverage;
}
