#version 460 core
#extension GL_GOOGLE_include_directive : require
#include "renderable.glsl"
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

layout(std430, binding = 6) readonly buffer CullSphereBuffer {
    vec4 cullSpheres[];
};

layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 Normal;
layout (location = 2) flat out float Billboard;
layout (location = 5) flat out vec4 out_material;
layout(std430) readonly buffer MaterialBuffer {
    vec4 materials[];
};



struct VisibleTransparentObject {
    uint objectId;
    float distance;
};

layout(std430, binding = 5) readonly buffer VisibleTransparentObjectBuffer {
    VisibleTransparentObject visibleObjects[];
};

// kLodLevelCount - 1 in Mesh.cppm
const uint LOD_BILLBOARD = 6u;

void main()
{
    uint packedEntry = visibleObjects[gl_InstanceIndex].objectId;
    uint objectId = packedEntry & 0x1FFFFFFFu;
    uint lod = packedEntry >> 29u;
    out_material = materials[renderableMaterial(renderables[objectId])];

    if (lod == LOD_BILLBOARD) {
        vec4 boundingSphere = cullSpheres[objectId];
        float depth = max(-(view * vec4(boundingSphere.xyz, 1.0)).z, 0.001);
        float radius = max(boundingSphere.w, depth * 2.0 / (projection[1][1] * screenParams.y));
        vec3 camRight = vec3(view[0][0], view[1][0], view[2][0]);
        vec3 camUp = vec3(view[0][1], view[1][1], view[2][1]);
        vec4 billboardPos = vec4(boundingSphere.xyz + (camRight * aPos.x + camUp * aPos.y) * radius, 1.0);
        FragPos = billboardPos.xyz;
        Normal = normalize(viewPos - billboardPos.xyz);
        Billboard = 1.0;
        gl_Position = projection * view * billboardPos;
        return;
    }

    mat4 modelMatrix = loadWorld(worldRows[objectId]);
    Billboard = 0.0;
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    // cofactor matrix == inverse-transpose up to a scale that is normalised away
    mat3 m3 = mat3(modelMatrix);
    mat3 cofactor = mat3(cross(m3[1], m3[2]), cross(m3[2], m3[0]), cross(m3[0], m3[1]));
    float detSign = dot(m3[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;
    Normal = (cofactor * aNormal) * detSign;
    gl_Position = projection * view * worldPos;
}
