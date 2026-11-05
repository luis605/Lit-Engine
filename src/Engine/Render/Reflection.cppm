module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module Engine.Render.reflection;

import Engine.glm;
import Engine.Render.settings;

export inline constexpr int kMaxReflectionProbes = 8;
export inline constexpr int kReflectionVoxelResolution = 64;
export inline constexpr std::size_t kMaxReflectionBoxes = 8192;
export inline constexpr uint32_t kReflectionModeSsr = 1u;
export inline constexpr uint32_t kReflectionModeProbes = 2u;
export inline constexpr uint32_t kReflectionModePlanar = 4u;
export inline constexpr uint32_t kReflectionModeWorld = 8u;

export inline constexpr uint32_t kProbeResolutionMin = 32u;
export inline constexpr uint32_t kProbeResolutionMax = 256u;
export inline constexpr uint32_t kProbeResolutionDefault = 128u;

export struct ReflectionProbe {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{5.0f};
    float intensity = 1.0f;
    float blendDistance = 1.0f;
    uint32_t resolution = kProbeResolutionDefault;
};

export struct ReflectionPlane {
    glm::vec3 center{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 axisU{1.0f, 0.0f, 0.0f};
    glm::vec2 halfSize{10.0f};
};

export struct ReflectionBox {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.5f};
    glm::vec3 color{0.5f};
};

export struct ReflectionScene {
    std::vector<ReflectionProbe> probes;
    std::vector<ReflectionPlane> planes;
    std::vector<ReflectionBox> boxes;
    bool operator==(const ReflectionScene&) const = default;
};

export struct ReflectionTier {
    int ssrStepCap = 40;
    int voxelStepCap = 40;
    int planarDivisor = 2;
    float roughnessScale = 1.0f;
};

export struct ReflectionPlan {
    uint32_t features = 0;
    ReflectionTier tier;
    [[nodiscard]] bool enabled() const { return features != 0u; }
    [[nodiscard]] bool has(uint32_t feature) const { return (features & feature) != 0u; }
};

export ReflectionTier reflectionTier(ReflectionQuality quality);
export ReflectionPlan reflectionPlan(const RenderSettings& settings);

export struct ReflectionVoxelGrid {
    glm::vec3 minCorner{0.0f};
    glm::vec3 size{1.0f};
    int resolution = 0;
    std::vector<uint32_t> cells;
    [[nodiscard]] bool valid() const { return resolution > 0 && !cells.empty(); }
    [[nodiscard]] uint32_t at(int x, int y, int z) const;
};

export ReflectionVoxelGrid buildReflectionVoxels(const std::vector<ReflectionBox>& boxes, int resolution);
export uint32_t packVoxelColor(const glm::vec3& color);
export glm::vec3 boxProjectDirection(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& center, const glm::vec3& halfExtents);
export float probeInfluence(const glm::vec3& position, const ReflectionProbe& probe);
export int strongestProbe(const std::vector<ReflectionProbe>& probes, const glm::vec3& position);
export int selectPlanarPlane(const std::vector<ReflectionPlane>& planes, const glm::vec3& cameraPosition);
export uint32_t clampProbeResolution(uint32_t resolution);
export uint32_t sceneProbeResolution(const std::vector<ReflectionProbe>& probes);

export struct ProbeBakeState {
    bool requested = false;
    int bakedCount = 0;
    uint32_t bakedResolution = 0;
};

export bool probeBakeNeeded(const ProbeBakeState& state, int probeCount, uint32_t resolution);
export uint64_t hashReflectionBoxes(const std::vector<ReflectionBox>& boxes);
