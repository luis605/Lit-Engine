#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 1) buffer ExposureBuffer {
    float exposure;
};

layout(binding  = 0) uniform sampler2D avgLuminanceTex;

layout(location = 1) uniform float deltaTime;
layout(location = 2) uniform float targetLuminance = 0.6;
layout(location = 3) uniform float exposureSpeed   = 0.6;

void main() {
    float avgLuminance = texelFetch(avgLuminanceTex, ivec2(0, 0), 0).r;
    float newExposure = clamp(targetLuminance / (avgLuminance + 0.0001), 0.1, 5.0);
    exposure = exposure + (newExposure - exposure) * (deltaTime * exposureSpeed);
}