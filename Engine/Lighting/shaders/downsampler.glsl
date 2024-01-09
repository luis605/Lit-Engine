#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

void main() {
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec3 result = texture(srcTexture, fragTexCoord).rgb;

    // Horizontal blur pass
    for (int x = -5; x <= 5; x++) {
        vec2 offset = vec2(x, 0) * texelSize * 5.0;
        result += texture(srcTexture, fragTexCoord + offset).rgb / 11.0;
    }

    // Vertical blur pass
    for (int y = -5; y <= 5; y++) {
        vec2 offset = vec2(0, y) * texelSize * 5.0;
        result += texture(srcTexture, fragTexCoord + offset).rgb / 11.0;
    }

    FragColor = vec4(result * bloomBrightness, 1.0);
}
