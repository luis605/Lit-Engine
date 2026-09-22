#ifdef TRANSPARENT
layout(std430) readonly buffer AlphaBuffer {
    uint packedAlphas[];
};

layout(location = 6) flat out float out_alpha;

float entityAlpha(uint objectId) {
    return float((packedAlphas[objectId >> 2u] >> ((objectId & 3u) * 8u)) & 0xFFu) * (1.0 / 255.0);
}
#endif
