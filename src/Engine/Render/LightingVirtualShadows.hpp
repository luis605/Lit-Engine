#pragma once

namespace lighting_detail {

inline constexpr uint32_t kVsmPage = 64;
inline constexpr uint32_t kVsmPagesPerFace = 341;
inline constexpr uint32_t kVsmMips = 5;
inline constexpr uint32_t kVsmMaxFaces = 32768;
inline constexpr uint32_t kVsmRequestCapacity = 1u << 18;
inline constexpr uint32_t kVsmInstanceCapacity = 1u << 20;
inline constexpr uint32_t kVsmMovedCapacity = 4096;
inline constexpr uint32_t kVsmBuckets = 2048;
inline constexpr uint32_t kVsmTiers = 8;
inline constexpr uint32_t kVsmCounterWords = 16 + 2 * kVsmMips * kVsmTiers;
inline constexpr uint32_t kVsmHistWords = kVsmMips * kVsmTiers;
inline constexpr uint32_t kVsmArgsWords = 16;
inline constexpr uint32_t kVsmReadbackRing = 4;

struct VsmFaceGpu {
    glm::mat4 viewProjection{0.0f};
    glm::vec4 info{0.0f};
    glm::vec4 sphere{0.0f};
    glm::vec4 clipPlane{0.0f, 0.0f, 0.0f, 1.0f};
};

struct VsmConstantsGpu {
    uint32_t counts[4];
    uint32_t limits[4];
    uint32_t misc[4];
    glm::vec4 tuning;
};

struct VsmLightSlot {
    uint64_t exactKey = 0;
    uint64_t identityKey = 0;
    glm::vec3 position{0.0f};
    uint32_t faceBase = 0;
    uint32_t faceCount = 0;
    bool matched = false;
};

struct VirtualShadowGpu {
    RefCntAutoPtr<Diligent::ITexture> atlas;
    RefCntAutoPtr<Diligent::ISampler> compareSampler;
    RefCntAutoPtr<Diligent::IBuffer> noStamps;
    RefCntAutoPtr<Diligent::IBuffer> zeroSource;
    RefCntAutoPtr<Diligent::IBuffer> faces, pageTable, stamps, physical, counters, args, requests, renderList, faceDirty, previousObjects, moved, instances, sorted, buckets, commands, constants;
    RefCntAutoPtr<Diligent::IBuffer> readback[kVsmReadbackRing];
    uint32_t readbackFrame[kVsmReadbackRing]{};
    RefCntAutoPtr<Diligent::IPipelineState> motionPso, faceDirtyPso, clearDirtyPso, releasePso, histPso, planPso, allocatePso, finalizePso, requestPso, cullPso, prefixPso, scatterPso, casterPso, clearPso;
    struct Bindings {
        std::vector<const void*> key;
        RefCntAutoPtr<Diligent::IShaderResourceBinding> motion, faceDirty, clearDirty, release, hist, plan, allocate, finalize, request, cull, prefix, scatter, caster, clear;
    };
    Bindings bindings[2];
    int nextBinding = 0;
    int poolSize = 0;
    uint32_t poolSide = 0;
    uint32_t objectCapacity = 0;
    uint32_t stampCapacity = 0;
    float effectiveScale = 1.0f;
    uint32_t lastObjectCount = 0;
    uint32_t faceSlots = 0;
    uint32_t assignedFrame = 0;
    uint32_t lastRunFrame = 0;
    uint32_t runsThisFrame = 0;
    bool resetPending = true;
    bool allDirty = true;
    std::vector<VsmFaceGpu> cpuFaces;
    std::vector<VsmLightSlot> slots;
    std::vector<uint32_t> freeSingle, freeCube;
    uint32_t highWater = 0;
    uint32_t stats[kVsmCounterWords]{};
    uint32_t statsFrame = 0;
};

}
