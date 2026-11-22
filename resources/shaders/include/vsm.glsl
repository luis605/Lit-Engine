#ifndef LIT_VSM_GLSL
#define LIT_VSM_GLSL

// Virtual (paged) shadow maps for local lights. See docs/Lighting.md "Virtual shadow maps".
// Each spot light owns one face, each point light six cube faces. A face is a perspective shadow
// map with a mip chain of virtual resolutions VSM_PAGE << mip (64 .. 1024 texels); every mip is cut
// into VSM_PAGE x VSM_PAGE pages. Pages that pixels of the current frame need are allocated from a
// physical pool (one depth atlas), rendered, and kept while the light and the casters around it
// stay put. The page table holds, per (face, mip, page), the physical page plus ALLOCATED / VALID bits.

#include "scene.glsl"

const uint VSM_PAGE = 64u;
const uint VSM_MIPS = 5u;
const uint VSM_PAGES_PER_FACE = 341u; // 1 + 4 + 16 + 64 + 256
const uint VSM_ALLOCATED = 0x80000000u;
const uint VSM_VALID = 0x40000000u;
const uint VSM_PHYSICAL_MASK = 0xFFFFu;
const uint VSM_INVALID = 0xFFFFFFFFu;

struct VsmFace {
    mat4 viewProjection;
    vec4 info;   // x tan(half fov) incl. border, y light index, z CPU dirty flag, w 1 for a used slot
    vec4 sphere; // light position and range
};

uint vsmMipOffset(uint mip) { return ((1u << (2u * mip)) - 1u) / 3u; }

uint vsmMipOf(uint local) {
    uint mip = 0u;
    for (uint m = 1u; m < VSM_MIPS; ++m) {
        if (local >= vsmMipOffset(m)) mip = m;
    }
    return mip;
}

// World size of one screen pixel at the given view depth (same in request and lookup).
float virtualShadowFootprint(float viewDepth) {
    bool orthographic = projection[3][3] > 0.5;
    return (orthographic ? 2.0 : 2.0 * viewDepth) / max(projection[1][1] * screenParams.y, 1e-6);
}

// Finest mip whose texel is no larger than one screen pixel (scaled by vsmParams.w).
uint vsmMipFor(float lightDepth, float tanHalf, float footprint) {
    float texelsNeeded = 2.0 * lightDepth * tanHalf / max(footprint * vsmParams.w, 1e-6);
    float mip = ceil(log2(max(texelsNeeded / float(VSM_PAGE), 1.0)));
    return uint(clamp(mip, 0.0, min(float(VSM_MIPS - 1u), vsmParams2.x)));
}

uint vsmCubeFace(vec3 d) {
    vec3 a = abs(d);
    return a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0u : 1u) : a.y >= a.z ? (d.y > 0.0 ? 2u : 3u) : (d.z > 0.0 ? 4u : 5u);
}

uint vsmPageIndex(uint face, uint mip, vec2 uv) {
    uint n = 1u << mip;
    uvec2 page = min(uvec2(clamp(uv, vec2(0.0), vec2(1.0)) * float(n)), uvec2(n - 1u));
    return face * VSM_PAGES_PER_FACE + vsmMipOffset(mip) + page.y * n + page.x;
}

#ifdef VSM_LOOKUP
layout(std430) readonly buffer VsmFaceBuffer {
    VsmFace vsmFaces[];
};

layout(std430) readonly buffer VsmPageTable {
    uint vsmPageTable[];
};

uniform sampler2DShadow u_vsmAtlas;

float g_pixelFootprint = 1.0;

// Resident page lookup at `mip`, falling back to coarser mips when a page is not resident (pool
// overflow, a view that did not request it, or a transparent surface outside the depth buffer).
// Returns the page table entry (0 if nothing is resident) and the mip it was found at.
uint vsmResolve(uint face, vec2 uv, inout uint mip) {
    uv = clamp(uv, vec2(0.0), vec2(0.99999));
    for (int m = int(mip); m >= 0; --m) {
        uint n = 1u << uint(m);
        uvec2 page = uvec2(uv * float(n));
        uint entry = vsmPageTable[face * VSM_PAGES_PER_FACE + vsmMipOffset(uint(m)) + page.y * n + page.x];
        if ((entry & VSM_VALID) != 0u) {
            mip = uint(m);
            return entry;
        }
    }
    return 0u;
}

float vsmSampleEntry(uint entry, vec2 uv, uint mip, float depth) {
    uint side = uint(vsmParams.z);
    uint physical = entry & VSM_PHYSICAL_MASK;
    // Keep the bilinear compare footprint inside the page: neighbouring atlas pages belong to other faces.
    vec2 local = clamp(fract(clamp(uv, vec2(0.0), vec2(0.99999)) * float(1u << mip)) * float(VSM_PAGE), vec2(0.5), vec2(float(VSM_PAGE) - 0.5));
    vec2 atlas = (vec2(physical % side, physical / side) * float(VSM_PAGE) + local) / vsmParams.y;
    return texture(u_vsmAtlas, vec3(atlas, depth));
}

float virtualShadow(uint faceBase, bool point, vec3 lightPos, vec3 worldPos, vec3 N, vec3 L, float NoL, float noise, float footprint) {
    if (vsmParams.x < 0.5) return 1.0;
    uint face = faceBase + (point ? vsmCubeFace(worldPos - lightPos) : 0u);
    VsmFace f = vsmFaces[face];
    vec4 centerClip = f.viewProjection * vec4(worldPos, 1.0);
    if (centerClip.w <= 0.0) return 1.0;
    uint mip = vsmMipFor(centerClip.w, f.info.x, footprint);
    vec2 centerUv = vec2(centerClip.x / centerClip.w * 0.5 + 0.5, 0.5 - centerClip.y / centerClip.w * 0.5);
    uint entry = vsmResolve(face, centerUv, mip);
    if (entry == 0u) return 1.0;
    float resolution = float(VSM_PAGE << mip);
    float texelWorld = 2.0 * centerClip.w * f.info.x / resolution;
    float slope = min(sqrt(max(1.0 - NoL * NoL, 0.0)) / max(NoL, 0.05), 4.0);
    vec3 offsetPos = worldPos + N * texelWorld * localShadowParams.w * (0.6 + 0.8 * slope) + L * texelWorld * localShadowParams2.x * (1.0 + slope);
    vec4 clip = f.viewProjection * vec4(offsetPos, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.z >= 1.0) return 1.0;
    vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    float n = float(1u << mip);
    uvec2 centerPage = uvec2(clamp(centerUv, vec2(0.0), vec2(0.99999)) * n);
    float radius = (1.0 + localShadowParams.z) / resolution;
    float phi = hash12(floor(uv * resolution) + float(face) * 17.0) * TWO_PI;
    // Four rotated taps, each a bilinear 2x2 compare. Taps in the centre page reuse its entry; taps
    // that cross into a neighbouring page look that page up at the same mip.
    const int TAPS = 4;
    float sum = 0.0;
    for (int i = 0; i < TAPS; ++i) {
        vec2 tapUv = clamp(uv + vogelDisk(i, TAPS, phi) * radius, vec2(0.0), vec2(0.99999));
        uvec2 page = uvec2(tapUv * n);
        uint tapEntry = entry;
        if (page != centerPage) {
            uint other = vsmPageTable[face * VSM_PAGES_PER_FACE + vsmMipOffset(mip) + page.y * uint(n) + page.x];
            if ((other & VSM_VALID) != 0u) tapEntry = other;
            else tapUv = clamp(tapUv, (vec2(centerPage) + 0.001) / n, (vec2(centerPage) + 0.999) / n);
        }
        sum += vsmSampleEntry(tapEntry, tapUv, mip, ndc.z);
    }
    return sum / float(TAPS);
}
#endif

#endif
