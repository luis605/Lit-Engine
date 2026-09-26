#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene.glsl"

uniform sampler2D u_ldr;

layout(location = 0) out vec4 out_color;

float lumaAt(vec2 uv) { return textureLod(u_ldr, uv, 0.0).a; }

void main() {
    vec2 uv = gl_FragCoord.xy * screenParams.zw;
    vec2 texel = screenParams.zw;
    vec4 center = textureLod(u_ldr, uv, 0.0);
    if (ssrParams.w < 0.5) {
        out_color = vec4(center.rgb, 1.0);
        return;
    }
    float lumaM = center.a;
    float lumaN = lumaAt(uv + vec2(0.0, -texel.y));
    float lumaS = lumaAt(uv + vec2(0.0, texel.y));
    float lumaE = lumaAt(uv + vec2(texel.x, 0.0));
    float lumaW = lumaAt(uv + vec2(-texel.x, 0.0));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float range = rangeMax - rangeMin;
    if (range < max(0.0312, rangeMax * 0.125)) {
        out_color = vec4(center.rgb, 1.0);
        return;
    }
    float lumaNW = lumaAt(uv + vec2(-texel.x, -texel.y));
    float lumaNE = lumaAt(uv + vec2(texel.x, -texel.y));
    float lumaSW = lumaAt(uv + vec2(-texel.x, texel.y));
    float lumaSE = lumaAt(uv + vec2(texel.x, texel.y));
    float edgeH = abs(lumaNW + lumaNE - 2.0 * lumaN) + 2.0 * abs(lumaW + lumaE - 2.0 * lumaM) + abs(lumaSW + lumaSE - 2.0 * lumaS);
    float edgeV = abs(lumaNW + lumaSW - 2.0 * lumaW) + 2.0 * abs(lumaN + lumaS - 2.0 * lumaM) + abs(lumaNE + lumaSE - 2.0 * lumaE);
    bool horizontal = edgeH >= edgeV;
    float luma1 = horizontal ? lumaN : lumaW;
    float luma2 = horizontal ? lumaS : lumaE;
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;
    bool steepest1 = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    float stepLength = horizontal ? texel.y : texel.x;
    float lumaLocalAverage;
    if (steepest1) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaM);
    }
    vec2 currentUv = uv;
    if (horizontal) currentUv.y += stepLength * 0.5;
    else currentUv.x += stepLength * 0.5;
    vec2 offset = horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 uv1 = currentUv - offset;
    vec2 uv2 = currentUv + offset;
    float lumaEnd1 = lumaAt(uv1) - lumaLocalAverage;
    float lumaEnd2 = lumaAt(uv2) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    const float quality[12] = float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);
    for (int i = 2; i < 12 && !(reached1 && reached2); ++i) {
        if (!reached1) {
            uv1 -= offset * quality[i];
            lumaEnd1 = lumaAt(uv1) - lumaLocalAverage;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += offset * quality[i];
            lumaEnd2 = lumaAt(uv2) - lumaLocalAverage;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
    }
    float distance1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;
    float distance2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;
    bool direction1 = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeThickness = distance1 + distance2;
    float pixelOffset = -distanceFinal / edgeThickness + 0.5;
    bool isLumaCenterSmaller = lumaM < lumaLocalAverage;
    bool correctVariation = ((direction1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaN + lumaS + lumaE + lumaW) + lumaNW + lumaNE + lumaSW + lumaSE);
    float subPixelOffset1 = saturate(abs(lumaAverage - lumaM) / range);
    float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
    float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * 0.75;
    finalOffset = max(finalOffset, subPixelOffsetFinal);
    vec2 finalUv = uv;
    if (horizontal) finalUv.y += finalOffset * stepLength;
    else finalUv.x += finalOffset * stepLength;
    out_color = vec4(textureLod(u_ldr, finalUv, 0.0).rgb, 1.0);
}
