#version 460 core

in vec2 TexCoords;
out vec4 color;

uniform sampler2D text;

layout (std140) uniform TextConstants {
    mat4 projection;
    vec4 textColor;
};

void main()
{
    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);
    color = textColor * sampled;
}
