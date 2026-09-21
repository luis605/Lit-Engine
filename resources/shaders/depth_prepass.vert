#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

struct WorldRows {
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

mat4 loadWorld(WorldRows w) {
    return mat4(vec4(w.r0.x, w.r1.x, w.r2.x, 0.0), vec4(w.r0.y, w.r1.y, w.r2.y, 0.0), vec4(w.r0.z, w.r1.z, w.r2.z, 0.0), vec4(w.r0.w, w.r1.w, w.r2.w, 1.0));
}

layout(std430, binding = 2) readonly buffer WorldMatrixBuffer {
    WorldRows worldRows[];
};

layout (std140, binding = 0) uniform SceneData {
    mat4 projection;
    mat4 view;
    vec3 lightPos;
    vec3 viewPos;
    vec3 lightColor;
    vec4 frustumPlanes[6];
} sceneData;

layout(std430)
layout(binding = 5) readonly buffer VisibleLargeObjectBuffer {
    uint visibleLargeObjects[];
};

void main()
{
    uint objectId = visibleLargeObjects[gl_InstanceIndex] & 0x1FFFFFFFu;
    mat4 modelMatrix = loadWorld(worldRows[objectId]);
    gl_Position = sceneData.projection * sceneData.view * modelMatrix * vec4(aPos, 1.0);
}
