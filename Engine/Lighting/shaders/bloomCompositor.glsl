/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

layout(location = 1) in vec2 fragTexCoord;
layout(location = 0) out vec4 FragColor;

uniform sampler2D blurredBrightPassTexture;
uniform sampler2D sceneTexture;
uniform float bloomIntensity;

void main() {
    vec2 texCoords = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);

    vec4 bloomColor = texture(blurredBrightPassTexture, texCoords);
    vec4 originalColor = texture(sceneTexture, texCoords);

    vec4 finalColor = originalColor + bloomColor * bloomIntensity;

    FragColor = finalColor;
}
