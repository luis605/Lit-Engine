#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;

const float weight = 1.0 / 2000.0; // Adjust the weight for a more intense bloom effect

void main() {
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec3 result = texture(srcTexture, fragTexCoord).rgb * (1.0 - 9.0 * weight);

    for(int x = -10; x <= 10; x++) {
        for(int y = -10; y <= 10; y++) {
            result += texture(srcTexture, fragTexCoord + vec2(x, y) * texelSize * 5.0).rgb * weight * 2;
        }
    }

    FragColor = vec4(result, 1.0);
}
