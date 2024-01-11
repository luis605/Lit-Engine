#version 330 core

const float weightConstant = 1.0 / 2000.0;
const int samples = 9;
const float stepSize = 5.0;

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

void main() {
    float weight = weightConstant * (1.0 + bloomBrightness) * 2;
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec3 result = texture(srcTexture, fragTexCoord).rgb * (1.0 - 9.0 * weight);

    // Unroll the loops
    for (int i = -samples; i <= samples; i += 2) {
        for (int j = -samples; j <= samples; j += 2) {
            vec2 offset1 = vec2(i, j) * texelSize * stepSize;
            vec2 offset2 = vec2(i + 1, j + 1) * texelSize * stepSize;
            result += (texture(srcTexture, fragTexCoord + offset1).rgb +
                       texture(srcTexture, fragTexCoord + offset2).rgb) * weight * 2.0;
        }
    }

    FragColor = vec4(result, 1.0);
}
