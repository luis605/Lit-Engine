#version 330 core

const float stepSize = 4.0;

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

const float weightConstant = 0.00025;
float weight = weightConstant * (1.0 + bloomBrightness) * 10;
float attenuation = 1.0 - 9.0 * weight;
vec2 texelSize = vec2(1.0) / textureSize(srcTexture, 0);

uniform int samples = 6;

void main() {
    vec3 result = texture(srcTexture, fragTexCoord).rgb;
    result *= attenuation;

    vec2 offsetStep = texelSize * stepSize;

    for (int i = -samples; i <= samples; ++i) {
        for (int j = -samples; j <= samples; ++j) {
            vec2 offset = vec2(i, j) * offsetStep;
            vec2 texCoordOffset = fragTexCoord + offset;

            vec3 texSample = texture(srcTexture, texCoordOffset).rgb;
            result += texSample * weight;
        }
    }

    FragColor = vec4(result, 1.0);
}
