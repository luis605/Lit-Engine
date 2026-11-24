#ifndef LIT_SURFACE_API_GLSL
#define LIT_SURFACE_API_GLSL

#include "common.glsl"

#define LIT_STANDARD 0
#define LIT_UNLIT 1
#define LIT_TOON 2

struct VertexData {
    vec3 position;
    vec3 normal;
    vec2 uv;
};

struct VertexContext {
    mat4 world;
    uint entity;
    uint material;
    float time;
    vec4 custom0;
    vec4 custom1;
    vec4 instanceColor;  // per-instance tint of an InstanceBatch instance (RGBA, 1 for ordinary objects)
    uint instanceSeed;   // per-instance 24-bit seed (ordinary objects: a hash of the entity id)
};

struct SurfaceInput {
    vec3 worldPosition;    // local (render-space) position, camera relative math
    vec3 patternPosition;  // worldPosition + origin phase: sample procedural world-space patterns with this (stable across floating-origin rebases)
    vec3 objectPosition;
    vec3 viewDirection;
    vec3 geometricNormal;
    vec2 uv;
    vec2 screenUv;
    uint entity;
    uint material;
    float time;
    vec4 custom0;
    vec4 custom1;
    bool frontFacing;
    vec4 instanceColor;  // as VertexContext
    uint instanceSeed;
};

// Wind sway for foliage (vertex()): bends the mesh-local position along `direction` (xz), growing with height above the
// base (quadratic, `height` = mesh height at which the offset reaches `amplitude`), with a per-instance phase.
vec3 litWindSway(vec3 position, float time, uint seed, vec2 direction, float amplitude, float height, float frequency) {
    float h = max(position.y, 0.0) / max(height, 1e-3);
    float phase = float(seed & 0xFFFFu) * (6.2831853 / 65536.0);
    float gust = sin(time * frequency + phase) * 0.7 + sin(time * frequency * 2.3 + phase * 1.7) * 0.3;
    vec2 offset = direction * (amplitude * h * h * (0.6 + 0.4 * gust));
    return position + vec3(offset.x, -0.15 * amplitude * h * h * abs(gust), offset.y);
}

struct Surface {
    vec3 baseColor;
    float opacity;
    vec3 normal;
    float metallic;
    float roughness;
    float occlusion;
    vec3 emissive;
    float tangentAngle;
    int lightingModel;
    float toonBands;
    float toonRim;
    float toonRimWidth;
};

uvec2 litEncodeToon(float bands, float rim, float rimWidth) {
    uint b = uint(clamp(bands, 1.0, 255.0) + 0.5);
    uint r = uint(saturate(rim) * 15.0 + 0.5);
    uint w = uint(saturate(rimWidth) * 15.0 + 0.5);
    return uvec2(b, (r << 4u) | w);
}

float litHash(vec3 p) { return hash33(p).x; }

float litNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash33(i).x;
    float n100 = hash33(i + vec3(1, 0, 0)).x;
    float n010 = hash33(i + vec3(0, 1, 0)).x;
    float n110 = hash33(i + vec3(1, 1, 0)).x;
    float n001 = hash33(i + vec3(0, 0, 1)).x;
    float n101 = hash33(i + vec3(1, 0, 1)).x;
    float n011 = hash33(i + vec3(0, 1, 1)).x;
    float n111 = hash33(i + vec3(1, 1, 1)).x;
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y), mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float litFbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; ++i) {
        value += amplitude * litNoise(p);
        p = p * 2.03 + vec3(1.7, 9.2, 3.1);
        amplitude *= 0.5;
    }
    return value;
}

vec2 litVoronoi(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    float nearest = 8.0;
    float id = 0.0;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec3 offset = vec3(x, y, z);
                vec3 h = hash33(cell + offset);
                vec3 d = offset + h - f;
                float dist = dot(d, d);
                if (dist < nearest) {
                    nearest = dist;
                    id = h.x;
                }
            }
        }
    }
    return vec2(sqrt(nearest), id);
}

vec3 litHsvToRgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

#endif
