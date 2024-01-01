#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;

const float bloomThreshold = 0.5;  // Adjust this threshold to control what is considered "bright"

void main() {
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec2 uv = fragTexCoord * textureSize(srcTexture, 0);

    ivec2 lowerLeft = ivec2(floor(uv - 0.5));  // Lower left texel coordinate
    vec2 frac = uv - vec2(lowerLeft) + 0.5;    // Fractional part

    // Bilinear interpolation
    vec4 result = mix(
        mix(texture(srcTexture, (lowerLeft + ivec2(0, 0)) * texelSize), texture(srcTexture, (lowerLeft + ivec2(1, 0)) * texelSize), frac.x),
        mix(texture(srcTexture, (lowerLeft + ivec2(0, 1)) * texelSize), texture(srcTexture, (lowerLeft + ivec2(1, 1)) * texelSize), frac.x),
        frac.y
    );

    // Apply bloom only to bright pixels
    vec3 brightColor = max(result.rgb - bloomThreshold, vec3(0.0));
    result.rgb = mix(result.rgb, brightColor, bloomThreshold);

    FragColor = result;
}
