/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

uniform sampler2D srcTexture;
uniform vec2 srcResolution;

layout(location = 1) in vec2 fragTexCoord;
layout(location = 0) out vec4 FragColor;

void main() {
    vec2 texelSize = 1.0 / srcResolution;

    vec3 color = vec3(0.0);
    color += texture(srcTexture, fragTexCoord + texelSize * vec2(-0.5, -0.5)).rgb;
    color += texture(srcTexture, fragTexCoord + texelSize * vec2(0.5, -0.5)).rgb;
    color += texture(srcTexture, fragTexCoord + texelSize * vec2(-0.5, 0.5)).rgb;
    color += texture(srcTexture, fragTexCoord + texelSize * vec2(0.5, 0.5)).rgb;

    FragColor = vec4(color * 0.25, 1.0);
}
