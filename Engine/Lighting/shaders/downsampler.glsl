#version 460 core

uniform sampler2D srcTexture;
uniform int uDownsampleFactor;

in vec2 fragTexCoord;
out vec4 FragColor;

layout(binding = 0) uniform atomic_uint pixelCount;
layout(binding = 1) uniform atomic_uint totalLuminance;

const float LUMINANCE_SCALE = 1000.0;

void main() {
    vec2 textureSize = textureSize(srcTexture, 0);

    int blockSize = uDownsampleFactor;
    vec2 texelSize = 1.0 / vec2(textureSize);

    vec2 baseTexelCoord = fragTexCoord * vec2(textureSize) * blockSize;
    baseTexelCoord.y = textureSize.y - baseTexelCoord.y;

    vec4 color = vec4(0.0);
    for (int y = 0; y < blockSize; ++y) {
        for (int x = 0; x < blockSize; ++x) {

            vec2 texelCoord = baseTexelCoord + vec2(x, y);
            color += texelFetch(srcTexture, ivec2(texelCoord), 0);
        }
    }

    color /= float(blockSize * blockSize);

    float luminance = dot(color.rgb, vec3(0.299, 0.587, 0.114));

    uint scaledLuminance = uint(luminance * LUMINANCE_SCALE);

    atomicCounterAdd(totalLuminance, scaledLuminance);
    atomicCounterIncrement(pixelCount);

    FragColor = color;
}
