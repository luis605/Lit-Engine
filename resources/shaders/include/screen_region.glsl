#ifndef LIT_SCREEN_REGION_GLSL
#define LIT_SCREEN_REGION_GLSL

layout(std140) uniform ScreenRegion {
    ivec2 regionLow;
    ivec2 regionHigh;
    ivec2 regionFullSize;
};

void screenRegionAtSize(ivec2 levelSize, out ivec2 first, out ivec2 last) {
    int mip = 0;
    while (any(greaterThan(max(regionFullSize >> mip, ivec2(1)), levelSize))) ++mip;
    first = min(regionLow >> mip, levelSize - 1);
    ivec2 rounded = min((regionHigh + ((1 << mip) - 1)) >> mip, levelSize);
    last = mix(rounded, levelSize, greaterThanEqual(regionHigh, regionFullSize));
}

#endif
