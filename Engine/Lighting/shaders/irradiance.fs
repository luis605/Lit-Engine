/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

layout(location = 0) in  vec3 fragPosition;
layout(location = 0) out vec4 FragColor;

uniform samplerCube environmentMap;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N) {
    return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

mat3 GetTangentSpace(vec3 N) {
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return mat3(tangent, bitangent, N);
}

vec3 CosineSampleHemisphere(vec2 Xi, mat3 TBN) {
    float r = sqrt(Xi.x);
    float phi = 2.0 * PI * Xi.y;

    vec3 L_tangent;
    L_tangent.x = r * cos(phi);
    L_tangent.y = r * sin(phi);
    L_tangent.z = sqrt(max(0.0, 1.0 - Xi.x));

    return normalize(TBN * L_tangent);
}

void main() {
    vec3 N = normalize(fragPosition);
    vec3 irradianceSum = vec3(0.0);
    mat3 TBN = GetTangentSpace(N);

    for(uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 L = CosineSampleHemisphere(Xi, TBN);
        vec3 sampleColor = texture(environmentMap, L).rgb;
        irradianceSum += sampleColor;
    }

    vec3 E = irradianceSum / float(SAMPLE_COUNT);

    FragColor = vec4(E, 1.0);
}