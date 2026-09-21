#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_fragPos;
layout(location = 2) flat in float in_billboard;

layout(std140) uniform SceneData {
    mat4 projection;
    mat4 view;
    vec3 lightPos;
    vec3 viewPos;
    vec3 lightColor;
    vec4 frustumPlanes[6];
    vec4 dirLightDir;
    vec4 dirLightColor;
    vec4 pointLight0Pos;
    vec4 pointLight0Color;
    vec4 pointLight1Pos;
    vec4 pointLight1Color;
    vec4 screenParams;
    vec4 lightInfo;
    vec4 lightHead[8];
};

layout(std140) uniform LightBlock {
    vec4 lightData[256];
};

layout(location = 5) flat in vec4 in_material;
layout(location = 0) out vec4 out_color;
#ifdef POINT_SPRITE
layout(location = 3) flat in float in_pointRound;
layout(location = 4) flat in float in_pointSize;
#endif


vec3 evalPointLight(vec4 posRange, vec4 colorIntensity, vec4 dirCone, vec4 params, vec3 N, vec3 V, vec3 fragPos) {
    vec3 toLight = posRange.xyz - fragPos;
    float dist = length(toLight);
    float atten = clamp(1.0 - dist / posRange.w, 0.0, 1.0);
    atten = atten * atten;
    vec3 L = toLight / max(dist, 0.001);
    if (params.y > 0.5) atten *= smoothstep(dirCone.w, params.x, dot(-L, dirCone.xyz));
    float diff = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    return (diff + spec) * colorIntensity.rgb * (colorIntensity.w * atten);
}

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
    vec3 N = normalize(in_normal);
#ifdef POINT_SPRITE
    if (in_pointRound > 0.5 && in_pointSize >= 2.5) {
        vec2 pc = gl_PointCoord * 2.0 - 1.0;
        float r2 = dot(pc, pc);
        if (r2 > 1.0) discard;
        vec3 camRight = vec3(view[0][0], view[1][0], view[2][0]);
        vec3 camUp = vec3(view[0][1], view[1][1], view[2][1]);
        N = normalize(camRight * pc.x - camUp * pc.y + normalize(viewPos - in_fragPos) * sqrt(1.0 - r2));
    }
#endif
    vec3 V = normalize(viewPos - in_fragPos);

    vec3 ambient = mix(vec3(0.06, 0.07, 0.10), vec3(0.16, 0.18, 0.22), N.y * 0.5 + 0.5);

    vec3 sunL = normalize(dirLightDir.xyz);
    float sunDiff = max(dot(N, sunL), 0.0);
    vec3 sunH = normalize(sunL + V);
    float sunSpec = pow(max(dot(N, sunH), 0.0), 32.0) * dirLightDir.w;
    vec3 sunColor = (sunDiff + sunSpec) * dirLightColor.rgb;

    vec3 pointColor = vec3(0.0);
    uint lightCount = uint(lightInfo.x);
    if (lightCount > 0u) pointColor += evalPointLight(lightHead[0], lightHead[1], lightHead[2], lightHead[3], N, V, in_fragPos);
    if (lightCount > 1u) pointColor += evalPointLight(lightHead[4], lightHead[5], lightHead[6], lightHead[7], N, V, in_fragPos);
    for (uint li = 2u; li < lightCount; ++li) pointColor += evalPointLight(lightData[li * 4u], lightData[li * 4u + 1u], lightData[li * 4u + 2u], lightData[li * 4u + 3u], N, V, in_fragPos);

    vec3 lighting = ambient + sunColor + pointColor;
    vec3 baseColor = mix(vec3(0.3, 0.6, 1.0), in_material.rgb, in_material.a);

    out_color = vec4(baseColor * lighting, 0.6);
}