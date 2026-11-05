#version 460 core

layout (location = 0) in vec2 TexCoord;

uniform sampler2D u_depthTexture;

void main()
{
    gl_FragDepth = textureLod(u_depthTexture, TexCoord, 0.0).r;
}
