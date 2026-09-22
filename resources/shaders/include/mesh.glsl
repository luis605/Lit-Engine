const uint MAX_MESHES = 2048u;
const uint LOD_LEVEL_COUNT = 7u;
const uint POINT_LOD = LOD_LEVEL_COUNT - 1u;
const float LOD_DIAMETER_PX[LOD_LEVEL_COUNT - 1u] = float[](60.0, 40.0, 25.0, 10.0, 4.0, 4.0);

struct MeshInfo {
    uint indexCount;
    uint firstIndex;
    uint baseVertex;
    float boundingRadius;
    vec4 boundingCenter;
};

struct DrawElementsIndirectCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

uint lodForDiameter(float diameterPx) {
    for (uint lod = 0u; lod < LOD_LEVEL_COUNT - 1u; ++lod) {
        if (diameterPx >= LOD_DIAMETER_PX[lod]) return lod;
    }
    return POINT_LOD;
}
