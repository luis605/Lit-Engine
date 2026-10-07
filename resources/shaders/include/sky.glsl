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

bool moonIsLight() { return celestialParams.w > 0.5; }

float nightFactor() { return saturate(-celestialParams.y * 6.0 + 0.3); }

vec3 skyLut(vec3 camPos, float viewHeight, vec3 up, vec3 dir, bool intersectGround) {
    vec3 side = cross(up, dir);
    float lightViewCos = 1.0;
    if (dot(side, side) > 1e-8) {
        side = normalize(side);
        vec3 forward = normalize(cross(side, up));
        vec2 lightOnPlane = vec2(dot(sunDirection.xyz, forward), dot(sunDirection.xyz, side));
        float len = length(lightOnPlane);
        lightViewCos = len > 1e-5 ? lightOnPlane.x / len : 1.0;
    }
    return textureLod(u_skyViewLut, skyViewUv(viewHeight, dot(dir, up), lightViewCos, intersectGround), 0.0).rgb * sunToaIlluminance();
}

vec3 zenithRadiance() {
    vec3 camPos = atmospherePosition(viewPos);
    float viewHeight = length(camPos);
    vec3 up = camPos / viewHeight;
    return skyLut(camPos, viewHeight, up, up, false);
}

vec3 sunTransmittanceAt(vec3 atmPos, vec3 sunDir) {
    float r = length(atmPos);
    vec3 up = atmPos / r;
    float mu = dot(sunDir, up);
    float horizonMu = -sqrt(max(1.0 - sq(ATM_BOTTOM / r), 0.0));
    float visible = smoothstep(horizonMu - 0.004, horizonMu + 0.004, mu);
    return transmittanceFromLut(u_transmittanceLut, r, max(mu, horizonMu + 0.002)) * visible;
}

vec3 physicalSkyRadiance(vec3 dir, bool includeSun) {
    vec3 camPos = atmospherePosition(viewPos);
    float viewHeight = length(camPos);
    vec3 up = camPos / viewHeight;
    vec3 sunDir = sunDirection.xyz;
    float horizonCos = -sqrt(max(1.0 - sq(ATM_BOTTOM / viewHeight), 0.0));
    float viewZenithCos = dot(dir, up);
    bool intersectGround = viewZenithCos < horizonCos;
    vec3 L;
    if (intersectGround) {
        vec3 level = dir - up * viewZenithCos;
        vec3 horizonDir = dot(level, level) > 1e-8 ? normalize(normalize(level) + up * (horizonCos + 0.004)) : up;
        vec3 horizon = skyLut(camPos, viewHeight, up, horizonDir, false);
        float t = max(raySphere(camPos, dir, ATM_BOTTOM), 0.0);
        vec3 groundUp = normalize(camPos + dir * t);
        vec3 sunAtGround = sunTransmittanceAt(groundUp * (ATM_BOTTOM + 0.001), sunDir) * sunToaIlluminance() * saturate(dot(groundUp, sunDir));
        vec3 skyIrradiance = zenithRadiance() * PI * 0.8;
        vec3 ground = skyParams2.w * INV_PI * (sunAtGround + skyIrradiance);
        vec3 inscatter = skyLut(camPos, viewHeight, up, dir, true);
        vec3 aerial = exp(-t * vec3(0.022, 0.03, 0.045) * 6.0);
        L = inscatter + ground * aerial + horizon * (1.0 - aerial) * 0.6;
        float blend = smoothstep(0.0, 0.18, horizonCos - viewZenithCos);
        L = mix(horizon, L, blend);
    } else {
        L = skyLut(camPos, viewHeight, up, dir, false);
    }
    if (includeSun && !moonIsLight() && !intersectGround) {
        float cosSun = dot(dir, sunDir);
        vec3 T = sunTransmittanceAt(camPos, sunDir);
        L += T * sunToaIlluminance() * (phaseHG(cosSun, 0.995) * 0.012 + phaseHG(cosSun, 0.93) * 0.004) * saturate(viewZenithCos * 40.0 + 1.0);
    }
    return L;
}

vec3 sunDiskRadiance(vec3 dir) {
    if (moonIsLight()) return vec3(0.0);
    vec3 camPos = atmospherePosition(viewPos);
    float viewHeight = length(camPos);
    vec3 up = camPos / viewHeight;
    vec3 sunDir = sunDirection.xyz;
    float radius = sunDirection.w;
    float cosSun = dot(dir, sunDir);
    if (cosSun < cos(radius * 1.3)) return vec3(0.0);
    float x = acos(clamp(cosSun, -1.0, 1.0)) / radius;
    float edge = 1.0 - smoothstep(0.85, 1.15, x);
    float mu = sqrt(max(1.0 - sq(min(x, 1.0)), 0.0));
    vec3 limb = pow(vec3(max(mu, 0.15)), vec3(0.397, 0.503, 0.652));
    vec3 T = sunTransmittanceAt(camPos, sunDir);
    float aboveHorizon = smoothstep(-0.004, 0.004, dot(dir, up) + sqrt(max(1.0 - sq(ATM_BOTTOM / viewHeight), 0.0)));
    return min(T * sunToaIlluminance() / (PI * radius * radius) * limb * edge * aboveHorizon, vec3(6.0e8));
}

float valueNoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i.xy + i.z * 37.1);
    float b = hash12(i.xy + vec2(1.0, 0.0) + i.z * 37.1);
    float c = hash12(i.xy + vec2(0.0, 1.0) + i.z * 37.1);
    float d = hash12(i.xy + vec2(1.0, 1.0) + i.z * 37.1);
    float e = hash12(i.xy + (i.z + 1.0) * 37.1);
    float g = hash12(i.xy + vec2(1.0, 0.0) + (i.z + 1.0) * 37.1);
    float h = hash12(i.xy + vec2(0.0, 1.0) + (i.z + 1.0) * 37.1);
    float k = hash12(i.xy + vec2(1.0, 1.0) + (i.z + 1.0) * 37.1);
    return mix(mix(mix(a, b, u.x), mix(c, d, u.x), u.y), mix(mix(e, g, u.x), mix(h, k, u.x), u.y), u.z);
}

vec3 moonRadiance(vec3 dir, vec3 camPos, float viewHeight, vec3 up) {
    if (skyParams3.w < 0.5) return vec3(0.0);
    vec3 m = moonDirection.xyz;
    float radius = moonDirection.w;
    float cosM = dot(dir, m);
    vec3 T = sunTransmittanceAt(camPos, m);
    vec3 glow = T * skyParams3.x * (phaseHG(cosM, 0.992) * 0.02 + phaseHG(cosM, 0.9) * 0.002) * vec3(0.8, 0.88, 1.0);
    if (cosM < cos(radius * 1.2)) return glow;
    vec3 right = normalize(cross(m, abs(m.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    vec3 upm = cross(right, m);
    vec2 q = vec2(dot(dir, right), dot(dir, upm)) / sin(radius);
    float r2 = dot(q, q);
    float edge = 1.0 - smoothstep(0.88, 1.05, sqrt(r2));
    vec3 n = normalize(q.x * right + q.y * upm - m * sqrt(max(1.0 - r2, 0.0)));
    float lit = smoothstep(-0.05, 0.25, dot(n, celestialParams.xyz));
    float maria = 0.62 + 0.38 * smoothstep(0.3, 0.7, valueNoise3(n * 3.0) * 0.65 + valueNoise3(n * 9.0) * 0.35);
    float crater = 0.9 + 0.1 * valueNoise3(n * 28.0);
    float earthshine = 0.012;
    vec3 disk = T * skyParams3.x / (PI * radius * radius) * 0.7 * maria * crater * (lit + earthshine) * edge * vec3(0.92, 0.94, 1.0);
    return disk + glow;
}

vec3 starColor(float t) {
    vec3 hot = vec3(0.62, 0.72, 1.0);
    vec3 white = vec3(1.0, 0.97, 0.92);
    vec3 cool = vec3(1.0, 0.72, 0.45);
    return t < 0.5 ? mix(hot, white, t * 2.0) : mix(white, cool, t * 2.0 - 1.0);
}

float skyFbm(vec3 p) {
    float sum = 0.0;
    float amplitude = 0.5;
    for (int o = 0; o < 4; ++o) {
        sum += valueNoise3(p) * amplitude;
        p *= 2.03;
        amplitude *= 0.5;
    }
    return sum / 0.9375;
}

vec3 milkyWay(vec3 dir) {
    vec3 bandNormal = normalize(vec3(0.35, 0.25, 0.9));
    vec3 core = normalize(cross(bandNormal, vec3(0.0, 1.0, 0.0)) + bandNormal * 0.05);
    float lat = dot(dir, bandNormal);
    float towardCore = dot(dir, core) * 0.5 + 0.5;
    float width = mix(0.16, 0.3, towardCore * towardCore);
    float band = exp(-sq(lat / width) * 1.6);
    float glow = skyFbm(dir * 6.0);
    float clumps = smoothstep(0.35, 0.85, skyFbm(dir * 22.0 + 3.1));
    float dust = smoothstep(0.45, 0.75, skyFbm(dir * 11.0 + 7.7)) * exp(-sq(lat / (width * 0.45)));
    float bulge = exp(-sq(acos(clamp(dot(dir, core), -1.0, 1.0)) / 0.35));
    vec3 tint = mix(vec3(0.55, 0.62, 0.85), vec3(1.0, 0.85, 0.65), saturate(bulge * 1.5 + towardCore * 0.3));
    float intensity = band * (0.35 + glow * 0.65) * (0.7 + clumps * 0.6) * (1.0 - dust * 0.85) + bulge * 0.8 * (1.0 - dust * 0.7);
    return tint * intensity * 0.0016;
}

vec3 starField(vec3 dir, float pixelAngle) {
    vec3 result = vec3(0.0);
    float horizon = saturate(dir.y);
    for (int layer = 0; layer < 3; ++layer) {
        float density = layer == 0 ? 90.0 : layer == 1 ? 200.0 : 420.0;
        vec3 p = dir * density;
        vec3 cell = floor(p);
        vec3 h = hash33(cell + float(layer) * 71.3);
        float bright = hash12(cell.xy * 1.37 + cell.z * 17.13 + float(layer) * 5.1);
        float threshold = layer == 0 ? 0.86 : layer == 1 ? 0.9 : 0.94;
        if (bright < threshold) continue;
        vec3 starDir = normalize(cell + 0.2 + 0.6 * h);
        vec3 local = starDir * density - cell;
        if (any(lessThan(local, vec3(0.1))) || any(greaterThan(local, vec3(0.9)))) continue;
        float size = max(pixelAngle * 0.7, 2.0e-4);
        float angle = length(cross(dir, starDir));
        float x = (bright - threshold) / (1.0 - threshold);
        float magnitude = pow(x, 6.0) * (layer == 0 ? 1.4 : layer == 1 ? 0.45 : 0.18);
        float scint = mix(0.1, 0.45, 1.0 - horizon);
        float twinkle = 1.0 - scint + scint * (sin(timeParams.x * (3.0 + h.x * 9.0) + h.y * 40.0) * 0.5 + 0.5) * (sin(timeParams.x * (5.3 + h.z * 7.0) + h.x * 13.0) * 0.5 + 0.5) * 2.0;
        vec3 tint = starColor(pow(h.z, 1.4));
        result += tint * magnitude * twinkle * exp(-sq(angle / size)) * sq(2.0e-4 / size) * 1.5;
    }
    vec3 bandNormal = normalize(vec3(0.35, 0.25, 0.9));
    result *= 1.0 + 1.5 * exp(-sq(dot(dir, bandNormal) / 0.2));
    result += milkyWay(dir);
    result += vec3(0.00025, 0.0005, 0.00032) * exp(-max(dir.y, 0.0) * 9.0);
    return result;
}

#ifndef CLOUD_TEXTURE
uniform sampler3D u_cloudNoise;

#ifdef SKY_ENV
const int CLOUD_STEPS = 32;
const int CLOUD_LIGHT_STEPS = 4;
const int CIRRUS_STEPS = 8;
#else
const int CLOUD_STEPS = 72;
const int CLOUD_LIGHT_STEPS = 5;
const int CIRRUS_STEPS = 12;
#endif
const float CLOUD_THICKNESS = 3400.0;
const float CIRRUS_OFFSET = 4600.0;
const float CIRRUS_THICKNESS = 700.0;
const float CLOUD_SIGMA = 0.045;

float g_cloudDither = 0.5;
float g_cloudPixelAngle = 0.004;
float g_cloudFootprint = 0.0;

float noiseLod(float period) { return log2(max(g_cloudFootprint * 128.0 / period, 1.0)); }

float remap(float v, float lo, float hi, float newLo, float newHi) { return newLo + (v - lo) / max(hi - lo, 1e-5) * (newHi - newLo); }

float cloudEvolve() { return cloudParams.z * 1.0e-4; }

vec3 cumulusPosition(vec3 p, float h) {
    vec3 wp = p + vec3(cloudParams.x, 0.0, cloudParams.y);
    wp.xz += vec2(0.35, 0.12) * h * CLOUD_THICKNESS;
    return wp;
}

vec2 cumulusWeather(vec3 wp) {
    float evolve = cloudEvolve();
    vec2 weatherUv = wp.xz * (1.0 / 70000.0);
    float weather = textureLod(u_cloudNoise, vec3(weatherUv.x, 0.31 + evolve * 3.0, weatherUv.y), max(noiseLod(70000.0), 2.0)).r;
    float kind = textureLod(u_cloudNoise, vec3(weatherUv * 1.7 + 0.37, 0.73 - evolve * 2.0), max(noiseLod(41000.0), 2.0)).r;
    float coverage = saturate(skyParams.w + (weather - 0.45) * 0.8);
    float top = mix(0.25, 1.0, saturate(kind * 1.6 - 0.25)) * mix(0.55, 1.0, coverage);
    return vec2(coverage, top);
}

float cumulusShape(vec3 wp, float h, vec2 weather) {
    if (h <= 0.0 || h >= weather.y) return 0.0;
    float bottom = saturate(remap(h, 0.0, 0.06, 0.0, 1.0));
    float fall = saturate(remap(h, weather.y * 0.35, weather.y, 1.0, 0.0));
    float profile = bottom * fall * fall * (3.0 - 2.0 * fall);
    vec4 n = textureLod(u_cloudNoise, (wp + vec3(0.0, cloudEvolve() * 60000.0, 0.0)) * vec3(1.0 / 15000.0, 1.0 / 7500.0, 1.0 / 15000.0), noiseLod(10000.0));
    float fbm = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float base = remap(n.r, fbm - 1.0, 1.0, 0.0, 1.0);
    return saturate(remap(base * profile, 1.0 - weather.x * 0.62, 1.0, 0.0, 1.0)) * mix(0.6, 1.0, saturate(h * 4.0));
}

float cumulusDetail(vec3 wp, float h, float c) {
    float evolve = cloudEvolve();
    vec4 dn = textureLod(u_cloudNoise, (wp + vec3(0.0, evolve * 36000.0, evolve * 18000.0)) * (1.0 / 1800.0), noiseLod(1800.0));
    float dfbm = dn.g * 0.625 + dn.b * 0.25 + dn.a * 0.125;
    float erode = mix(dfbm, 1.0 - dfbm, saturate(h * 3.0)) * 0.45;
    return saturate(remap(c, erode, 1.0, 0.0, 1.0));
}

float cumulusDensity(vec3 p, float h, bool detail) {
    if (h <= 0.0 || h >= 1.0) return 0.0;
    vec3 wp = cumulusPosition(p, h);
    float c = cumulusShape(wp, h, cumulusWeather(wp));
    if (c <= 0.0 || !detail) return c * skyParams2.z;
    return cumulusDetail(wp, h, c) * skyParams2.z;
}

float cirrusDensity(vec3 p, float h) {
    if (h <= 0.0 || h >= 1.0) return 0.0;
    float evolve = cloudEvolve();
    vec3 wp = p + vec3(cloudParams.x * 1.6 + 23000.0, 0.0, cloudParams.y * 0.6 - 9000.0);
    vec4 weather = textureLod(u_cloudNoise, vec3(wp.xz * (1.0 / 110000.0), 0.57 + evolve * 2.0).xzy, max(noiseLod(110000.0), 2.0));
    float coverage = saturate(skyParams.w * 0.8 + (weather.r - 0.5) * 0.9);
    vec4 n = textureLod(u_cloudNoise, (wp + vec3(0.0, evolve * 20000.0, 0.0)) * vec3(1.0 / 9000.0, 1.0 / 2500.0, 1.0 / 4500.0), noiseLod(5000.0));
    float fbm = n.g * 0.5 + n.b * 0.3 + n.a * 0.2;
    float profile = saturate(remap(h, 0.0, 0.3, 0.0, 1.0)) * saturate(remap(h, 0.6, 1.0, 1.0, 0.0));
    float c = saturate(remap(n.r * 0.6 + fbm * 0.4, 1.0 - coverage * 0.7, 1.0, 0.0, 1.0)) * profile;
    return c * skyParams2.z * 0.35;
}

float cloudDensity(vec3 p, float h, bool detail) { return cumulusDensity(p, h, detail); }

vec2 layerRange(vec3 dir, float yCam, float hBase, float thickness, float maxLength) {
    float hTop = hBase + thickness;
    float vy = max(dir.y, 0.002);
    float rc = 6360000.0 + yCam;
    float dhNear = max(hBase - yCam, 0.0);
    float dhFar = max(hTop - yCam, 10.0);
    float tNear = (2.0 * dhNear) / (vy + sqrt(vy * vy + (2.0 * dhNear) / rc));
    float tFar = (2.0 * dhFar) / (vy + sqrt(vy * vy + (2.0 * dhFar) / rc));
    if (yCam >= hBase && yCam <= hTop) tNear = 0.0;
    return vec2(tNear, min(tFar, tNear + maxLength));
}

float layerHeight(float t, float dirY, float yCam, float hBase, float thickness) {
    return (yCam + dirY * t - t * t / (2.0 * 6360000.0) - hBase) / thickness;
}

struct CloudLight {
    vec3 sunDir;
    float toa;
    float cosTheta;
    float phases[3];
    vec3 ambientTop;
    vec3 ambientBottom;
    float powderAmount;
};

CloudLight cloudLight(vec3 dir) {
    CloudLight L;
    L.sunDir = sunDirection.xyz;
    L.toa = sunToaIlluminance();
    L.cosTheta = dot(dir, L.sunDir);
    for (int k = 0; k < 3; ++k) {
        float c = pow(0.5, float(k));
        L.phases[k] = mix(phaseHG(L.cosTheta, 0.8 * c), phaseHG(L.cosTheta, -0.25 * c), 0.25) + phaseHG(L.cosTheta, 0.97) * 0.1 * c;
    }
    vec3 zenith = zenithRadiance();
    vec3 camAtm = atmospherePosition(viewPos);
    vec3 groundSun = sunTransmittanceAt(normalize(camAtm) * (ATM_BOTTOM + 0.001), L.sunDir) * L.toa * saturate(L.sunDir.y);
    L.ambientTop = zenith * 1.1;
    L.ambientBottom = skyParams2.w * INV_PI * (groundSun + zenith * PI) * 0.5 + zenith * 0.12;
    L.powderAmount = saturate(0.5 - L.cosTheta * 0.5);
    return L;
}

vec3 cloudInscatter(CloudLight L, vec3 sunLight, float h, float density, float opticalDepth, float heightShade) {
    float scatter = 0.0;
    float a = 1.0;
    float b = 1.0;
    for (int k = 0; k < 3; ++k) {
        scatter += a * L.phases[k] * max(exp(-opticalDepth * b), 0.2 * exp(-opticalDepth * b * 0.25));
        a *= 0.55;
        b *= 0.35;
    }
    float sigma = density * CLOUD_SIGMA;
    float powder = mix(1.0, 1.0 - exp(-sigma * 150.0), L.powderAmount * 0.6);
    vec3 ambient = mix(L.ambientBottom, L.ambientTop, saturate(h * 1.2)) * mix(0.45, 1.0, exp(-density * 1.5)) * heightShade;
    return sunLight * scatter * powder * 3.0 + ambient * 0.6;
}

vec4 cumulusLayer(vec3 dir, CloudLight L, out float distance) {
    distance = 0.0;
    float hBase = max(skyGradient[0].w, 300.0);
    float yCam = max(viewPos.y, 0.0);
    vec2 range = layerRange(dir, yCam, hBase, CLOUD_THICKNESS, 28000.0);
    if (range.y <= range.x || range.x > 120000.0) return vec4(0.0, 0.0, 0.0, 1.0);
    int steps = int(mix(float(CLOUD_STEPS) * 0.5, float(CLOUD_STEPS), saturate((range.y - range.x) / 9000.0)));
    float stepSize = (range.y - range.x) / float(steps);
    vec3 sunLight = sunTransmittanceAt(atmospherePosition(viewPos + dir * range.x), L.sunDir) * L.toa;
    float t = range.x + stepSize * g_cloudDither;
    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    float weightedT = 0.0;
    float weightSum = 0.0;
    int empty = 0;
    for (int i = 0; i < steps * 2 && t < range.y; ++i) {
        float h = layerHeight(t, dir.y, yCam, hBase, CLOUD_THICKNESS);
        g_cloudFootprint = t * g_cloudPixelAngle;
        vec3 wp = cumulusPosition(viewPos + dir * t, h);
        vec2 weather = cumulusWeather(wp);
        float c = cumulusShape(wp, h, weather);
        if (c <= 0.0) {
            ++empty;
            t += empty > 2 ? stepSize * 2.0 : stepSize;
            continue;
        }
        if (empty > 2) {
            empty = 0;
            t -= stepSize;
            continue;
        }
        empty = 0;
        float density = cumulusDetail(wp, h, c) * skyParams2.z;
        if (density > 0.001) {
            float opticalDepth = 0.0;
            float travelled = 0.0;
            float segment = 50.0;
            for (int j = 0; j < CLOUD_LIGHT_STEPS; ++j) {
                float mid = travelled + segment * 0.5;
                float hq = h + L.sunDir.y * mid / CLOUD_THICKNESS;
                vec3 q = wp + L.sunDir * mid + vec3(0.35, 0.0, 0.12) * (hq - h) * CLOUD_THICKNESS;
                float lc = cumulusShape(q, hq, weather);
                if (lc > 0.0 && j < 1) lc = cumulusDetail(q, hq, lc);
                opticalDepth += lc * skyParams2.z * segment;
                travelled += segment;
                segment *= 2.0;
            }
            opticalDepth *= CLOUD_SIGMA * 0.3;
            vec3 S = cloudInscatter(L, sunLight, h, density, opticalDepth, mix(0.55, 1.0, saturate(h * 1.5)));
            float stepT = exp(-density * CLOUD_SIGMA * stepSize);
            float absorbed = transmittance * (1.0 - stepT);
            color += S * absorbed;
            weightedT += t * absorbed;
            weightSum += absorbed;
            transmittance *= stepT;
            if (transmittance < 0.01) break;
        }
        t += stepSize;
    }
    if (weightSum > 0.0) distance = weightedT / weightSum;
    return vec4(color, transmittance);
}

vec4 cirrusLayer(vec3 dir, CloudLight L, out float distance) {
    distance = 0.0;
    float hBase = max(skyGradient[0].w, 300.0) + CIRRUS_OFFSET;
    float yCam = max(viewPos.y, 0.0);
    vec2 range = layerRange(dir, yCam, hBase, CIRRUS_THICKNESS, 30000.0);
    if (range.y <= range.x || range.x > 160000.0) return vec4(0.0, 0.0, 0.0, 1.0);
    float stepSize = (range.y - range.x) / float(CIRRUS_STEPS);
    vec3 sunLight = sunTransmittanceAt(atmospherePosition(viewPos + dir * range.x), L.sunDir) * L.toa;
    float t = range.x + stepSize * g_cloudDither;
    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    float weightedT = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < CIRRUS_STEPS; ++i) {
        float h = layerHeight(t, dir.y, yCam, hBase, CIRRUS_THICKNESS);
        g_cloudFootprint = t * g_cloudPixelAngle;
        vec3 p = viewPos + dir * t;
        float density = cirrusDensity(p, h);
        if (density > 0.001) {
            float opticalDepth = (cirrusDensity(p + L.sunDir * 150.0, h + L.sunDir.y * 150.0 / CIRRUS_THICKNESS) * 300.0 + density * 100.0) * CLOUD_SIGMA * 0.3;
            vec3 S = cloudInscatter(L, sunLight, h, density, opticalDepth, 1.0);
            float stepT = exp(-density * CLOUD_SIGMA * stepSize);
            float absorbed = transmittance * (1.0 - stepT);
            color += S * absorbed;
            weightedT += t * absorbed;
            weightSum += absorbed;
            transmittance *= stepT;
        }
        t += stepSize;
    }
    if (weightSum > 0.0) distance = weightedT / weightSum;
    return vec4(color, transmittance);
}

vec4 applyCloudHaze(vec4 c, float distance, vec3 dir) {
    if (distance <= 0.0) return c;
    float haze = 1.0 - exp(-distance / 38000.0);
    vec3 camPos = atmospherePosition(viewPos);
    float viewHeight = length(camPos);
    vec3 horizonSky = skyLut(camPos, viewHeight, camPos / viewHeight, dir, false);
    c.rgb = mix(c.rgb, horizonSky * (1.0 - c.a), haze);
    return c;
}

vec4 cloudLayer(vec3 dir) {
    if (skyParams.w <= 0.001 || skyParams2.z <= 0.001 || dir.y <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    CloudLight L = cloudLight(dir);
    float lowDistance, highDistance;
    vec4 low = applyCloudHaze(cumulusLayer(dir, L, lowDistance), lowDistance, dir);
    vec4 high = low.a > 0.004 ? applyCloudHaze(cirrusLayer(dir, L, highDistance), highDistance, dir) : vec4(0.0, 0.0, 0.0, 1.0);
    vec4 c = vec4(low.rgb + high.rgb * low.a, low.a * high.a);
    float horizonFade = smoothstep(0.0, 0.05, dir.y);
    return vec4(c.rgb * horizonFade, mix(1.0, c.a, horizonFade));
}

vec4 cloudsForDirection(vec3 dir) { return cloudLayer(dir); }
#else
uniform sampler2D u_clouds;
vec2 g_cloudUv = vec2(0.5);

vec4 sampleCloudTexture(vec2 uv) {
    vec2 texel = 1.0 / vec2(textureSize(u_clouds, 0));
    vec4 c = textureLod(u_clouds, uv, 0.0) * 0.4;
    c += textureLod(u_clouds, uv + vec2(0.75, 0.25) * texel, 0.0) * 0.15;
    c += textureLod(u_clouds, uv + vec2(-0.25, 0.75) * texel, 0.0) * 0.15;
    c += textureLod(u_clouds, uv + vec2(-0.75, -0.25) * texel, 0.0) * 0.15;
    c += textureLod(u_clouds, uv + vec2(0.25, -0.75) * texel, 0.0) * 0.15;
    return c;
}
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
    vec3 camPos = atmospherePosition(viewPos);
    float viewHeight = length(camPos);
    vec3 up = camPos / viewHeight;
    float night = nightFactor();
    L += vec3(0.0006, 0.0009, 0.0018) * night;
    vec3 celestial = vec3(0.0);
    if (dir.y > -0.01) {
        celestial += moonRadiance(dir, camPos, viewHeight, up);
#ifndef SKY_ENV
        if (skyParams3.y > 0.5 && night > 0.0) celestial += starField(dir, 2.0 / (projection[1][1] * screenParams.y)) * night * sunTransmittanceAt(camPos, dir);
#endif
    }
    vec3 disk = includeSun ? sunDiskRadiance(dir) : vec3(0.0);
#ifdef CLOUD_TEXTURE
    vec4 cloud = sampleCloudTexture(g_cloudUv);
#else
    vec4 cloud = cloudsForDirection(dir);
#endif
    return (L + celestial) * cloud.a + disk * sq(sq(cloud.a)) + cloud.rgb;
}

#endif
