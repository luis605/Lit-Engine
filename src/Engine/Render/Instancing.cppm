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

// GPU instancing (docs/Instancing.md). CPU side, no GPU dependency: the InstanceBatch component (one mesh, one material,
// one surface shader and a list of compact per-instance records), the GPU record layouts the renderer uploads into its
// persistent instance pool, the pool's cell sub-allocator, the spatial cell build (64 instances per cell, the unit of
// hierarchical culling and of partial uploads) and a CPU reference of the per-instance cull (frustum, distance fade, LOD).
export module Engine.Instancing;

import Engine.glm;

// Instances per cell: a cell is one cull workgroup and owns two words of the per-view visibility bits.
export inline constexpr uint32_t kInstanceCellSize = 64;
// Visible entries of instances carry this bit in their id (bits 0..27: pool index), objects stay below it.
export inline constexpr uint32_t kInstanceEntryBit = 1u << 28;
// Pool capacity limit. The per-view visibility bits reserve one bit per pool slot up to this many.
export inline constexpr uint32_t kMaxPoolInstances = 1u << 22;
export inline constexpr uint32_t kMaxPoolCells = kMaxPoolInstances / kInstanceCellSize;
export inline constexpr uint32_t kInstanceLodLevels = 7;  // mesh LOD slots 0..5 plus the point impostor (mesh.glsl)
export inline constexpr uint32_t kNoInstanceBatch = 0xFFFFFFFFu;

export namespace InstanceFlags {
inline constexpr uint32_t Dead = 1u;      // removed (free mask): culled, the slot and its index stay
inline constexpr uint32_t NoShadow = 2u;  // drawn, but not into shadow maps
}

// One instance, relative to its batch's entity (the batch transform; floating origin follows the entity).
export struct InstanceData {
    glm::vec3 position{0.0f};
    float scale = 1.0f;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t color = 0xFFFFFFFFu;  // RGBA8 tint (red in the low byte), surface shaders read it as instanceColor
    uint32_t seed = 0;             // 24 bits: instanceSeed (variation, wind phase, fade dither)
    uint32_t flags = 0;            // InstanceFlags
    bool operator==(const InstanceData&) const = default;
};

export struct InstanceBatchSettings {
    float drawDistance = 0.0f;  // metres from the camera; 0 = unlimited
    float fadeStart = 0.0f;     // metres; from here to drawDistance instances thin out stochastically (seed dither); 0 = hard cut
    float lodBias = 1.0f;       // multiplies the screen-size LOD metric (as Renderer::setLodBias, per batch)
    bool castShadows = true;
    bool operator==(const InstanceBatchSettings&) const = default;
};

// World component. The renderer mirrors it (Engine::syncInstanceBatches each frame): `layoutVersion` changes (count, mesh,
// material, shader, settings) re-allocate the batch's pool cells, `version` changes upload only the cells touched by the
// dirty range.
export struct InstanceBatch {
    uint32_t mesh = 0xFFFFFFFFu;
    uint32_t material = 0;
    uint32_t shader = 0;
    InstanceBatchSettings settings;
    std::vector<InstanceData> instances;
    std::vector<uint64_t> keys;  // optional stable keys (empty, or one per instance; swapRemove keeps them in step)
    uint64_t version = 1;
    uint64_t layoutVersion = 1;
    uint32_t dirtyFirst = 0xFFFFFFFFu;
    uint32_t dirtyEnd = 0;

    void assign(std::span<const InstanceData> data);
    // Overwrites instances [first, first + data.size()) (grows the batch when it writes past the end).
    void update(uint32_t first, std::span<const InstanceData> data);
    // Free mask: the instance is marked dead, every index stays valid. False if out of range or already dead.
    bool remove(uint32_t index);
    bool revive(uint32_t index);
    // Swap-remove: the last instance moves into `index` and the batch shrinks by one. Returns the key of the moved instance (if keys are kept).
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

// GPU records (std430, mirror resources/shaders/include/instancing.glsl).
export struct InstanceGpu {
    glm::vec4 positionScale{0.0f};
    uint32_t packed[4] = {0, 0, 0, 0};  // quaternion snorm16 xy, zw; colour RGBA8; seed (24) | flags << 24
};
static_assert(sizeof(InstanceGpu) == 32);

export struct InstanceCellGpu {
    glm::vec4 sphere{0.0f};           // batch-local bounds of the cell's live instances (mesh sphere per instance)
    uint32_t info[4] = {0, 0, 0, 0};  // batch, instance count, live count, unused
};
static_assert(sizeof(InstanceCellGpu) == 32);

export struct InstanceBatchGpu {
    uint32_t info[4] = {0, 0, 0, 0};  // owner entity, mesh slot, packed renderable (as PackRenderable), flags (InstanceBatchGpuFlags)
    glm::vec4 meshSphere{0.0f};       // mesh-local bounds (shader displacement bounds included)
    glm::vec4 lod{0.0f};              // lodBias, fadeStart, drawDistance, unused
    uint32_t cells[4] = {0, 0, 0, 0};  // first cell, cell count, instance count, unused
};
static_assert(sizeof(InstanceBatchGpu) == 64);

export namespace InstanceBatchGpuFlags {
inline constexpr uint32_t Active = 1u;
inline constexpr uint32_t Visible = 2u;
inline constexpr uint32_t Shadows = 4u;
}

export [[nodiscard]] InstanceGpu packInstance(const InstanceData& instance);
export [[nodiscard]] InstanceData unpackInstance(const InstanceGpu& gpu);
// 24-bit dither hash of a seed in [0, 1) (same integer hash as instanceHash01 in instancing.glsl).
export [[nodiscard]] float instanceHash01(uint32_t seed);
export [[nodiscard]] glm::vec4 instanceColorFloat(uint32_t rgba8);
export [[nodiscard]] uint32_t packInstanceColor(const glm::vec4& color);

// Spatial order of a batch's instances (3D Morton code of the position in the batch bounds): consecutive runs of 64 become
// compact cells, so a cell rejected as a whole (frustum, distance, Hi-Z) saves 64 instance tests.
export [[nodiscard]] std::vector<uint32_t> instanceSpatialOrder(std::span<const InstanceData> instances);
// Bounds of one cell: `slots` are instance indices (<= 64), meshSphere the mesh-local sphere. Dead instances do not count.
export [[nodiscard]] InstanceCellGpu buildInstanceCell(std::span<const InstanceData> instances, std::span<const uint32_t> slots, const glm::vec4& meshSphere, uint32_t batch);

// Sub-allocator of the instance pool in whole cells. Best-fit over a free list; the pool grows at the end (high water)
// up to `limit` cells; freeing the last range lowers the high water again.
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
    std::map<uint32_t, uint32_t> m_free;  // first cell -> count, below the high water
    uint32_t m_highWater = 0;
    uint32_t m_used = 0;
    uint32_t m_limit = kMaxPoolCells;
};

// CPU reference of the camera-view instance cull (instance_cells.comp + instance_cull.comp without Hi-Z occlusion).
export struct InstanceCullView {
    std::array<glm::vec4, 6> planes{};  // world-space frustum planes (xyz normal pointing inside, w distance)
    glm::mat4 view{1.0f};
    glm::vec3 position{0.0f};
    float pixelScale = 1.0f;  // projection[1][1] * render height (projectedDiameterPx)
    float lodBias = 1.0f;
    float distanceScale = 1.0f;  // multiplies every batch's drawDistance and fadeStart (setting instanceDistanceScale)
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

// `owner` is the batch entity's world matrix; meshSphere as for buildInstanceCell. Cells are formed exactly as the renderer forms them.
export [[nodiscard]] InstanceCullResult cullInstancesReference(const InstanceBatch& batch, const glm::mat4& owner, const glm::vec4& meshSphere, const InstanceCullView& view);
export [[nodiscard]] uint32_t instanceLodForDiameter(float diameterPx);
export [[nodiscard]] std::array<glm::vec4, 6> instanceFrustumPlanes(const glm::mat4& viewProjection);

// Renderer statistics of the last frame whose counters were read back (no stall: read 3 frames late).
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
    uint32_t instances = 0;      // live instances in the pool
    uint32_t poolCells = 0;      // high water of the pool, in cells
    uint32_t poolCapacity = 0;   // allocated GPU instance slots
    uint64_t uploadedBytes = 0;  // last frame
    uint64_t frame = 0;          // frame the counters below belong to
    InstanceViewStats early;     // main view, early (previous Hi-Z) phase
    InstanceViewStats retest;    // main view, retest phase
    InstanceViewStats shadow;    // cascades and local shadow maps, summed
    std::array<uint32_t, kInstanceLodLevels> perLod{};  // main view drawn instances per LOD slot
};

// Text form of the component ("InstanceBatch" serializer: mesh id, material, shader, settings, instances).
export [[nodiscard]] std::string instanceBatchText(const InstanceBatch& batch);
export [[nodiscard]] std::optional<InstanceBatch> instanceBatchFromText(const std::string& text);
