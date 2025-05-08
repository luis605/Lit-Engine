/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 330

in vec3 fragPosition;
out vec4 finalColor;

uniform sampler2D equirectangularMap;

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = SampleSphericalMap(normalize(fragPosition));

    vec3 color = min(texture(equirectangularMap, uv).rgb, vec3(1.0));

    finalColor = vec4(color, 1.0);
}
