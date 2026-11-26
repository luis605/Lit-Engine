#ifndef LIT_VSM_PASS_GLSL
#define LIT_VSM_PASS_GLSL

#include "vsm.glsl"

layout(std140) uniform VsmConstants {
    uvec4 vsmCounts;
    uvec4 vsmLimits;
    uvec4 vsmMisc;
    vec4 vsmTuning;
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

const uint VSM_ARGS_CLEAR = 0u;
const uint VSM_ARGS_CULL = 4u;
const uint VSM_ARGS_SCATTER = 8u;
const uint VSM_ARGS_DRAW_COUNT = 12u;

const uint VSM_BUCKETS = 2048u;

const uint VSM_TIERS = 8u;
const uint VSM_HIST_BASE = 16u;
const uint VSM_GRANT_BASE = VSM_HIST_BASE + 5u * VSM_TIERS;


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
