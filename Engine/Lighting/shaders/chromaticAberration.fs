#version 460 core

uniform sampler2D screenTexture;
uniform vec3 offset;

out vec4 fragColor;

void main() {
    vec2 texSize  = textureSize(screenTexture, 0);
    vec2 texCoord = gl_FragCoord.xy / texSize;

    vec2 focusPoint = vec2(0.5, 0.5);
    vec2 direction = texCoord - focusPoint;

    float dist = length(direction);
    float maxDist = length(vec2(0.5));
    float fade = smoothstep(0.0, 0.9 * maxDist, maxDist - dist);

    vec2 offsetDirR = direction * offset.r * fade;
    vec2 offsetDirG = direction * offset.g * fade;
    vec2 offsetDirB = direction * offset.b * fade;

    vec2 safeMin = vec2(0.001);
    vec2 safeMax = vec2(0.999);

    vec2 uvR = clamp(texCoord + offsetDirR, safeMin, safeMax);
    vec2 uvG = clamp(texCoord + offsetDirG, safeMin, safeMax);
    vec2 uvB = clamp(texCoord + offsetDirB, safeMin, safeMax);

    float r = texture(screenTexture, uvR).r;
    float g = texture(screenTexture, uvG).g;
    float b = texture(screenTexture, uvB).b;

    fragColor = vec4(r, g, b, 1.0);
}
