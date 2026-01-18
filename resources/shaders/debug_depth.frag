#version 460 core

layout (location = 0) in vec2 TexCoord;
layout (location = 0) out vec4 FragColor;

uniform sampler2D u_depthTexture;

layout (std140, binding = 1) uniform DebugDepthUniforms {
    float u_nearPlane;
    float u_farPlane;
    float u_mipLevel;
    float u_padding;
};

void main()
{
    float depth = textureLod(u_depthTexture, TexCoord, u_mipLevel).r;
    float displayValue = 1.0 - depth;

    FragColor = vec4(vec3(displayValue), 1.0);
}
