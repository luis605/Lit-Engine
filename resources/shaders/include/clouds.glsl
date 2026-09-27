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

float cloudFbm(vec3 p) {
    float f = 0.0;
    f += 0.5200 * noise3D(p); p = p * 2.02 + vec3(0.15, 0.32, 0.27);
    f += 0.2800 * noise3D(p); p = p * 2.03 + vec3(0.24, 0.18, 0.41);
    f += 0.1400 * noise3D(p); p = p * 2.01 + vec3(0.31, 0.42, 0.13);
    f += 0.0600 * noise3D(p);
    return f;
}

float billowFbm(vec3 p) {
    float f = 0.0;
    f += 0.5500 * abs(noise3D(p) * 2.0 - 1.0); p = p * 2.02 + vec3(0.21, 0.14, 0.35);
    f += 0.3000 * abs(noise3D(p) * 2.0 - 1.0); p = p * 2.03 + vec3(0.12, 0.43, 0.19);
    f += 0.1500 * abs(noise3D(p) * 2.0 - 1.0);
    return f;
}

float hgPhase(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265359 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001), 1.5));
}

float dualHenyeyGreenstein(float cosTheta) {
    return mix(hgPhase(cosTheta, 0.76), hgPhase(cosTheta, -0.20), 0.25);
}

float beerPowder(float opticalDepth) {
    float beer = exp(-opticalDepth);
    float powder = 1.0 - exp(-opticalDepth * 2.0);
    return mix(beer, 2.0 * beer * powder, 0.65);
}

bool intersectCloudShell(vec3 rayOrigin, vec3 rayDir, float hBase, float hTop, out float tNear, out float tFar) {
    float vy = max(rayDir.y, 0.003);
    float yCam = max(rayOrigin.y, 0.0);
    float rc = 6360000.0 + yCam;

    float dhNear = max(hBase - yCam, 0.0);
    float dhFar = max(hTop - yCam, 10.0);

    tNear = (2.0 * dhNear) / (vy + sqrt(vy * vy + (2.0 * dhNear) / rc));
    tFar = (2.0 * dhFar) / (vy + sqrt(vy * vy + (2.0 * dhFar) / rc));

    if (yCam >= hBase && yCam <= hTop) tNear = 0.0;

    tFar = min(tFar, tNear + 24000.0);
    return tFar > tNear && rayDir.y > -0.01;
}

float sampleCloudDensity(vec3 p, float h, float coverage, float densityMultiplier, vec3 wind) {
    if (h <= 0.0 || h >= 1.0) return 0.0;
    float heightProfile = smoothstep(0.0, 0.16, h) * smoothstep(1.0, 0.45, h);
    vec3 coord = vec3(p.x + wind.x, p.y * 1.8, p.z + wind.z) * 0.00020;
    float baseNoise = cloudFbm(coord);
    vec3 detailCoord = vec3(p.x + wind.x * 1.4, p.y * 2.2, p.z + wind.z * 1.4) * 0.00065;
    float detail = billowFbm(detailCoord);
    float shape = baseNoise - detail * 0.25 * (1.0 - h * 0.5);
    float threshold = clamp(1.0 - coverage, 0.0, 0.98);
    float d = smoothstep(threshold, threshold + 0.30, shape) * heightProfile;
    return d * densityMultiplier * 0.003;
}

vec4 renderClouds(vec3 camPos, vec3 rayDir, vec3 sunDir, vec3 sunLight, vec3 ambientZenith, vec3 ambientGround, float coverage, float densityMultiplier, float cloudSpeed, float altitude, float timeVal, float dither) {
    if (coverage <= 0.001 || densityMultiplier <= 0.001 || rayDir.y <= -0.01) return vec4(0.0, 0.0, 0.0, 1.0);

    float hBase = max(altitude, 400.0);
    float thickness = 1400.0;
    float hTop = hBase + thickness;
    float tNear = 0.0;
    float tFar = 0.0;

    if (!intersectCloudShell(camPos, rayDir, hBase, hTop, tNear, tFar)) return vec4(0.0, 0.0, 0.0, 1.0);

    int steps = 7;
    float stepSize = (tFar - tNear) / float(steps);
    float t = tNear + stepSize * dither;
    vec3 wind = vec3(timeVal * cloudSpeed, 0.0, timeVal * cloudSpeed * 0.35);

    float cosTheta = dot(rayDir, sunDir);
    float phase = clamp(dualHenyeyGreenstein(cosTheta), 0.05, 1.8);

    vec3 accumColor = vec3(0.0);
    float accumTransmittance = 1.0;
    float yCam = max(camPos.y, 0.0);
    float invLayerThickness = 1.0 / thickness;

    vec3 sunRadiance = (sunLight / 3.14159265359) * 0.70;

    for (int i = 0; i < steps; ++i) {
        vec3 p = camPos + rayDir * t;
        float h = clamp((yCam + rayDir.y * t - (t * t) * (1.0 / 12720000.0) - hBase) * invLayerThickness, 0.0, 1.0);
        float density = sampleCloudDensity(p, h, coverage, densityMultiplier, wind);

        if (density > 0.00005) {
            float distToTop = max((1.0 - h) * thickness / max(sunDir.y, 0.15), 0.0);
            float sunOpticalDepth = distToTop * density * 6.0;
            float sunTransmittance = beerPowder(sunOpticalDepth);

            vec3 sunScatter = sunRadiance * (sunTransmittance * phase);
            vec3 ambient = mix(ambientGround, ambientZenith, h * 0.65 + 0.35) * (0.35 + 0.65 * h);

            float stepExtinction = density * stepSize * 1.1;
            float stepTransmittance = exp(-stepExtinction);
            vec3 stepLuminance = (sunScatter + ambient) * (1.0 - stepTransmittance);

            accumColor += accumTransmittance * stepLuminance;
            accumTransmittance *= stepTransmittance;

            if (accumTransmittance < 0.01) break;
        }

        t += stepSize;
    }

    float midDist = tNear + (tFar - tNear) * 0.5;
    float aerialHaze = 1.0 - exp(-midDist * 0.000035);
    accumColor = mix(accumColor, ambientZenith * 0.85, aerialHaze * 0.65);

    return vec4(accumColor, accumTransmittance);
}