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

float projectedDiameterPx(float radius, float dist) {
    return radius * projection[1][1] * screenParams.y / max(dist, 1e-4);
}
