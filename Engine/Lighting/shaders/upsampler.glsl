#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;

uniform sampler2D downsampledTexture; // The bloom texture at lower resolution
uniform sampler2D originalTexture;    // The original scene texture
uniform float threshold = 0.2;        // Brightness threshold to extract bloom
uniform float bloomIntensity = 1.0;   // Intensity of the bloom effect
uniform float smoothness = 0.1;       // Smoothness factor for the threshold

// Smooth step function for gradual thresholding
float smoothStep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void main() {
    vec2 texCoords = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    vec4 bloomColor = texture(downsampledTexture, texCoords);
    vec4 originalColor = texture(originalTexture, texCoords);

    float originalBrightness = max(max(originalColor.r, originalColor.g), originalColor.b);
    float bloomFactor = smoothStep(threshold - smoothness, threshold + smoothness, originalBrightness);

    vec4 bloomEffect = bloomColor * bloomIntensity;
    vec4 finalColor = mix(originalColor, originalColor + bloomEffect, bloomFactor);

    FragColor = finalColor;
}
