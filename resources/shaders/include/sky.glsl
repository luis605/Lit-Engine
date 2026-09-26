#ifndef LIT_SKY_GLSL
#define LIT_SKY_GLSL

#include "atmosphere.glsl"
#include "scene.glsl"

uniform sampler2D u_transmittanceLut;
uniform sampler2D u_skyViewLut;
uniform sampler2D u_hdri;

const float ENV_SCALE = 1.0e-3;

AtmosphereScales atmosphereScales() { return AtmosphereScales(atmosphereParams.x, atmosphereParams.y, atmosphereParams.z, atmosphereParams.w, skyParams2.w); }

float sunToaIlluminance() { return skyGradient[2].w; }

vec3 physicalSkyRadiance(vec3 dir, bool includeSun) {
    vec3 camPos = atmospherePosition(viewPos);
    float viewHeight = length(camPos);
    vec3 up = camPos / viewHeight;
    float viewZenithCos = dot(dir, up);
    vec3 sunDir = sunDirection.xyz;
    vec3 side = cross(up, dir);
    float lightViewCos = 1.0;
    if (dot(side, side) > 1e-8) {
        side = normalize(side);
        vec3 forward = normalize(cross(side, up));
        vec2 lightOnPlane = vec2(dot(sunDir, forward), dot(sunDir, side));
        float len = length(lightOnPlane);
        lightViewCos = len > 1e-5 ? lightOnPlane.x / len : 1.0;
    }
    bool intersectGround = raySphere(camPos, dir, ATM_BOTTOM) >= 0.0;
    vec3 L = textureLod(u_skyViewLut, skyViewUv(viewHeight, viewZenithCos, lightViewCos, intersectGround), 0.0).rgb * sunToaIlluminance();
    if (intersectGround) {
        float t = raySphere(camPos, dir, ATM_BOTTOM);
        vec3 hit = camPos + dir * t;
        vec3 groundUp = normalize(hit);
        float haze = exp(-t * 0.12);
        vec3 sunAtGround = transmittanceFromLut(u_transmittanceLut, ATM_BOTTOM + 0.001, dot(sunDir, groundUp)) * sunToaIlluminance();
        L += skyParams2.w * INV_PI * sunAtGround * saturate(dot(groundUp, sunDir)) * haze;
    }
    if (includeSun && !intersectGround) {
        float cosSun = dot(dir, sunDir);
        float radius = sunDirection.w;
        float cosRadius = cos(radius);
        if (cosSun > cosRadius) {
            vec3 T = transmittanceFromLut(u_transmittanceLut, viewHeight, dot(sunDir, up));
            float x = saturate(acos(clamp(cosSun, -1.0, 1.0)) / radius);
            float mu = sqrt(max(1.0 - x * x, 0.0));
            vec3 limb = pow(vec3(mu), vec3(0.397, 0.503, 0.652));
            vec3 disk = T * sunToaIlluminance() / (PI * radius * radius) * limb;
            L += min(disk, vec3(6.0e8));
        }
    }
    return L;
}

vec3 starField(vec3 dir) {
    vec3 cell = floor(dir * 260.0);
    float h = hash12(cell.xy * 1.37 + cell.z * 17.13);
    if (h < 0.9965) return vec3(0.0);
    float strength = pow((h - 0.9965) / 0.0035, 6.0);
    vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.85, 0.7), hash12(cell.zy));
    return tint * strength * 0.06;
}

vec4 cloudLayer(vec3 dir, vec3 skyAmbient) {
    if (skyParams.w <= 0.001 || dir.y <= 0.015) return vec4(0.0, 0.0, 0.0, 1.0);
    float altitude = skyGradient[0].w;
    float t = (altitude - viewPos.y) / dir.y;
    if (t <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 p = viewPos + dir * t;
    vec2 wind = vec2(1.0, 0.35) * sceneTime() * skyParams2.x;
    vec2 uv = (p.xz + wind) * 0.00022;
    float coverage = skyParams.w;
    float n = cloudFbm(uv);
    float density = saturate((n - (1.0 - coverage)) / max(coverage, 0.05)) * skyParams2.z;
    density *= saturate((dir.y - 0.015) * 8.0);
    float transmittance = exp(-density * 3.2);
    vec3 sunDir = sunDirection.xyz;
    float nSun = cloudFbm(uv + sunDir.xz * 0.06);
    float selfShadow = exp(-saturate((nSun - (1.0 - coverage)) / max(coverage, 0.05)) * skyParams2.z * 2.4);
    float cosTheta = dot(dir, sunDir);
    float phase = mix(phaseHG(cosTheta, 0.65), phaseHG(cosTheta, -0.25), 0.35) * 4.0 * PI;
    float powder = 1.0 - exp(-density * 6.0);
    vec3 sunLight = sunIlluminance.rgb * sunIlluminance.w;
    vec3 color = sunLight * INV_PI * 0.85 * selfShadow * (0.35 + 0.65 * phase) * mix(1.0, powder, 0.5) + skyAmbient * 0.85;
    return vec4(color * (1.0 - transmittance), transmittance);
}

vec3 skyRadiance(vec3 dir, bool includeSun) {
    int mode = int(skyParams.x + 0.5);
    vec3 L;
    if (mode == 1) {
        float up = dir.y;
        vec3 zenith = skyGradient[0].rgb;
        vec3 horizon = skyGradient[1].rgb;
        vec3 ground = skyGradient[2].rgb;
        vec3 c = up >= 0.0 ? mix(horizon, zenith, pow(saturate(up), 0.45)) : mix(horizon, ground, pow(saturate(-up), 0.35));
        L = c * skyParams.y;
        if (includeSun) {
            float cosSun = dot(dir, sunDirection.xyz);
            if (cosSun > cos(sunDirection.w) && dir.y > 0.0) L += sunIlluminance.rgb / (PI * sunDirection.w * sunDirection.w);
        }
        return L;
    }
    if (mode == 2) {
        float u = atan(dir.z, dir.x) / TWO_PI + 0.5 + skyParams.z / 360.0;
        float v = acos(clamp(dir.y, -1.0, 1.0)) / PI;
        return textureLod(u_hdri, vec2(fract(u), v), 0.0).rgb * skyParams.y;
    }
    L = physicalSkyRadiance(dir, includeSun);
    float night = saturate(-sunDirection.y * 6.0 + 0.3);
    L += vec3(0.0025, 0.0035, 0.0065) * night;
    if (skyParams2.y > 0.5 && dir.y > 0.0) L += starField(dir) * night;
    vec3 ambient = physicalSkyRadiance(vec3(0.0, 1.0, 0.0), false) * 0.6 + vec3(0.002, 0.003, 0.006) * night;
    vec4 cloud = cloudLayer(dir, ambient);
    return L * cloud.a + cloud.rgb;
}

#endif
