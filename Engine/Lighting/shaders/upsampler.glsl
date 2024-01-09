#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;

const float bloomThreshold = 0.5;
const float bloomIntensity = 0.5;

void main() {
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec2 uv = fragTexCoord * textureSize(srcTexture, 0);

    vec2 lowerLeft = floor(uv - 0.5);
    vec2 frac = uv - lowerLeft + 0.5;

    // Bilinear interpolation
    vec4 result = mix(
        mix(texture(srcTexture, lowerLeft * texelSize), texture(srcTexture, (lowerLeft + vec2(1.0, 0.0)) * texelSize), frac.x),
        mix(texture(srcTexture, (lowerLeft + vec2(0.0, 1.0)) * texelSize), texture(srcTexture, (lowerLeft + vec2(1.0, 1.0)) * texelSize), frac.x),
        frac.y
    );

    // Apply bloom only to bright pixels with adjusted intensity
    vec3 brightColor = max(result.rgb - bloomThreshold, vec3(0.0));
    result.rgb = mix(result.rgb, result.rgb + brightColor * bloomIntensity, bloomThreshold);

    FragColor = result;
}
