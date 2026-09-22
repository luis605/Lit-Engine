#version 460 core
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"

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

layout(std430)
layout(binding = 5) readonly buffer VisibleLargeObjectBuffer {
    uvec2 visibleLargeObjects[];
};

void main()
{
    uint objectId = visibleLargeObjects[gl_InstanceIndex].x & 0x1FFFFFFFu;
    mat4 modelMatrix = loadWorld(worldRows[objectId]);
    gl_Position = projection * view * modelMatrix * vec4(aPos, 1.0);
}
