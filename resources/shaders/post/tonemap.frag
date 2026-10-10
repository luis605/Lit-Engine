#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"

uniform sampler2D u_hdr;
uniform sampler2D u_bloom;
uniform sampler2D u_depth;
uniform usampler2D u_gbuffer2;

layout(std430) readonly buffer ExposureBuffer {
    vec4 exposureData;
};

layout(location = 0) out vec4 out_color;

vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 c, bool punchy) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    c = inset * max(c, vec3(1e-10));
    c = clamp(log2(c), minEv, maxEv);
    c = (c - minEv) / (maxEv - minEv);
    c = agxContrast(c);
    if (punchy) {
        float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = pow(max(c, vec3(0.0)), vec3(1.35));
        c = luma + 1.4 * (c - luma);
    }
    c = outset * c;
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 aces(vec3 c) {
    const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    c = inputMatrix * c;
    vec3 a = c * (c + 0.0245786) - 0.000090537;
    vec3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
    return saturate(outputMatrix * (a / b));
}

vec3 pbrNeutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

vec3 kelvinToRgb(float kelvin) {
    float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    vec3 c;
    c.r = t <= 66.0 ? 1.0 : saturate(1.29293618606 * pow(t - 60.0, -0.1332047592));
    c.g = t <= 66.0 ? saturate(0.39008157876 * log(t) - 0.63184144378) : saturate(1.12989086089 * pow(t - 60.0, -0.0755148492));
    c.b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : saturate(0.54320678911 * log(t - 10.0) - 1.19625408914));
    return c;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy * screenParams.zw;
    vec3 hdr = texelFetch(u_hdr, pixel, 0).rgb;
    int debugMode = debugViewMode();
    bool raw = debugMode != 0 && debugMode != 7 && debugMode != 8;
    vec3 color;
    if (raw) {
        color = hdr;
    } else {
        float ratio = exposureData.y / max(exposureData.x, 1e-12);
        color = hdr * ratio;
        if (postParams.x > 0.0) color = mix(color, textureLod(u_bloom, uv, 0.0).rgb * ratio, postParams.x);
        vec3 balance = kelvinToRgb(6500.0) / max(kelvinToRgb(postParams2.z), vec3(1e-3));
        balance *= vec3(1.0 + postParams2.w * 0.1, 1.0 - postParams2.w * 0.1, 1.0 + postParams2.w * 0.1);
        color *= balance / max(luminance(balance), 1e-3);
        float luma = luminance(color);
        color = max(luma + (color - luma) * postParams2.y, vec3(0.0));
        color = 0.18 * pow(max(color / 0.18, vec3(0.0)), vec3(postParams2.x));
        bool masked = texelFetch(u_depth, pixel, 0).r < 1.0 && (texelFetch(u_gbuffer2, pixel, 0).w & 128u) != 0u;
        if (masked) {
            color = 0.18 * pow(max(color / 0.18, vec3(0.0)), vec3(maskParams.x));
            vec2 aspectUv = uv * vec2(screenParams.x * screenParams.w, 1.0);
            float along = dot(aspectUv, vec2(cos(maskColor.w), sin(maskColor.w)));
            float phase = fract(along * 0.7 - sceneTime() * maskParams.z) - 0.5;
            float band = exp(-phase * phase / max(maskParams.w * maskParams.w, 1e-5));
            color += maskColor.rgb * band * maskParams.y * (0.1 + luminance(color));
        }
        vec2 v = uv - 0.5;
        float vignetteOuter = max(vignetteParams.x, 1e-3);
        float vignetteInner = vignetteOuter * (1.0 - clamp(vignetteParams.y, 0.0, 0.999));
        color *= mix(1.0, smoothstep(vignetteOuter, vignetteInner, length(v * vec2(screenParams.x * screenParams.w, 1.0))), postParams.y);
        int op = int(postParams.w + 0.5);
        if (op == 0) color = agx(color, false);
        else if (op == 1) color = agx(color, true);
        else if (op == 2) color = aces(color * 0.8);
        else if (op == 3) color = pbrNeutral(color);
        else if (op == 4) color = color / (1.0 + luminance(color));
        color = saturate(color);
        if (postParams.z > 0.0) color += (hash12(gl_FragCoord.xy + fract(sceneTime()) * 173.0) - 0.5) * postParams.z * 0.1;
    }
    vec3 display = linearToSrgb(saturate(color));
    display += (interleavedGradientNoise(gl_FragCoord.xy, frameIndex()) - 0.5) / 255.0;
    out_color = vec4(display, dot(display, vec3(0.299, 0.587, 0.114)));
}
