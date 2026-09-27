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

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(
            mix(hash13(i + vec3(0.0, 0.0, 0.0)), hash13(i + vec3(1.0, 0.0, 0.0)), u.x),
            mix(hash13(i + vec3(0.0, 1.0, 0.0)), hash13(i + vec3(1.0, 1.0, 0.0)), u.x),
            u.y
        ),
        mix(
            mix(hash13(i + vec3(0.0, 0.0, 1.0)), hash13(i + vec3(1.0, 0.0, 1.0)), u.x),
            mix(hash13(i + vec3(0.0, 1.0, 1.0)), hash13(i + vec3(1.0, 1.0, 1.0)), u.x),
            u.y
        ),
        u.z
    );
}

float cloudFbm3D(vec3 p) {
    float f = 0.52 * noise3D(p); p = p * 2.02 + vec3(0.15, 0.32, 0.27);
    f += 0.28 * noise3D(p); p = p * 2.03 + vec3(0.24, 0.18, 0.41);
    f += 0.14 * noise3D(p); p = p * 2.01 + vec3(0.31, 0.42, 0.13);
    f += 0.06 * noise3D(p);
    return f;
}

float billow3D(vec3 p) {
    float f = 0.55 * abs(noise3D(p) * 2.0 - 1.0); p = p * 2.02 + vec3(0.21, 0.14, 0.35);
    f += 0.30 * abs(noise3D(p) * 2.0 - 1.0); p = p * 2.03 + vec3(0.12, 0.43, 0.19);
    f += 0.15 * abs(noise3D(p) * 2.0 - 1.0);
    return f;
}

float sampleCloudDensity(vec3 p, float h, float coverage, float densityMultiplier, vec3 wind) {
    if (h <= 0.0 || h >= 1.0) return 0.0;
    float profile = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.45, h);
    vec3 coord = (p + wind) * 0.00022;
    float base = cloudFbm3D(coord);
    float threshold = clamp(1.0 - coverage, 0.0, 0.98);
    if (base < threshold) return 0.0;
    vec3 detailCoord = (p + wind * 1.4) * 0.00075;
    float detail = billow3D(detailCoord);
    float shape = base - detail * 0.25 * (1.0 - h * 0.5);
    float d = saturate((shape - threshold) / max(coverage, 0.05)) * profile;
    return d * densityMultiplier;
}

float g_cloudDither = 0.5;

vec4 cloudLayer(vec3 dir, vec3 skyAmbient) {
    if (skyParams.w <= 0.001 || dir.y <= 0.01) return vec4(0.0, 0.0, 0.0, 1.0);
    float altitude = max(skyGradient[0].w, 400.0);
    float thickness = 1400.0;
    float hBase = altitude;
    float hTop = hBase + thickness;

    float vy = max(dir.y, 0.003);
    float yCam = max(viewPos.y, 0.0);
    float rc = 6360000.0 + yCam;

    float dhNear = max(hBase - yCam, 0.0);
    float dhFar = max(hTop - yCam, 10.0);

    float tNear = (2.0 * dhNear) / (vy + sqrt(vy * vy + (2.0 * dhNear) / rc));
    float tFar = (2.0 * dhFar) / (vy + sqrt(vy * vy + (2.0 * dhFar) / rc));

    if (yCam >= hBase && yCam <= hTop) tNear = 0.0;

    if (tFar <= tNear) return vec4(0.0, 0.0, 0.0, 1.0);

    tFar = min(tFar, tNear + 12000.0);
    float coverage = skyParams.w;
    vec3 sunDir = sunDirection.xyz;
    vec3 sunLight = sunIlluminance.rgb * sunIlluminance.w;
#ifdef CLOUD_CHEAP
    int steps = 6;
#else
    int steps = 12;
#endif
    float stepSize = (tFar - tNear) / float(steps);
    float t = tNear + stepSize * g_cloudDither;
    vec3 wind = vec3(1.0, 0.0, 0.35) * sceneTime() * skyParams2.x;
    float densityMult = skyParams2.z;
    float sigma = 3.5 / thickness;
    float cosTheta = dot(dir, sunDir);
    float phase = mix(phaseHG(cosTheta, 0.76), phaseHG(cosTheta, -0.22), 0.25) * 4.0 * PI;
    float phaseSoft = mix(phaseHG(cosTheta, 0.3), 1.0 / (4.0 * PI), 0.5) * 4.0 * PI;
    vec3 accumColor = vec3(0.0);
    float accumTransmittance = 1.0;
    float invThickness = 1.0 / thickness;
    for (int i = 0; i < steps; ++i) {
        vec3 p = viewPos + dir * t;
        float h = saturate((yCam + dir.y * t - (t * t) / (2.0 * rc) - hBase) * invThickness);
        float density = sampleCloudDensity(p, h, coverage, densityMult, wind);
        if (density > 0.0001) {
            float d1 = sampleCloudDensity(p + sunDir * 120.0, saturate(h + sunDir.y * 120.0 * invThickness), coverage, densityMult, wind);
#ifdef CLOUD_CHEAP
            float d2 = d1;
#else
            float d2 = sampleCloudDensity(p + sunDir * 420.0, saturate(h + sunDir.y * 420.0 * invThickness), coverage, densityMult, wind);
#endif
            float od = sigma * (density * 60.0 + d1 * 180.0 + d2 * 300.0);
            float beer = max(exp(-od), 0.3 * exp(-od * 0.25));
            float powder = 1.0 - exp(-density * 4.0);
            vec3 directLight = sunLight * INV_PI * beer * mix(phaseSoft, phase, beer) * mix(1.0, powder, 0.4);
            vec3 ambient = skyAmbient * mix(0.55, 1.1, h);
            float stepTransmittance = exp(-density * sigma * stepSize);
            accumColor += accumTransmittance * (directLight + ambient) * (1.0 - stepTransmittance);
            accumTransmittance *= stepTransmittance;
            if (accumTransmittance < 0.01) break;
        }
        t += stepSize;
    }
    float horizonFade = saturate((dir.y - 0.01) * 8.0);
    accumColor *= horizonFade;
    accumTransmittance = mix(1.0, accumTransmittance, horizonFade);
    return vec4(accumColor, accumTransmittance);
}

vec4 cloudsForDirection(vec3 dir) {
    float night = saturate(-sunDirection.y * 6.0 + 0.3);
    vec3 ambient = physicalSkyRadiance(vec3(0.0, 1.0, 0.0), false) * 0.6 + vec3(0.002, 0.003, 0.006) * night;
    return cloudLayer(dir, ambient);
}

#ifdef CLOUD_TEXTURE
uniform sampler2D u_clouds;
vec2 g_cloudUv = vec2(0.5);
#endif

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
#ifdef CLOUD_TEXTURE
    vec4 cloud = textureLod(u_clouds, g_cloudUv, 0.0);
#else
    vec4 cloud = cloudsForDirection(dir);
#endif
    return L * cloud.a + cloud.rgb;
}

#endif