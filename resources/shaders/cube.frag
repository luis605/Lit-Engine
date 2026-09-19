#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_fragPos;

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

layout(location = 0) out vec4 out_color;

void main() {
    vec3 N = normalize(in_normal);
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
    vec3 baseColor = abs(N) * 0.7 + 0.3;

    out_color = vec4(baseColor * lighting, 1.0);
}