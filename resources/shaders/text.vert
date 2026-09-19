#version 450

layout(location = 0) in vec4 in_pos_uv;

layout(std140) uniform TextConstants {
    mat4 projection;
    vec4 textColor;
};

out vec2 TexCoords;

void main() {
    gl_Position = projection * vec4(in_pos_uv.xy, 0.0, 1.0);
    TexCoords = in_pos_uv.zw;
}