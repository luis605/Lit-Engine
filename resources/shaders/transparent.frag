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
};

layout(location = 5) flat in vec4 in_material;
layout(location = 0) out vec4 out_color;
#ifdef POINT_SPRITE
layout(location = 3) flat in float in_pointRound;
layout(location = 4) flat in float in_pointSize;
#endif


float spherePhase(float c) {
    c = clamp(c, -1.0, 1.0);
    float x = abs(c);
    float a = sqrt(1.0 - x) * (1.5707288 + x * (-0.2121144 + x * (0.0742610 - 0.0187293 * x)));
    a = c >= 0.0 ? a : 3.14159265 - a;
    return (sqrt(1.0 - c * c) + (3.14159265 - a) * c) / 3.14159265;
}

vec3 billboardLighting(vec3 fragPos) {
    vec3 V = normalize(viewPos - fragPos);
    vec3 lighting = mix(vec3(0.06, 0.07, 0.10), vec3(0.16, 0.18, 0.22), 0.5);

    lighting += (2.0 / 3.0) * spherePhase(dot(normalize(dirLightDir.xyz), V)) * dirLightColor.rgb;

    vec3 p0Dir = pointLight0Pos.xyz - fragPos;
    float p0Dist = length(p0Dir);
    float p0Atten = clamp(1.0 - p0Dist / pointLight0Pos.w, 0.0, 1.0);
    lighting += (2.0 / 3.0) * spherePhase(dot(p0Dir / max(p0Dist, 0.001), V)) * pointLight0Color.rgb * (pointLight0Color.w * p0Atten * p0Atten);

    vec3 p1Dir = pointLight1Pos.xyz - fragPos;
    float p1Dist = length(p1Dir);
    float p1Atten = clamp(1.0 - p1Dist / pointLight1Pos.w, 0.0, 1.0);
    lighting += (2.0 / 3.0) * spherePhase(dot(p1Dir / max(p1Dist, 0.001), V)) * pointLight1Color.rgb * (pointLight1Color.w * p1Atten * p1Atten);

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

    vec3 p0Dir = pointLight0Pos.xyz - in_fragPos;
    float p0Dist = length(p0Dir);
    float p0Atten = clamp(1.0 - p0Dist / pointLight0Pos.w, 0.0, 1.0);
    p0Atten = p0Atten * p0Atten;
    vec3 p0L = p0Dir / max(p0Dist, 0.001);
    float p0Diff = max(dot(N, p0L), 0.0);
    vec3 p0H = normalize(p0L + V);
    float p0Spec = pow(max(dot(N, p0H), 0.0), 32.0);
    vec3 p0Color = (p0Diff + p0Spec) * pointLight0Color.rgb * (pointLight0Color.w * p0Atten);

    vec3 p1Dir = pointLight1Pos.xyz - in_fragPos;
    float p1Dist = length(p1Dir);
    float p1Atten = clamp(1.0 - p1Dist / pointLight1Pos.w, 0.0, 1.0);
    p1Atten = p1Atten * p1Atten;
    vec3 p1L = p1Dir / max(p1Dist, 0.001);
    float p1Diff = max(dot(N, p1L), 0.0);
    vec3 p1H = normalize(p1L + V);
    float p1Spec = pow(max(dot(N, p1H), 0.0), 32.0);
    vec3 p1Color = (p1Diff + p1Spec) * pointLight1Color.rgb * (pointLight1Color.w * p1Atten);

    vec3 lighting = ambient + sunColor + p0Color + p1Color;
    vec3 baseColor = mix(vec3(0.3, 0.6, 1.0), in_material.rgb, in_material.a);

    out_color = vec4(baseColor * lighting, 0.6);
}