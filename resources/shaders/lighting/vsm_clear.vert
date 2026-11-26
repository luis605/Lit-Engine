#version 450
#extension GL_GOOGLE_include_directive : require
#include "vsm_pass.glsl"

layout(std430) readonly buffer VsmRenderList {
    uvec2 vsmRenderList[];
};

void main() {
    uint physical = vsmRenderList[gl_InstanceIndex].y;
    uint side = uint(vsmParams.z);
    const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 atlas = (vec2(physical % side, physical / side) + corners[gl_VertexIndex]) * (float(VSM_PAGE) / vsmParams.y);
    gl_Position = vec4(atlas.x * 2.0 - 1.0, 1.0 - atlas.y * 2.0, 1.0, 1.0);
}
