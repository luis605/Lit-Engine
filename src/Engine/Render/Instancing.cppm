module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Engine.Instancing;

import Engine.glm;

export inline constexpr uint32_t kInstanceCellSize = 64;
export inline constexpr uint32_t kInstanceEntryBit = 1u << 28;
export inline constexpr uint32_t kMaxPoolInstances = 1u << 22;
export inline constexpr uint32_t kMaxPoolCells = kMaxPoolInstances / kInstanceCellSize;
export inline constexpr uint32_t kInstanceLodLevels = 7;
export inline constexpr uint32_t kNoInstanceBatch = 0xFFFFFFFFu;

export namespace InstanceFlags {
inline constexpr uint32_t Dead = 1u;
inline constexpr uint32_t NoShadow = 2u;
}

export struct InstanceData {
    glm::vec3 position{0.0f};
    float scale = 1.0f;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t color = 0xFFFFFFFFu;
    uint32_t seed = 0;
    uint32_t flags = 0;
    bool operator==(const InstanceData&) const = default;
};

export struct InstanceBatchSettings {
    float drawDistance = 0.0f;
    float fadeStart = 0.0f;
    float lodBias = 1.0f;
    bool castShadows = true;
    bool operator==(const InstanceBatchSettings&) const = default;
};

export struct InstanceBatch {
    uint32_t mesh = 0xFFFFFFFFu;
    uint32_t material = 0;
    uint32_t shader = 0;
    InstanceBatchSettings settings;
    std::vector<InstanceData> instances;
    std::vector<uint64_t> keys;
    uint64_t version = 1;
    uint64_t layoutVersion = 1;
    uint32_t dirtyFirst = 0xFFFFFFFFu;
    uint32_t dirtyEnd = 0;

    void assign(std::span<const InstanceData> data);
    void update(uint32_t first, std::span<const InstanceData> data);
    bool remove(uint32_t index);
    bool revive(uint32_t index);
    std::optional<uint64_t> swapRemove(uint32_t index);
    [[nodiscard]] std::optional<uint32_t> indexOfKey(uint64_t key) const;
    [[nodiscard]] uint32_t aliveCount() const;
    void setSettings(const InstanceBatchSettings& value);
    void markDirty(uint32_t first, uint32_t end);
    void markLayout();
    void clearDirty() {
        dirtyFirst = 0xFFFFFFFFu;
        dirtyEnd = 0;
    }
    [[nodiscard]] bool dirty() const { return dirtyEnd > dirtyFirst; }
};

export struct InstanceGpu {
    glm::vec4 positionScale{0.0f};
    uint32_t packed[4] = {0, 0, 0, 0};
};
static_assert(sizeof(InstanceGpu) == 32);

export struct InstanceCellGpu {
    glm::vec4 sphere{0.0f};
    uint32_t info[4] = {0, 0, 0, 0};
};
static_assert(sizeof(InstanceCellGpu) == 32);

export struct InstanceBatchGpu {
    uint32_t info[4] = {0, 0, 0, 0};
    glm::vec4 meshSphere{0.0f};
    glm::vec4 lod{0.0f};
    uint32_t cells[4] = {0, 0, 0, 0};
};
static_assert(sizeof(InstanceBatchGpu) == 64);

export namespace InstanceBatchGpuFlags {
inline constexpr uint32_t Active = 1u;
inline constexpr uint32_t Visible = 2u;
inline constexpr uint32_t Shadows = 4u;
}

export [[nodiscard]] InstanceGpu packInstance(const InstanceData& instance);
export [[nodiscard]] InstanceData unpackInstance(const InstanceGpu& gpu);
export [[nodiscard]] float instanceHash01(uint32_t seed);
export [[nodiscard]] glm::vec4 instanceColorFloat(uint32_t rgba8);
export [[nodiscard]] uint32_t packInstanceColor(const glm::vec4& color);

export [[nodiscard]] std::vector<uint32_t> instanceSpatialOrder(std::span<const InstanceData> instances);
export [[nodiscard]] InstanceCellGpu buildInstanceCell(std::span<const InstanceData> instances, std::span<const uint32_t> slots, const glm::vec4& meshSphere, uint32_t batch);

export class InstanceAllocator {
  public:
    explicit InstanceAllocator(uint32_t limitCells = kMaxPoolCells) : m_limit(limitCells) {}
    [[nodiscard]] std::optional<uint32_t> allocate(uint32_t cells);
    void free(uint32_t first, uint32_t cells);
    void clear();
    [[nodiscard]] uint32_t highWater() const { return m_highWater; }
    [[nodiscard]] uint32_t usedCells() const { return m_used; }
    [[nodiscard]] uint32_t freeRanges() const { return static_cast<uint32_t>(m_free.size()); }
    [[nodiscard]] uint32_t limit() const { return m_limit; }

  private:
    std::map<uint32_t, uint32_t> m_free;
    uint32_t m_highWater = 0;
    uint32_t m_used = 0;
    uint32_t m_limit = kMaxPoolCells;
};

export struct InstanceCullView {
    std::array<glm::vec4, 6> planes{};
    glm::mat4 view{1.0f};
    glm::vec3 position{0.0f};
    float pixelScale = 1.0f;
    float lodBias = 1.0f;
    float distanceScale = 1.0f;
    int forcedLod = -1;
    bool cellCull = true;
};

export struct InstanceCullResult {
    uint32_t cellsTested = 0;
    uint32_t cellsAccepted = 0;
    uint32_t instancesTested = 0;
    uint32_t visible = 0;
    std::array<uint32_t, kInstanceLodLevels> perLod{};
};

export [[nodiscard]] InstanceCullResult cullInstancesReference(const InstanceBatch& batch, const glm::mat4& owner, const glm::vec4& meshSphere, const InstanceCullView& view);
export [[nodiscard]] uint32_t instanceLodForDiameter(float diameterPx);
export [[nodiscard]] std::array<glm::vec4, 6> instanceFrustumPlanes(const glm::mat4& viewProjection);

export struct InstanceViewStats {
    uint32_t cellsTested = 0;
    uint32_t cellsAccepted = 0;
    uint32_t instancesTested = 0;
    uint32_t visible = 0;
    uint32_t occluded = 0;
    uint32_t overflow = 0;
};

export struct InstanceRenderStats {
    bool enabled = false;
    uint32_t batches = 0;
    uint32_t instances = 0;
    uint32_t poolCells = 0;
    uint32_t poolCapacity = 0;
    uint64_t uploadedBytes = 0;
    uint64_t frame = 0;
    InstanceViewStats early;
    InstanceViewStats retest;
    InstanceViewStats shadow;
    std::array<uint32_t, kInstanceLodLevels> perLod{};
};

export [[nodiscard]] std::string instanceBatchText(const InstanceBatch& batch);
export [[nodiscard]] std::optional<InstanceBatch> instanceBatchFromText(const std::string& text);
