#ifndef LIT_VSM_PASS_GLSL
#define LIT_VSM_PASS_GLSL

// Shared declarations of the virtual shadow map passes (vsm_*.comp, vsm_*.vert).
#include "vsm.glsl"

layout(std140) uniform VsmConstants {
    uvec4 vsmCounts; // x face slots in use, y frame stamp, z object count, w physical pages in the pool
    uvec4 vsmLimits; // x request capacity, y instance capacity, z moved-object capacity, w 1 = invalidate every face
    uvec4 vsmMisc;   // x opaque surface shader count, y render-list capacity, z mip of the ALLOCATE pass
    vec4 vsmTuning;  // x caster LOD bias
};

const uint VSM_COUNTER_FREE = 0u;
const uint VSM_COUNTER_REQUESTS = 1u;
const uint VSM_COUNTER_POPPED = 2u;
const uint VSM_COUNTER_RENDER = 3u;
const uint VSM_COUNTER_OVERFLOW = 4u;
const uint VSM_COUNTER_INSTANCES = 5u;
const uint VSM_COUNTER_MOVED = 6u;
const uint VSM_COUNTER_MOVE_OVERFLOW = 7u;
const uint VSM_COUNTER_RESIDENT = 8u;
const uint VSM_COUNTER_RENDERED_FRAME = 9u;

// VsmArgs layout (uints): [0..4) clear draw, [4..7) cull dispatch, [8..11) scatter dispatch, [12] caster draw count.
const uint VSM_ARGS_CLEAR = 0u;
const uint VSM_ARGS_CULL = 4u;
const uint VSM_ARGS_SCATTER = 8u;
const uint VSM_ARGS_DRAW_COUNT = 12u;

const uint VSM_BUCKETS = 2048u;

// Overflow priority: every face carries a tier (0 = most important, info.w = 1 + tier). Requests that still need a
// physical page are counted per (mip, tier) group; groups are granted whole, coarse mips first and by tier within a
// mip, until the free pages run out, so the outcome of an overflow does not depend on thread order.
const uint VSM_TIERS = 8u;
const uint VSM_HIST_BASE = 16u;                                // counter words: histogram of unallocated requests per group
const uint VSM_GRANT_BASE = VSM_HIST_BASE + 5u * VSM_TIERS;    // counter words: 1 when the group is granted


struct VsmPage {
    uint face;
    uint mip;
    uvec2 page;
};

VsmPage vsmDecodePage(uint index) {
    VsmPage result;
    result.face = index / VSM_PAGES_PER_FACE;
    uint local = index % VSM_PAGES_PER_FACE;
    result.mip = vsmMipOf(local);
    uint inMip = local - vsmMipOffset(result.mip);
    uint n = 1u << result.mip;
    result.page = uvec2(inMip % n, inMip / n);
    return result;
}

#endif
