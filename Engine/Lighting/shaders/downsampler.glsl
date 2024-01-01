#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

void main() {
    float weight = 1.0 / (2.0 * 5.0 + 1.0); // Adjust the weight for a more intense bloom effect
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec3 result = texture(srcTexture, fragTexCoord).rgb * (1.0 - 9.0 * weight);

    // Horizontal blur pass
    for (int x = -5; x <= 5; x++) {
        vec2 offset = vec2(x, 0) * texelSize * 5.0;
        result += texture(srcTexture, fragTexCoord + offset).rgb * weight;
    }

    // Vertical blur pass
    for (int y = -5; y <= 5; y++) {
        vec2 offset = vec2(0, y) * texelSize * 5.0;
        result += texture(srcTexture, fragTexCoord + offset).rgb * weight;
    }

    FragColor = vec4(result, 1.0);
}
