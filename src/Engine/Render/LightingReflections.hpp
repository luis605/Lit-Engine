#pragma once

namespace lighting_detail {

inline constexpr uint32_t kProbeMips = 6;
inline constexpr uint32_t kProbeCubeFaces = 6;
inline constexpr uint32_t kProbeSlices = kMaxReflectionProbes * kProbeCubeFaces;
inline constexpr uint32_t kSkyVolumeGroupSize = 4;

struct ReflectionConstants {
    glm::vec4 mode;
    glm::vec4 counts;
    glm::vec4 planeCenter;
    glm::vec4 planeNormal;
    glm::vec4 planeAxisU;
    glm::vec4 voxelMin;
    glm::vec4 voxelSize;
    glm::vec4 probeCenter[kMaxReflectionProbes];
    glm::vec4 probeExtent[kMaxReflectionProbes];
};

struct ReflectionGpu {
    ReflectionScene scene;
    glm::vec3 cameraPosition{0.0f};
    int planeIndex = -1;
    ReflectionVoxelGrid grid;
    bool gridDirty = false;
    bool skyVolumeDirty = true;
    uint64_t gridKey = 0;
    ProbeBakeState bake;
    uint32_t probeResolution = kProbeResolutionDefault;
    uint64_t bindingVersion = 0;
    Diligent::ITexture* boundDepth = nullptr;
    int planarWidth = 0;
    int planarHeight = 0;
    RefCntAutoPtr<Diligent::IBuffer> constants;
    RefCntAutoPtr<Diligent::ITexture> voxels, skyVolume, probeCubes, planarDepth, planarColor;
    RefCntAutoPtr<Diligent::ITextureView> probeMips[kProbeMips];
    RefCntAutoPtr<Diligent::IPipelineState> planarClearPso, planarDepthPso, planarColorPso, probeBakePso, skyVolumePso, compositePso;
    RefCntAutoPtr<Diligent::IShaderResourceBinding> planarClearSrb, planarDepthSrb, planarColorSrb, probeBakeSrb, skyVolumeSrb, compositeSrb[2];
    std::vector<RefCntAutoPtr<Diligent::IShaderResourceBinding>> probeDownSrbs;
};

}
