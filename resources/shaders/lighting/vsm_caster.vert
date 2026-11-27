#version 450
#extension GL_GOOGLE_include_directive : require
#ifdef PAGE_VIEWPORTS
#extension GL_ARB_shader_viewport_layer_array : require
#endif
#include "vsm_pass.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

struct WorldRows {
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

layout(std430) readonly buffer WorldMatrixBuffer {
    WorldRows worldRows[];
};

layout(std430) readonly buffer VsmFaceBuffer {
    VsmFace vsmFaces[];
};

layout(std430) readonly buffer VsmRenderList {
    uvec2 vsmRenderList[];
};

layout(std430) readonly buffer VsmSortedInstances {
    uvec2 vsmSorted[];
};

#ifdef PAGE_VIEWPORTS
out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[1];
};
#else
out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[5];
};
#endif

void main() {
    uvec2 instance = vsmSorted[gl_InstanceIndex];
    uvec2 item = vsmRenderList[instance.y];
    VsmPage page = vsmDecodePage(item.x);
    WorldRows w = worldRows[instance.x];
    vec3 world = vec3(dot(w.r0, vec4(in_position, 1.0)), dot(w.r1, vec4(in_position, 1.0)), dot(w.r2, vec4(in_position, 1.0)));
    vec4 clip = vsmFaces[page.face].viewProjection * vec4(world, 1.0);
    float n = float(1u << page.mip);
    float uw = n * 0.5 * (clip.x + clip.w) - float(page.page.x) * clip.w;
    float vw = n * 0.5 * (clip.w - clip.y) - float(page.page.y) * clip.w;
    vec4 cut = vsmFaces[page.face].clipPlane;
#ifdef PAGE_VIEWPORTS
    gl_ClipDistance[0] = dot(cut.xyz, world) + cut.w;
    gl_ViewportIndex = int(item.y % VSM_VIEWPORTS_PER_DRAW);
    gl_Position = vec4(2.0 * uw - clip.w, clip.w - 2.0 * vw, clip.z, clip.w);
#else
    gl_ClipDistance[0] = uw;
    gl_ClipDistance[1] = clip.w - uw;
    gl_ClipDistance[2] = vw;
    gl_ClipDistance[3] = clip.w - vw;
    gl_ClipDistance[4] = dot(cut.xyz, world) + cut.w;
    uint side = uint(vsmParams.z);
    vec2 physical = vec2(item.y % side, item.y / side);
    float scale = float(VSM_PAGE) / vsmParams.y;
    gl_Position = vec4(2.0 * scale * (physical.x * clip.w + uw) - clip.w, clip.w - 2.0 * scale * (physical.y * clip.w + vw), clip.z, clip.w);
#endif
}
