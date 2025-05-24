/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

layout(location = 1) in vec2 fragTexCoord;
layout(location = 0) out vec4 FragColor;

uniform sampler2D downsampledTexture;
uniform sampler2D originalTexture;
uniform float threshold;
uniform float bloomIntensity;
const float smoothness = 0.1;

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
