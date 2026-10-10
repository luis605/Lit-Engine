#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "material.glsl"
#include "surface_api.glsl"
#ifdef TRANSPARENT
#include "lighting_models.glsl"

uniform sampler2D u_sceneColor;

vec3 refractedBackground(ShadingData sd, vec3 worldPos) {
    vec3 refracted = refract(-sd.V, sd.N, 1.0 / max(sd.ior, 1.0));
    if (dot(refracted, refracted) < 1e-6) refracted = -sd.V;
    float travel = max(sd.thickness, 0.02) * 2.0;
    float exitDepth;
    vec2 uv = projectToUv(worldPos + refracted * travel, viewProjection, exitDepth);
    vec2 screenUv = gl_FragCoord.xy * screenParams.zw;
    vec2 edge = min(uv, 1.0 - uv);
    float inside = saturate(min(edge.x, edge.y) * 20.0);
    uv = mix(screenUv, clamp(uv, vec2(0.0), vec2(1.0)), inside);
    float distanceToEye = max(distance(viewPos, worldPos), 0.1);
    float blurPx = sd.roughness * travel * projection[1][1] * screenParams.y * 0.25 / distanceToEye;
    float lod = clamp(log2(max(blurPx, 1.0)), 0.0, float(textureQueryLevels(u_sceneColor) - 1));
    return textureLod(u_sceneColor, uv, lod).rgb;
}
#endif

layout(std430) readonly buffer ExposureBuffer {
    vec4 exposureData;
};

uniform sampler2DArray u_colorTextures;
uniform sampler2DArray u_dataTextures;

layout(location = 0) in vec3 in_worldPos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) flat in uint in_entity;
layout(location = 4) flat in uint in_material;
layout(location = 5) in vec3 in_objectPos;
layout(location = 6) flat in float in_alpha;
#ifdef POINT_SPRITE
layout(location = 7) flat in float in_pointRound;
layout(location = 8) flat in float in_pointSize;
#endif

#ifdef TRANSPARENT
layout(location = 0) out vec4 out_accumulation;
layout(location = 1) out float out_revealage;
#else
layout(location = 0) out vec4 out_gbuffer0;
layout(location = 1) out vec4 out_gbuffer1;
layout(location = 2) out uvec4 out_gbuffer2;
layout(location = 3) out vec4 out_emissive;
#endif

vec3 litPerturbNormal(vec3 N, vec3 p, vec2 uv, vec3 tangentNormal) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return normalize(mat3(T * invmax, B * invmax, N) * tangentNormal);
}

vec3 litBumpNormal(SurfaceInput i, vec3 N, float height, float strength) {
    vec3 dpdx = dFdx(i.worldPosition);
    vec3 dpdy = dFdy(i.worldPosition);
    float dhdx = dFdx(height);
    float dhdy = dFdy(height);
    vec3 r1 = cross(dpdy, N);
    vec3 r2 = cross(N, dpdx);
    float det = dot(dpdx, r1);
    vec3 gradient = sign(det) * (dhdx * r1 + dhdy * r2);
    return normalize(abs(det) * N - strength * gradient);
}

vec4 litTriplanar(bool color, int layer, vec3 p, vec3 n, float scale) {
    vec3 w = pow(abs(n), vec3(4.0));
    w /= dot(w, vec3(1.0)) + 1e-5;
    vec4 x = color ? texture(u_colorTextures, vec3(p.zy * scale, float(layer))) : texture(u_dataTextures, vec3(p.zy * scale, float(layer)));
    vec4 y = color ? texture(u_colorTextures, vec3(p.xz * scale, float(layer))) : texture(u_dataTextures, vec3(p.xz * scale, float(layer)));
    vec4 z = color ? texture(u_colorTextures, vec3(p.xy * scale, float(layer))) : texture(u_dataTextures, vec3(p.xy * scale, float(layer)));
    return x * w.x + y * w.y + z * w.z;
}

vec4 litSample(bool color, int layer, SurfaceInput i, bool triplanar, float scale) {
    if (triplanar) return litTriplanar(color, layer, i.worldPosition, i.geometricNormal, scale);
    return color ? texture(u_colorTextures, vec3(i.uv * scale, float(layer))) : texture(u_dataTextures, vec3(i.uv * scale, float(layer)));
}

Surface litDefaultSurface(MaterialData m, SurfaceInput i) {
    Surface s;
    s.baseColor = m.baseColorOpacity.rgb;
    s.opacity = m.baseColorOpacity.a;
    s.normal = i.geometricNormal;
    s.metallic = m.pbr.x;
    s.roughness = m.pbr.y;
    s.occlusion = m.pbr.w;
    s.emissive = m.emissive.rgb;
    s.tangentAngle = 0.0;
    s.lightingModel = LIT_STANDARD;
    s.toonBands = 3.0;
    s.toonRim = 0.5;
    s.toonRimWidth = 0.3;
    bool triplanar = m.flags.x > 0.5;
    float scale = m.transmission.w;
    if (m.textures.x >= 0) {
        vec4 c = litSample(true, m.textures.x, i, triplanar, scale);
        s.baseColor *= c.rgb;
        s.opacity *= c.a;
    }
    if (m.textures.y >= 0) {
        vec3 tn = litSample(false, m.textures.y, i, triplanar, scale).xyz * 2.0 - 1.0;
        tn.xy *= m.emissive.w;
        s.normal = litPerturbNormal(i.geometricNormal, i.worldPosition, i.uv * scale, normalize(tn));
    }
    if (m.textures.z >= 0) {
        vec3 orm = litSample(false, m.textures.z, i, triplanar, scale).rgb;
        s.occlusion *= orm.r;
        s.roughness *= orm.g;
        s.metallic *= orm.b;
    }
    if (m.textures.w >= 0) s.emissive *= litSample(true, m.textures.w, i, triplanar, scale).rgb;
    return s;
}

#pragma lit_user_code

void main() {
    vec3 N = normalize(in_normal);
#ifdef POINT_SPRITE
    if (in_pointRound > 0.5 && in_pointSize >= 2.5) {
        vec2 pc = gl_PointCoord * 2.0 - 1.0;
        float r2 = dot(pc, pc);
        if (r2 > 1.0) discard;
        vec3 camRight = vec3(view[0][0], view[1][0], view[2][0]);
        vec3 camUp = vec3(view[0][1], view[1][1], view[2][1]);
        N = normalize(camRight * pc.x - camUp * pc.y + normalize(viewPos - in_worldPos) * sqrt(1.0 - r2));
    }
#else
    if (!gl_FrontFacing) N = -N;
#endif
    MaterialData m = materials[in_material & 0x7FFFu];
    SurfaceInput i;
    i.worldPosition = in_worldPos;
    i.objectPosition = in_objectPos;
    i.viewDirection = normalize(viewPos - in_worldPos);
    i.geometricNormal = N;
    i.uv = in_uv;
    i.screenUv = gl_FragCoord.xy * screenParams.zw;
    i.entity = in_entity;
    i.material = in_material & 0x7FFFu;
    i.time = sceneTime();
    i.custom0 = m.custom0;
    i.custom1 = m.custom1;
    i.frontFacing = gl_FrontFacing;
    Surface s = litDefaultSurface(m, i);
#ifdef LIT_USER_SURFACE
    surface(i, s);
#endif
    s.normal = normalize(s.normal);
    float roughness = clamp(s.roughness, 0.045, 1.0);
#ifndef POINT_SPRITE
    if (renderFlags.w > 0.5) {
        vec3 du = dFdx(s.normal);
        vec3 dv = dFdy(s.normal);
        float variance = 0.25 * (dot(du, du) + dot(dv, dv));
        float kernel = min(2.0 * variance, 0.18);
        float a2 = roughness * roughness * roughness * roughness;
        roughness = sqrt(sqrt(clamp(a2 + kernel, 0.0, 1.0)));
    }
#endif
#ifdef TRANSPARENT
    ShadingData sd = prepareShading(in_worldPos, s.normal, s.baseColor, saturate(s.metallic), roughness, saturate(s.occlusion), s.tangentAngle, in_material & 0x7FFFu);
    float visibility;
    float viewDepth = -(view * vec4(in_worldPos, 1.0)).z;
    vec3 color;
    if (s.lightingModel == LIT_UNLIT) {
        color = s.baseColor + s.emissive * exposureData.x;
    } else if (s.lightingModel == LIT_TOON) {
        LitToon toon = LitToon(clamp(s.toonBands, 1.0, 255.0), saturate(s.toonRim), saturate(s.toonRimWidth));
        color = (litToonDirect(sd, toon, gl_FragCoord.xy, viewDepth, visibility) + litToonAmbient(sd, toon, visibility) + s.emissive) * exposureData.x;
    } else {
        color = (evaluateSun(sd, gl_FragCoord.xy, visibility) + evaluateLights(sd, gl_FragCoord.xy, viewDepth) + evaluateIBL(sd, vec4(0.0)) + s.emissive) * exposureData.x;
    }
    float alpha = clamp(in_alpha * s.opacity, 0.0, 1.0);
    float fresnel = pow5(1.0 - sd.NoV);
    alpha = mix(alpha, 1.0, fresnel * (1.0 - sd.roughness) * 0.6);
    if (sd.transmission > 0.0) {
        vec3 background = refractedBackground(sd, in_worldPos);
        vec3 reflectance = sd.f0 * sd.dfg.x + sd.dfg.y;
        color += background * sd.baseColor * sd.transmission * (1.0 - reflectance);
        alpha = mix(alpha, 1.0, sd.transmission);
    }
    float distanceToEye = distance(viewPos, in_worldPos);
    float weight = alpha * clamp(10.0 / (1e-5 + pow(distanceToEye / 5.0, 2.0) + pow(distanceToEye / 200.0, 6.0)), 1e-2, 3e3);
    out_accumulation = vec4(color * alpha, alpha) * weight;
    out_revealage = alpha;
#else
    out_gbuffer0 = vec4(saturate(s.baseColor), saturate(s.occlusion));
    int model = clamp(s.lightingModel, LIT_STANDARD, LIT_TOON);
    out_gbuffer1 = vec4(encodeNormal(s.normal), roughness, float(model) / 3.0);
    uvec2 shading = model == LIT_TOON ? litEncodeToon(s.toonBands, s.toonRim, s.toonRimWidth) : uvec2(uint(saturate(s.metallic) * 255.0 + 0.5), uint(fract(s.tangentAngle) * 255.0 + 0.5));
    out_gbuffer2 = uvec4(shading, in_material & 255u, in_material >> 8u);
    out_emissive = vec4(s.emissive * exposureData.x + (model == LIT_UNLIT ? s.baseColor : vec3(0.0)), 0.0);
#endif
}
