#pragma once

struct DrawElementsIndirectCommand {
    unsigned int count;
    unsigned int instanceCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    unsigned int baseInstance;
};

struct MeshInfo {
    unsigned int indexCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    float boundingRadius;
    alignas(16) glm::vec4 boundingCenter;
};

struct CullingUniforms {
    uint32_t objectCount;
    uint32_t maxDraws;
    float smallObjectThreshold;
    float lodBias;
    int32_t forcedLod;
    uint32_t opaqueShaderCount;
    uint32_t bucketSlots;
};

struct CommandGenUniforms {
    uint32_t maxBucketsPerShader;
    uint32_t totalBuckets;
    uint32_t bucketSlots;
    uint32_t viewBuckets;
};

struct PrefixSumConstants {
    uint32_t bucketCount;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
};

struct DispatchArgsConstants {
    uint32_t maxCount;
    uint32_t workgroupSize;
    uint32_t padding0;
    uint32_t padding1;
};

struct ScatterConstants {
    uint32_t maxDraws;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
};

struct ApplyDirtyConstants {
    uint32_t dirtyCount;
    uint32_t epoch;
    uint32_t padding0;
    uint32_t padding1;
};

struct TransformUniforms {
    unsigned int objectCount;
    unsigned int currentHierarchyLevel;
    unsigned int sortedListOffset;
    unsigned int frameOffset;
    unsigned int epoch;
    unsigned int forceFullRecompute;
    float animTime;
    unsigned int animMovingCount;
    unsigned int animEntityOffset;
    unsigned int directEntityRange;
};

struct DebugLineVertex {
    glm::vec3 position;
    glm::vec4 color;
};

struct PortalSurfaceUniforms {
    glm::mat4 viewProjection;
    glm::mat4 invView;
    glm::vec4 center;
    glm::vec4 axisU;
    glm::vec4 axisV;
    glm::vec4 normal;
    glm::vec4 camera;
    glm::vec4 params;
    glm::vec4 color;
};

struct PortalDepthUniforms {
    glm::mat4 projection;
    glm::mat4 childInvProjection;
    glm::mat4 viewProjection;
    glm::mat4 invView;
    glm::vec4 center;
    glm::vec4 axisU;
    glm::vec4 axisV;
    glm::vec4 normal;
    glm::vec4 camera;
    glm::vec4 params;
};

enum VisibilityPhaseIndex : int { kPhaseOpaque = 0, kPhaseRetest = 1, kPhaseTransparent = 2, kPhaseShadow = 3, kPhaseCount = 4 };

inline constexpr uint32_t kMaxMeshes = 2048;
inline constexpr uint32_t kInvalidMeshId = 0xFFFFFFFFu;
inline constexpr uint32_t kNormalSamples = 64;
inline constexpr Diligent::TEXTURE_FORMAT kSceneDepthFormat = Diligent::TEX_FORMAT_D32_FLOAT;
inline constexpr Diligent::TEXTURE_FORMAT kAccumulationFormat = Diligent::TEX_FORMAT_RGBA16_FLOAT;
inline constexpr Diligent::TEXTURE_FORMAT kRevealageFormat = Diligent::TEX_FORMAT_R16_FLOAT;
inline constexpr uint32_t kMaxDirtyPerFrame = 65536;
inline constexpr uint32_t kMaxTransformWork = 65536;
inline constexpr uint32_t kShadowBucketCapacity = 65536;
inline constexpr uint32_t kWorkgroupSize = 256;
inline constexpr uint32_t kVisibilityBitWordsPerGroup = 8;
inline constexpr int kCascadeShadowViews = 4;
inline constexpr int kShadowViews = kCascadeShadowViews + kMaxLocalShadowViews;
inline constexpr int kMaxHiZMips = 32;
inline constexpr int kMaxPortalViews = 24;
inline constexpr size_t kMaxSurfaceShaders = 16;
inline constexpr size_t kMaxDebugVertices = 1u << 17;
inline constexpr size_t kInitialMaxObjects = 500000;
inline constexpr float kPi = 3.14159265f;
inline constexpr float kPortalAxisEpsilon = 1.0e-8f;

inline constexpr size_t kGpuPassCount = static_cast<size_t>(GpuPass::Count);
inline constexpr std::array<const char*, kGpuPassCount> kGpuPassNames = {"Upload", "Transform", "Sky+IBL", "Shadows", "Cull", "CmdGen", "GBuffer", "Hi-Z", "Retest Cull", "Retest CmdGen", "Retest GBuffer", "Trans Cull", "Trans CmdGen", "GTAO", "SSR", "Lighting", "Trans Draw", "Post", "UI"};

inline std::vector<MeshInfo> gMeshInfos;
inline std::vector<glm::vec4> gNormalSamples;
inline size_t gTotalVertexBytes = 0;
inline size_t gTotalIndexBytes = 0;

inline uint32_t BucketSlotCount() { return static_cast<uint32_t>(std::clamp<size_t>(gMeshInfos.size(), 1, kMaxMeshes)); }

inline uint32_t ActiveBucketCount(size_t shaders) { return static_cast<uint32_t>(shaders) * BucketSlotCount(); }

inline glm::vec4 PackPortalAxis(const glm::vec3& axis) { return glm::vec4(axis, 1.0f / std::max(glm::dot(axis, axis), kPortalAxisEpsilon)); }

inline CullingUniforms MakeCullingUniforms(uint32_t objectCount, size_t maxObjects, float smallObjectThreshold, float lodBias, int forcedLod, size_t shaderCount) {
    return CullingUniforms{objectCount, static_cast<uint32_t>(maxObjects), smallObjectThreshold, lodBias, forcedLod, static_cast<uint32_t>(shaderCount), BucketSlotCount()};
}
