#version 330 core

const float sigma = 20.0;  // Standard deviation for Gaussian function
const float sigmaSquared = sigma * sigma;
const float normalizationFactor = 1.0 / sqrt(2.0 * 3.1415926 * sigmaSquared);

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform int kernelSize = 30;

float gaussian(float x) {
    return normalizationFactor * exp(-0.5 * x * x / sigmaSquared);
}

void main() {
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec3 color = vec3(0.0);
    mediump float totalWeight = 0.0;

    // Apply vertical blur
    for (int i = -kernelSize; i <= kernelSize; ++i) {
        float weight = gaussian(float(i));
        vec2 offset = vec2(0.0, float(i)) * texelSize;
        vec3 sample = texture(srcTexture, fragTexCoord + offset).rgb;
        color += sample * weight;
        totalWeight += weight;
    }

    FragColor = vec4(color / totalWeight, 1.0);
}
