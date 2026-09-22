#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"
#include "lighting.glsl"

layout(location = 0) out vec4 out_color;
layout(location = 2) flat in float in_billboard;

float spherePhase(float c) {
    c = clamp(c, -1.0, 1.0);
    float x = abs(c);
    float a = sqrt(1.0 - x) * (1.5707288 + x * (-0.2121144 + x * (0.0742610 - 0.0187293 * x)));
    a = c >= 0.0 ? a : 3.14159265 - a;
    return (sqrt(1.0 - c * c) + (3.14159265 - a) * c) / 3.14159265;
}

vec3 billboardLight(vec4 posRange, vec4 colorIntensity, vec4 dirCone, vec4 params, vec3 V, vec3 fragPos) {
    vec3 toLight = posRange.xyz - fragPos;
    float dist = length(toLight);
    float atten = clamp(1.0 - dist / posRange.w, 0.0, 1.0);
    atten = atten * atten;
    vec3 L = toLight / max(dist, 0.001);
    if (params.y > 0.5) atten *= smoothstep(dirCone.w, params.x, dot(-L, dirCone.xyz));
    return (2.0 / 3.0) * spherePhase(dot(L, V)) * colorIntensity.rgb * (colorIntensity.w * atten);
}

vec3 billboardLighting(vec3 fragPos) {
    vec3 V = normalize(viewPos - fragPos);
    vec3 lighting = mix(vec3(0.06, 0.07, 0.10), vec3(0.16, 0.18, 0.22), 0.5);

    lighting += (2.0 / 3.0) * spherePhase(dot(normalize(dirLightDir.xyz), V)) * dirLightColor.rgb;

    uint lightCount = uint(lightInfo.x);
    if (lightCount > 0u) lighting += billboardLight(lightHead[0], lightHead[1], lightHead[2], lightHead[3], V, fragPos);
    if (lightCount > 1u) lighting += billboardLight(lightHead[4], lightHead[5], lightHead[6], lightHead[7], V, fragPos);
    for (uint li = 2u; li < lightCount; ++li) lighting += billboardLight(lightData[li * 4u], lightData[li * 4u + 1u], lightData[li * 4u + 2u], lightData[li * 4u + 3u], V, fragPos);

    return lighting;
}


void main() {
    if (in_billboard > 0.5) {
        out_color = vec4(mix(vec3(0.3, 0.6, 1.0), in_material.rgb, in_material.a) * billboardLighting(in_fragPos), 0.6);
        return;
    }
    vec3 N = surfaceNormal();
    vec3 baseColor = mix(vec3(0.3, 0.6, 1.0), in_material.rgb, in_material.a);
    out_color = vec4(baseColor * surfaceLighting(N, in_fragPos), 0.6);
}
