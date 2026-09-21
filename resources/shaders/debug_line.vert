#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

layout (std140) uniform DebugLineUniforms {
    mat4 viewProjection;
};

layout (location = 0) out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = viewProjection * vec4(aPos, 1.0);
}
