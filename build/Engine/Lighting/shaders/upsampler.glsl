#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;

void main() {
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec4 result = texture(srcTexture, fragTexCoord) + texture(srcTexture, fragTexCoord + vec2(texelSize.x, 0)) +
                  texture(srcTexture, fragTexCoord + vec2(0, texelSize.y)) +
                  texture(srcTexture, fragTexCoord + texelSize);

    FragColor = result / 4.0; // Simple averaging for upscaling
}
