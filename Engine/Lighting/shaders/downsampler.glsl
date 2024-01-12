#version 330 core

const int samples = 6;
const float stepSize = 3.0;

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

const float weightConstant = 0.0006;
float weight = weightConstant * (1.0 + bloomBrightness) * 8;
float attenuation = 1.0 - 9.0 * weight;
vec2 texelSize = vec2(1.0) / textureSize(srcTexture, 0);

void main() {
    vec3 result = texture(srcTexture, fragTexCoord).rgb;
    result *= attenuation;

    vec2 offsetStep = texelSize * stepSize;

    for (int i = -samples; i <= samples; i += 3) {
        for (int j = -samples; j <= samples; j += 3) {
            vec2 offset = vec2(i, j) * offsetStep;
            vec2 texCoordOffset = fragTexCoord + offset;

            vec3 tex1 = texture(srcTexture, texCoordOffset).rgb;
            result += tex1 * weight;

            offset += offsetStep; // Reuse offsetStep
            texCoordOffset = fragTexCoord + offset;

            vec3 tex2 = texture(srcTexture, texCoordOffset).rgb;
            result += tex2 * weight;
        }
    }

    FragColor = vec4(result, 1.0);
}
