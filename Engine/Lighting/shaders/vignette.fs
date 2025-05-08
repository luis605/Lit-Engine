/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;

uniform sampler2D screenTexture; // the rendered scene
uniform float strength;          // vignette strength, e.g. 0.5
uniform float radius;            // vignette radius, e.g. 0.75
uniform vec4 color;              // vignette color, usually black (0,0,0)

void main() {
    vec2 uv = vec2(fragTexCoord.x, 1 - fragTexCoord.y);
    vec2 centered = uv - vec2(0.5);
    float dist = length(centered);

    float vignette = smoothstep(radius - strength, radius, dist);

    vec4 sceneColor = texture(screenTexture, uv);
    FragColor = mix(sceneColor, color, vignette);
}
