#version 330 core

const float stepSize = 6.0;

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

const float weightConstant = 0.0007;
float weight = weightConstant * (1.0 + bloomBrightness) * 10;
float attenuation = 1.0 - 6.0 * weight;
vec2 texelSize = vec2(1.0) / textureSize(srcTexture, 0);

uniform int samples = 33; // Increased number of samples

float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    vec3 result = texture(srcTexture, fragTexCoord).rgb;
    result *= attenuation;

    vec2 offsetStep = texelSize * stepSize;

    for (int i = -samples; i <= samples; ++i) {
        for (int j = -samples; j <= samples; ++j) {
            vec2 offset = vec2(i, j) * offsetStep;
            vec2 texCoordOffset = fragTexCoord + offset;

            // Clamp texture coordinates to [0,1]
            texCoordOffset = clamp(texCoordOffset, 0.0, 1.0);

            // Use wider Gaussian filter for weighting
            float gaussWeight = gaussian(length(offset) / (samples * offsetStep.x), 3.0);

            vec3 tex = texture(srcTexture, texCoordOffset).rgb;
            result += tex * gaussWeight * weight;
        }
    }

    FragColor = vec4(result, 1.0);
}
