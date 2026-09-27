#ifndef LIT_ATMOSPHERE_GLSL
#define LIT_ATMOSPHERE_GLSL

#include "common.glsl"

const float ATM_BOTTOM = 6360.0;
const float ATM_TOP = 6460.0;
const vec3 RAYLEIGH_SCATTERING = vec3(5.802, 13.558, 33.1) * 1e-3;
const float MIE_SCATTERING = 3.996e-3;
const float MIE_EXTINCTION = 4.440e-3;
const vec3 OZONE_ABSORPTION = vec3(0.650, 1.881, 0.085) * 1e-3;
const vec2 TRANSMITTANCE_LUT_SIZE = vec2(256.0, 64.0);
const vec2 MULTISCATTER_LUT_SIZE = vec2(32.0, 32.0);
const vec2 SKYVIEW_LUT_SIZE = vec2(384.0, 216.0);
const float CAMERA_BASE_ALTITUDE_KM = 0.15;

struct AtmosphereScales {
    float rayleigh;
    float mie;
    float ozone;
    float mieG;
    float groundAlbedo;
};

struct Medium {
    vec3 scattering;
    vec3 extinction;
    vec3 rayleigh;
    float mie;
};

Medium sampleMedium(float heightKm, AtmosphereScales s) {
    float h = max(heightKm, 0.0);
    float densityR = exp(-h / 8.0);
    float densityM = exp(-h / 1.2);
    float densityO = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    Medium m;
    m.rayleigh = RAYLEIGH_SCATTERING * s.rayleigh * densityR;
    m.mie = MIE_SCATTERING * s.mie * densityM;
    m.scattering = m.rayleigh + vec3(m.mie);
    m.extinction = m.rayleigh + vec3(MIE_EXTINCTION * s.mie * densityM) + OZONE_ABSORPTION * s.ozone * densityO;
    return m;
}

float raySphere(vec3 ro, vec3 rd, float radius) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return -1.0;
    float s = sqrt(d);
    float t0 = -b - s;
    float t1 = -b + s;
    if (t0 >= 0.0) return t0;
    if (t1 >= 0.0) return t1;
    return -1.0;
}

float phaseRayleigh(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }

float phaseMie(float c, float g) {
    float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + c * c) / pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5);
}

float phaseHG(float c, float g) { return (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5)); }

vec2 transmittanceUv(float r, float mu) {
    float H = sqrt(ATM_TOP * ATM_TOP - ATM_BOTTOM * ATM_BOTTOM);
    float rho = sqrt(max(0.0, r * r - ATM_BOTTOM * ATM_BOTTOM));
    float discriminant = r * r * (mu * mu - 1.0) + ATM_TOP * ATM_TOP;
    float d = max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
    float dMin = ATM_TOP - r;
    float dMax = rho + H;
    return vec2((d - dMin) / max(dMax - dMin, 1e-6), rho / H);
}

void transmittanceParams(vec2 uv, out float r, out float mu) {
    float H = sqrt(ATM_TOP * ATM_TOP - ATM_BOTTOM * ATM_BOTTOM);
    float rho = H * uv.y;
    r = sqrt(rho * rho + ATM_BOTTOM * ATM_BOTTOM);
    float dMin = ATM_TOP - r;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

vec3 transmittanceFromLut(sampler2D lut, float r, float mu) { return textureLod(lut, transmittanceUv(r, mu), 0.0).rgb; }

vec3 multiScatterFromLut(sampler2D lut, float r, float sunZenithCos) {
    vec2 uv = vec2(sunZenithCos * 0.5 + 0.5, saturate((r - ATM_BOTTOM) / (ATM_TOP - ATM_BOTTOM)));
    uv = (uv * (MULTISCATTER_LUT_SIZE - 1.0) + 0.5) / MULTISCATTER_LUT_SIZE;
    return textureLod(lut, uv, 0.0).rgb;
}

vec2 skyViewUv(float viewHeight, float viewZenithCos, float lightViewCos, bool intersectGround) {
    float vHorizon = sqrt(max(viewHeight * viewHeight - ATM_BOTTOM * ATM_BOTTOM, 0.0));
    float cosBeta = vHorizon / viewHeight;
    float beta = acos(clamp(cosBeta, -1.0, 1.0));
    float zenithHorizonAngle = PI - beta;
    vec2 uv;
    if (!intersectGround) {
        float coord = acos(clamp(viewZenithCos, -1.0, 1.0)) / zenithHorizonAngle;
        coord = 1.0 - coord;
        coord = sqrt(max(coord, 0.0));
        coord = 1.0 - coord;
        uv.y = coord * 0.5;
    } else {
        float coord = (acos(clamp(viewZenithCos, -1.0, 1.0)) - zenithHorizonAngle) / beta;
        coord = sqrt(max(coord, 0.0));
        uv.y = coord * 0.5 + 0.5;
    }
    uv.x = sqrt(clamp(-lightViewCos * 0.5 + 0.5, 0.0, 1.0));
    return (uv * (SKYVIEW_LUT_SIZE - 1.0) + 0.5) / SKYVIEW_LUT_SIZE;
}

vec3 atmospherePosition(vec3 worldMeters) { return vec3(0.0, ATM_BOTTOM + CAMERA_BASE_ALTITUDE_KM + max(worldMeters.y, -100.0) * 0.001, 0.0) + vec3(worldMeters.x, 0.0, worldMeters.z) * 0.001; }

vec3 integrateScattering(vec3 pos, vec3 dir, vec3 sunDir, float tMaxLimit, int steps, AtmosphereScales scales, sampler2D transmittanceLut, sampler2D multiScatterLut, bool useMultiScatter, out vec3 transmittance) {
    transmittance = vec3(1.0);
    float tBottom = raySphere(pos, dir, ATM_BOTTOM);
    float tTop = raySphere(pos, dir, ATM_TOP);
    float tMax = 0.0;
    if (tBottom < 0.0) {
        if (tTop < 0.0) return vec3(0.0);
        tMax = tTop;
    } else {
        tMax = tTop > 0.0 ? min(tTop, tBottom) : tBottom;
    }
    tMax = min(tMax, tMaxLimit);
    float cosTheta = dot(dir, sunDir);
    float phaseR = phaseRayleigh(cosTheta);
    float phaseM = phaseMie(cosTheta, scales.mieG);
    vec3 luminance = vec3(0.0);
    float t = 0.0;
    for (int i = 0; i < steps; ++i) {
        float tNew = tMax * sq((float(i) + 0.3) / float(steps));
        float dt = tNew - t;
        t = tNew;
        vec3 p = pos + dir * t;
        float r = length(p);
        vec3 up = p / r;
        Medium m = sampleMedium(r - ATM_BOTTOM, scales);
        vec3 sampleTransmittance = exp(-m.extinction * dt);
        float sunZenithCos = dot(sunDir, up);
        vec3 sunTransmittance = transmittanceFromLut(transmittanceLut, r, sunZenithCos);
        float earthShadow = raySphere(p + up * 0.01, sunDir, ATM_BOTTOM) >= 0.0 ? 0.0 : 1.0;
        vec3 phaseScattering = m.rayleigh * phaseR + vec3(m.mie * phaseM);
        vec3 ms = useMultiScatter ? multiScatterFromLut(multiScatterLut, r, sunZenithCos) : vec3(0.0);
        vec3 S = earthShadow * sunTransmittance * phaseScattering + ms * m.scattering;
        vec3 Sint = (S - S * sampleTransmittance) / max(m.extinction, vec3(1e-7));
        luminance += transmittance * Sint;
        transmittance *= sampleTransmittance;
    }
    return luminance;
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float cloudFbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.55;
    for (int i = 0; i < 5; ++i) {
        value += amplitude * valueNoise(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p + vec2(3.1, 1.7);
        amplitude *= 0.5;
    }
    return value;
}

#endif
