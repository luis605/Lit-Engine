/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

layout(location = 1) in vec2 fragTexCoord;
out vec4 FragColor;

uniform sampler2D sceneTexture;
uniform float grainStrength; // 0.0 - 1.0
uniform float grainSize;    // 0.5 (fine) - 2.0 (coarse)
uniform float time;         // for animation

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec4 color = texture(sceneTexture, fragTexCoord);
    float grain = rand(fragTexCoord * grainSize + time * vec2(0.7, 1.3));
    grain = grain - 0.5;
    color.rgb += grain * grainStrength;
    FragColor = color;
}
