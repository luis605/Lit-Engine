/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

layout(location = 1) in vec2 fragTexCoord;
out vec4 FragColor;

uniform sampler2D sceneTexture;
uniform float threshold;

void main() {
    const vec2 coords = vec2(fragTexCoord.x, 1 - fragTexCoord.y);
    vec4 color = texture(sceneTexture, coords);

    float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));

    if (brightness < threshold) {
        color.rgb = vec3(0.0);
    }

    color.rgb = max(vec3(0.0), color.rgb - threshold);

    FragColor = color;
}