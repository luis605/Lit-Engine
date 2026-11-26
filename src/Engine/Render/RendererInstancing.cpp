module;

#include "RendererPrelude.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Instancing;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

using BufferPtr = Diligent::RefCntAutoPtr<Diligent::IBuffer>;
using PipelinePtr = Diligent::RefCntAutoPtr<Diligent::IPipelineState>;
using BindingPtr = Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>;

static_assert(kInstanceEntryBit == (1u << 28), "instancing.glsl INSTANCE_ENTRY_BIT");
static_assert(kMaxPoolInstances / 32 == kInstanceVisibilityWords, "visibility words reserved for the pool (RendererTypes.hpp)");

enum InstanceMode : int { kModeEarly = 0, kModeRetest = 1, kModeCascade = 2, kModeLocal = 3, kModeCount = 4 };
constexpr const char* kModeMacros[kModeCount] = {"INSTANCE_EARLY", "INSTANCE_RETEST", "INSTANCE_CASCADE", "INSTANCE_LOCAL"};
constexpr uint32_t kCellGroup = 256;
constexpr uint32_t kStatsGroupWords = 16;
constexpr uint32_t kStatsGroups = 4;
constexpr uint32_t kStatsWords = kStatsGroupWords * kStatsGroups;
constexpr uint32_t kFlagOcclusion = 1;
constexpr uint32_t kFlagCells = 2;
constexpr uint32_t kFlagStats = 4;
constexpr uint32_t kForcedNone = 0xFFFFFFFFu;
constexpr uint32_t kInitialPoolCells = 1024;
constexpr uint64_t kUploadChunkBytes = 4u << 20;
constexpr int kStagingFrames = 3;

struct InstanceCullConstants {
    uint32_t counts[4];
    uint32_t config[4];
    glm::vec4 params;
    glm::vec4 planes[6];
    glm::vec4 localSpheres[kMaxLocalShadowLights];
    glm::vec4 localCones[kMaxLocalShadowLights];
    uint32_t localSlots[kMaxLocalShadowLights][4];
    uint32_t localInfo[4];
    glm::vec4 localOrigins[kMaxLocalShadowViews];
    uint32_t stats[4];
};
static_assert(sizeof(InstanceCullConstants) == 16 * (3 + 6 + 8 * 3 + 1 + kMaxLocalShadowViews + 1), "InstanceCullConstants must match instance_cull.glsl (std140)");

struct ArgsConstants {
    uint32_t maxCount;
    uint32_t workgroupSize;
    uint32_t pad[2];
};

InstanceViewStats ReadStats(const uint32_t* words) {
    InstanceViewStats stats;
    stats.cellsTested = words[0];
    stats.cellsAccepted = words[1];
    stats.instancesTested = words[2];
    stats.visible = words[3];
    stats.occluded = words[4];
    stats.overflow = words[5];
    return stats;
}

uint32_t ObjectVisibilityWords(size_t maxObjects) { return static_cast<uint32_t>((maxObjects + kWorkgroupSize - 1) / kWorkgroupSize * kVisibilityBitWordsPerGroup); }

}

struct InstancingState {
    struct Batch {
        bool used = false;
        Entity owner = INVALID_ENTITY;
        bool visible = true;
        uint32_t mesh = NO_MESH;
        uint32_t material = 0;
        uint32_t shader = 0;
        InstanceBatchSettings settings;
        uint32_t firstCell = 0;
        uint32_t cellCount = 0;
        uint32_t instanceCount = 0;
        uint32_t alive = 0;
        std::vector<uint32_t> order;
        std::vector<uint32_t> slotOf;
        uint64_t layoutVersion = 0;
        uint64_t version = 0;
        glm::vec4 meshSphere{0.0f};
        uint64_t meshInfoStamp = 0;
    };

    std::vector<Batch> batches;
    std::vector<uint32_t> freeBatches;
    InstanceAllocator allocator;
    std::vector<InstanceCellGpu> cells;
    std::vector<InstanceBatchGpu> batchRecords;
    uint32_t cellDirtyFirst = 0xFFFFFFFFu;
    uint32_t cellDirtyEnd = 0;
    bool batchesDirty = true;
    std::vector<std::pair<uint32_t, std::vector<InstanceGpu>>> pending;
    uint32_t liveBatches = 0;
    uint32_t liveInstances = 0;
    uint64_t uploadedBytes = 0;

    BufferPtr instanceBuffer;
    BufferPtr cellBuffer;
    BufferPtr batchBuffer;
    uint32_t poolCapacityCells = 0;
    uint32_t cellCapacity = 0;
    uint32_t batchCapacity = 0;
    uint64_t bufferGeneration = 1;

    PipelinePtr cellsPSO[kModeCount];
    PipelinePtr cullPSO[kModeCount];
    bool failed = false;
    bool pipelinesBuilt = false;
    BufferPtr constants;
    BufferPtr cellCounter;
    BufferPtr cellList;
    BufferPtr cellArgs;
    BufferPtr argsConstants;
    BindingPtr argsSRB;
    BufferPtr statsBuffer;
    BufferPtr staging[kStagingFrames];
    bool stagingValid[kStagingFrames] = {};
    uint64_t stagingFrame[kStagingFrames] = {};
    uint64_t statsFrame = 0;
    InstanceRenderStats lastStats;

    struct Bindings {
        std::array<const void*, 10> key{};
        BindingPtr cells;
        BindingPtr cull;
    };
    Bindings bindings[kModeCount][DiligentData::NumFrames];
};

namespace {
InstancingState* Instancing(const std::shared_ptr<void>& state) { return static_cast<InstancingState*>(state.get()); }

void MarkCells(InstancingState& state, uint32_t first, uint32_t count) {
    if (count == 0) return;
    state.cellDirtyFirst = std::min(state.cellDirtyFirst, first);
    state.cellDirtyEnd = std::max(state.cellDirtyEnd, first + count);
}

void PackCells(InstancingState& state, uint32_t batchIndex, const InstanceBatch& data, uint32_t firstLocal, uint32_t count) {
    InstancingState::Batch& batch = state.batches[batchIndex];
    if (count == 0) return;
    const uint32_t firstSlot = firstLocal * kInstanceCellSize;
    const uint32_t endSlot = std::min(batch.instanceCount, (firstLocal + count) * kInstanceCellSize);
    std::vector<InstanceGpu> records(static_cast<size_t>(count) * kInstanceCellSize);
    for (uint32_t slot = firstSlot; slot < endSlot; ++slot) records[slot - firstSlot] = packInstance(data.instances[batch.order[slot]]);
    for (uint32_t slot = std::max(endSlot, firstSlot); slot < (firstLocal + count) * kInstanceCellSize; ++slot) records[slot - firstSlot].packed[3] = InstanceFlags::Dead << 24u;
    state.pending.emplace_back((batch.firstCell + firstLocal) * kInstanceCellSize, std::move(records));
    for (uint32_t cell = firstLocal; cell < firstLocal + count; ++cell) {
        const uint32_t begin = cell * kInstanceCellSize;
        const uint32_t end = std::min(batch.instanceCount, begin + kInstanceCellSize);
        const std::span<const uint32_t> slots(batch.order.data() + begin, end - begin);
        state.cells[batch.firstCell + cell] = buildInstanceCell(data.instances, slots, batch.meshSphere, batchIndex);
    }
    MarkCells(state, batch.firstCell + firstLocal, count);
}

void ReleaseCells(InstancingState& state, InstancingState::Batch& batch) {
    if (batch.cellCount == 0) return;
    for (uint32_t cell = batch.firstCell; cell < batch.firstCell + batch.cellCount; ++cell) state.cells[cell] = InstanceCellGpu{};
    MarkCells(state, batch.firstCell, batch.cellCount);
    state.allocator.free(batch.firstCell, batch.cellCount);
    batch.cellCount = 0;
    batch.firstCell = 0;
}
}

uint32_t Renderer::createInstanceBatch() {
    if (!m_instancing) m_instancing = std::make_shared<InstancingState>();
    auto& state = *Instancing(m_instancing);
    uint32_t index;
    if (!state.freeBatches.empty()) {
        index = state.freeBatches.back();
        state.freeBatches.pop_back();
    } else {
        index = static_cast<uint32_t>(state.batches.size());
        state.batches.emplace_back();
        state.batchRecords.emplace_back();
    }
    state.batches[index] = InstancingState::Batch{};
    state.batches[index].used = true;
    state.batchRecords[index] = InstanceBatchGpu{};
    state.batchesDirty = true;
    ++state.liveBatches;
    return index;
}

void Renderer::destroyInstanceBatch(uint32_t batchIndex) {
    if (!m_instancing) return;
    auto& state = *Instancing(m_instancing);
    if (batchIndex >= state.batches.size() || !state.batches[batchIndex].used) return;
    InstancingState::Batch& batch = state.batches[batchIndex];
    ReleaseCells(state, batch);
    state.liveInstances -= std::min(state.liveInstances, batch.alive);
    batch = InstancingState::Batch{};
    state.batchRecords[batchIndex] = InstanceBatchGpu{};
    state.freeBatches.push_back(batchIndex);
    state.batchesDirty = true;
    --state.liveBatches;
}

void Renderer::syncInstanceBatch(uint32_t batchIndex, const InstanceBatch& data, Entity owner, bool visible) {
    if (!m_instancing) return;
    auto& state = *Instancing(m_instancing);
    if (batchIndex >= state.batches.size() || !state.batches[batchIndex].used) return;
    InstancingState::Batch& batch = state.batches[batchIndex];
    const glm::vec4 bounds = data.mesh < gMeshInfos.size() ? getMeshBounds(data.mesh) : glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
    const float shaderBounds = m_diligent && data.shader < m_diligent->surfaces.size() ? m_diligent->surfaces[data.shader].bounds : 0.0f;
    const glm::vec4 meshSphere(glm::vec3(bounds), bounds.w + shaderBounds);
    const bool layout = batch.layoutVersion != data.layoutVersion || batch.mesh != data.mesh || batch.meshSphere != meshSphere;
    if (layout) {
        ReleaseCells(state, batch);
        state.liveInstances -= std::min(state.liveInstances, batch.alive);
        batch.mesh = data.mesh;
        batch.material = data.material;
        batch.shader = data.shader;
        batch.settings = data.settings;
        batch.meshSphere = meshSphere;
        batch.instanceCount = static_cast<uint32_t>(std::min<size_t>(data.instances.size(), kMaxPoolInstances));
        batch.order = instanceSpatialOrder(std::span<const InstanceData>(data.instances.data(), batch.instanceCount));
        batch.slotOf.assign(batch.instanceCount, 0);
        for (uint32_t slot = 0; slot < batch.instanceCount; ++slot) batch.slotOf[batch.order[slot]] = slot;
        const uint32_t cellCount = (batch.instanceCount + kInstanceCellSize - 1) / kInstanceCellSize;
        if (cellCount > 0) {
            const std::optional<uint32_t> first = state.allocator.allocate(cellCount);
            if (!first) {
                Lit::Log::Error("Instance pool full: batch of {} instances not drawn ({} of {} cells used)", batch.instanceCount, state.allocator.usedCells(), state.allocator.limit());
                batch.instanceCount = 0;
                batch.order.clear();
                batch.slotOf.clear();
            } else {
                batch.firstCell = *first;
                batch.cellCount = cellCount;
                if (state.cells.size() < state.allocator.highWater()) state.cells.resize(state.allocator.highWater());
                PackCells(state, batchIndex, data, 0, cellCount);
            }
        }
        batch.alive = data.aliveCount();
        state.liveInstances += batch.alive;
        batch.layoutVersion = data.layoutVersion;
        batch.version = data.version;
        state.batchesDirty = true;
    } else if (batch.version != data.version) {
        if (data.dirty() && batch.cellCount > 0) {
            std::vector<uint32_t> touched;
            const uint32_t end = std::min<uint32_t>(data.dirtyEnd, batch.instanceCount);
            for (uint32_t index = data.dirtyFirst; index < end; ++index) touched.push_back(batch.slotOf[index] / kInstanceCellSize);
            std::sort(touched.begin(), touched.end());
            touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
            for (size_t i = 0; i < touched.size();) {
                size_t j = i + 1;
                while (j < touched.size() && touched[j] == touched[j - 1] + 1) ++j;
                PackCells(state, batchIndex, data, touched[i], static_cast<uint32_t>(j - i));
                i = j;
            }
            const uint32_t alive = data.aliveCount();
            state.liveInstances = state.liveInstances - std::min(state.liveInstances, batch.alive) + alive;
            batch.alive = alive;
        }
        batch.version = data.version;
    }
    if (batch.owner != owner || batch.visible != visible || batch.material != data.material || batch.shader != data.shader || !(batch.settings == data.settings)) {
        batch.owner = owner;
        batch.visible = visible;
        batch.material = data.material;
        batch.shader = data.shader;
        batch.settings = data.settings;
        state.batchesDirty = true;
    }
    InstanceBatchGpu record;
    RenderableComponent renderable;
    renderable.mesh_uuid = batch.mesh;
    renderable.material_uuid = batch.material;
    renderable.shaderId = batch.shader;
    record.info[0] = owner;
    record.info[1] = batch.mesh;
    record.info[2] = PackRenderable(renderable);
    const bool active = batch.cellCount > 0 && owner != INVALID_ENTITY && batch.mesh < gMeshInfos.size();
    record.info[3] = (active ? InstanceBatchGpuFlags::Active : 0u) | (visible ? InstanceBatchGpuFlags::Visible : 0u) | (batch.settings.castShadows ? InstanceBatchGpuFlags::Shadows : 0u);
    record.meshSphere = batch.meshSphere;
    record.lod = glm::vec4(batch.settings.lodBias, batch.settings.fadeStart, batch.settings.drawDistance, 0.0f);
    record.cells[0] = batch.firstCell;
    record.cells[1] = batch.cellCount;
    record.cells[2] = batch.instanceCount;
    if (std::memcmp(&record, &state.batchRecords[batchIndex], sizeof(record)) != 0) {
        state.batchRecords[batchIndex] = record;
        state.batchesDirty = true;
    }
}

bool Renderer::instancingActive() const {
    if (!m_instancing || !m_diligent) return false;
    const auto& state = *Instancing(m_instancing);
    return !state.failed && state.liveBatches > 0 && state.allocator.highWater() > 0 && state.instanceBuffer && state.cellBuffer && const_cast<Lighting&>(m_lighting).settings().instancing;
}

void Renderer::uploadInstances() {
    if (!m_instancing || !m_diligent) return;
    auto& state = *Instancing(m_instancing);
    auto& gpu = *m_diligent;
    auto* device = gpu.pDevice.RawPtr();
    auto* context = gpu.pImmediateContext.RawPtr();
    if (!state.pipelinesBuilt) {
        state.pipelinesBuilt = true;
        for (int mode = 0; mode < kModeCount; ++mode) {
            const Diligent::ShaderMacro macro[] = {{kModeMacros[mode], "1"}};
            const bool camera = mode == kModeEarly || mode == kModeRetest;
            const std::string cellsName = std::format("Instance Cells PSO {}", mode);
            const std::string cullName = std::format("Instance Cull PSO {}", mode);
            state.cellsPSO[mode] = CreateComputePipeline(device, cellsName.c_str(), "resources/shaders/instance_cells.comp", camera ? std::initializer_list<std::string_view>{"u_hizTexture"} : std::initializer_list<std::string_view>{}, Diligent::ShaderMacroArray{macro, 1});
            state.cullPSO[mode] = CreateComputePipeline(device, cullName.c_str(), "resources/shaders/instance_cull.comp", camera ? std::initializer_list<std::string_view>{"u_hizTexture"} : std::initializer_list<std::string_view>{}, Diligent::ShaderMacroArray{macro, 1});
            if (!state.cellsPSO[mode] || !state.cullPSO[mode]) state.failed = true;
        }
        const uint32_t zeros[kStatsWords] = {};
        state.constants = CreateUniformBuffer(device, "Instance Cull Constants", sizeof(InstanceCullConstants), Diligent::USAGE_DEFAULT);
        state.cellCounter = CreateStructuredBuffer(device, "Instance Cell Counter", sizeof(uint32_t), 1, const_cast<uint32_t*>(zeros));
        state.cellList = CreateStructuredBuffer(device, "Instance Cell List", sizeof(uint32_t) * 2, kMaxPoolCells);
        state.cellArgs = CreateDispatchArgsBuffer(device, "Instance Cull Args");
        state.argsConstants = CreateUniformBuffer(device, "Instance Args Constants", sizeof(ArgsConstants), Diligent::USAGE_DEFAULT);
        state.statsBuffer = CreateStructuredBuffer(device, "Instance Stats", sizeof(uint32_t), kStatsWords, const_cast<uint32_t*>(zeros));
        for (int slot = 0; slot < kStagingFrames; ++slot) {
            Diligent::BufferDesc desc;
            desc.Name = "Instance Stats Readback";
            desc.Usage = Diligent::USAGE_STAGING;
            desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
            desc.Size = kStatsWords * sizeof(uint32_t);
            device->CreateBuffer(desc, nullptr, &state.staging[slot]);
        }
        if (!state.constants || !state.cellCounter || !state.cellList || !state.cellArgs || !state.argsConstants || !state.statsBuffer) state.failed = true;
        if (state.failed) Lit::Log::Error("Instancing pipelines or buffers failed; instance batches are not drawn.");
        if (!state.failed) {
            const ArgsConstants args{kMaxPoolCells, 1u, {0u, 0u}};
            LIT_UP(context)->UpdateBuffer(state.argsConstants, 0, sizeof(args), &args, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            state.argsSRB = litgpu::makeBinding(gpu.pDispatchArgsPSO, {
                                                                          {"SourceCounterBuffer",   state.cellCounter  },
                                                                          {"DispatchArgsBuffer",    state.cellArgs     },
                                                                          {"DispatchArgsConstants", state.argsConstants}
            });
        }
    }
    if (state.failed) return;

    if (state.statsFrame != m_frameCounter) {
        const int slot = static_cast<int>(m_frameCounter % kStagingFrames);
        if (state.stagingValid[slot] && state.staging[slot]) {
            Diligent::MapHelper<uint32_t> mapped(context, state.staging[slot], Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
            if (mapped) {
                const uint32_t* words = mapped;
                state.lastStats.early = ReadStats(words);
                state.lastStats.retest = ReadStats(words + kStatsGroupWords);
                state.lastStats.shadow = ReadStats(words + 2 * kStatsGroupWords);
                for (uint32_t lod = 0; lod < kInstanceLodLevels; ++lod) state.lastStats.perLod[lod] = words[6 + lod] + words[kStatsGroupWords + 6 + lod];
                state.lastStats.frame = state.stagingFrame[slot];
            }
        }
        if (state.statsFrame != 0 && state.staging[slot]) {
            context->CopyBuffer(state.statsBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, state.staging[slot], 0, kStatsWords * sizeof(uint32_t), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            state.stagingValid[slot] = true;
            state.stagingFrame[slot] = state.statsFrame;
        }
        const std::array<uint32_t, kStatsWords> zeros{};
        LIT_UP(context)->UpdateBuffer(state.statsBuffer, 0, sizeof(zeros), zeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        state.statsFrame = m_frameCounter;
    }

    state.uploadedBytes = 0;
    bool rebind = false;
    const uint32_t neededCells = std::max(state.allocator.highWater(), 1u);
    if (neededCells > state.poolCapacityCells || !state.instanceBuffer) {
        uint32_t capacity = std::max(state.poolCapacityCells, kInitialPoolCells);
        while (capacity < neededCells) capacity *= 2;
        capacity = std::min(capacity, kMaxPoolCells);
        BufferPtr grown = CreateStructuredBuffer(device, "Instance Pool", sizeof(InstanceGpu), capacity * kInstanceCellSize);
        if (!grown) {
            state.failed = true;
            return;
        }
        if (state.instanceBuffer && state.poolCapacityCells > 0) context->CopyBuffer(state.instanceBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, grown, 0, static_cast<Diligent::Uint64>(state.poolCapacityCells) * kInstanceCellSize * sizeof(InstanceGpu), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        state.instanceBuffer = grown;
        state.poolCapacityCells = capacity;
        rebind = true;
    }
    if (state.cells.size() < state.allocator.highWater()) state.cells.resize(state.allocator.highWater());
    if (state.cells.size() > state.cellCapacity || !state.cellBuffer) {
        state.cellCapacity = std::max<uint32_t>(state.poolCapacityCells, static_cast<uint32_t>(state.cells.size()));
        std::vector<InstanceCellGpu> init(state.cellCapacity);
        std::copy(state.cells.begin(), state.cells.end(), init.begin());
        state.cellBuffer = CreateStructuredBuffer(device, "Instance Cells", sizeof(InstanceCellGpu), state.cellCapacity, init.data());
        state.cellDirtyFirst = 0xFFFFFFFFu;
        state.cellDirtyEnd = 0;
        rebind = true;
    }
    if (state.batchRecords.size() > state.batchCapacity || !state.batchBuffer) {
        state.batchCapacity = std::max<uint32_t>(64, static_cast<uint32_t>(std::bit_ceil(std::max<size_t>(state.batchRecords.size(), 1))));
        std::vector<InstanceBatchGpu> init(state.batchCapacity);
        std::copy(state.batchRecords.begin(), state.batchRecords.end(), init.begin());
        state.batchBuffer = CreateStructuredBuffer(device, "Instance Batches", sizeof(InstanceBatchGpu), state.batchCapacity, init.data());
        state.batchesDirty = false;
        rebind = true;
    }
    if (!state.instanceBuffer || !state.cellBuffer || !state.batchBuffer) {
        state.failed = true;
        return;
    }
    for (auto& [firstSlot, records] : state.pending) {
        const uint64_t bytes = records.size() * sizeof(InstanceGpu);
        for (uint64_t offset = 0; offset < bytes; offset += kUploadChunkBytes) {
            const uint64_t size = std::min(kUploadChunkBytes, bytes - offset);
            LIT_UP(context)->UpdateBuffer(state.instanceBuffer, static_cast<Diligent::Uint64>(firstSlot) * sizeof(InstanceGpu) + offset, size, reinterpret_cast<const uint8_t*>(records.data()) + offset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        state.uploadedBytes += bytes;
    }
    state.pending.clear();
    if (state.cellDirtyEnd > state.cellDirtyFirst) {
        const uint32_t end = std::min<uint32_t>(state.cellDirtyEnd, static_cast<uint32_t>(state.cells.size()));
        if (end > state.cellDirtyFirst) {
            const uint64_t bytes = static_cast<uint64_t>(end - state.cellDirtyFirst) * sizeof(InstanceCellGpu);
            LIT_UP(context)->UpdateBuffer(state.cellBuffer, static_cast<Diligent::Uint64>(state.cellDirtyFirst) * sizeof(InstanceCellGpu), bytes, state.cells.data() + state.cellDirtyFirst, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            state.uploadedBytes += bytes;
        }
        state.cellDirtyFirst = 0xFFFFFFFFu;
        state.cellDirtyEnd = 0;
    }
    if (state.batchesDirty && !state.batchRecords.empty()) {
        const uint64_t bytes = state.batchRecords.size() * sizeof(InstanceBatchGpu);
        LIT_UP(context)->UpdateBuffer(state.batchBuffer, 0, bytes, state.batchRecords.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        state.uploadedBytes += bytes;
        state.batchesDirty = false;
    }
    if (rebind) {
        gpu.pInstanceBuffer = state.instanceBuffer;
        gpu.pInstanceCellBuffer = state.cellBuffer;
        gpu.pInstanceBatchBuffer = state.batchBuffer;
        ++state.bufferGeneration;
        rebindSurfaceResources();
        invalidateMeshletBindings();
    }
}

void Renderer::dispatchInstanceCull(int mode, int phaseIndex, Diligent::ITexture* hiz, const void* constantsData) {
    auto& gpu = *m_diligent;
    auto& state = *Instancing(m_instancing);
    auto* context = gpu.pImmediateContext.RawPtr();
    auto& phase = gpu.visibility[phaseIndex];
    const int frame = static_cast<int>(m_currentFrame);
    auto& bindings = state.bindings[mode][frame];
    const std::array<const void*, 10> key = {phase.visibleCounter.RawPtr(), phase.visibleEntries.RawPtr(), phase.bucketCounts.RawPtr(), gpu.pWorldMatrixBuffer[frame].RawPtr(), gpu.pSceneUBO[frame].RawPtr(), state.instanceBuffer.RawPtr(), state.cellBuffer.RawPtr(), state.batchBuffer.RawPtr(), gpu.pVisibilityBits.RawPtr(), m_lighting.materialBuffer()};
    if (key != bindings.key || !bindings.cells || !bindings.cull) {
        bindings.key = key;
        bindings.cells = litgpu::makeBinding(state.cellsPSO[mode], {
                                                                       {"SceneData",             gpu.pSceneUBO[frame]         },
                                                                       {"InstanceCullConstants", state.constants              },
                                                                       {"InstanceStatsBuffer",   state.statsBuffer            },
                                                                       {"WorldMatrixBuffer",     gpu.pWorldMatrixBuffer[frame]},
                                                                       {"InstanceBuffer",        state.instanceBuffer         },
                                                                       {"InstanceCellBuffer",    state.cellBuffer             },
                                                                       {"InstanceBatchBuffer",   state.batchBuffer            },
                                                                       {"VisibilityBuffer",      gpu.pVisibilityBits          },
                                                                       {"CellCounterBuffer",     state.cellCounter            },
                                                                       {"CellListBuffer",        state.cellList               }
        });
        bindings.cull = litgpu::makeBinding(state.cullPSO[mode], {
                                                                     {"SceneData",             gpu.pSceneUBO[frame]         },
                                                                     {"InstanceCullConstants", state.constants              },
                                                                     {"InstanceStatsBuffer",   state.statsBuffer            },
                                                                     {"WorldMatrixBuffer",     gpu.pWorldMatrixBuffer[frame]},
                                                                     {"InstanceBuffer",        state.instanceBuffer         },
                                                                     {"InstanceCellBuffer",    state.cellBuffer             },
                                                                     {"InstanceBatchBuffer",   state.batchBuffer            },
                                                                     {"VisibilityBuffer",      gpu.pVisibilityBits          },
                                                                     {"CellCounterBuffer",     state.cellCounter            },
                                                                     {"CellListBuffer",        state.cellList               },
                                                                     {"AtomicCounterBuffer",   phase.visibleCounter         },
                                                                     {"VisibleObjectBuffer",   phase.visibleEntries         },
                                                                     {"BucketCountBuffer",     phase.bucketCounts           },
                                                                     {"MaterialBuffer",        m_lighting.materialBuffer()  }
        });
    }
    if (!bindings.cells || !bindings.cull) return;
    if (hiz) {
        SetDynamicVariable(bindings.cells, "u_hizTexture", hiz->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
        SetDynamicVariable(bindings.cull, "u_hizTexture", hiz->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    }
    LIT_UP(context)->UpdateBuffer(state.constants, 0, sizeof(InstanceCullConstants), constantsData, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ClearCounter(context, state.cellCounter);
    const uint32_t cells = state.allocator.highWater();
    litgpu::dispatch(context, state.cellsPSO[mode], bindings.cells, (cells + kCellGroup - 1) / kCellGroup);
    DispatchSingleGroup(context, gpu.pDispatchArgsPSO, state.argsSRB);
    context->SetPipelineState(state.cullPSO[mode]);
    context->CommitShaderResources(bindings.cull, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{state.cellArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
}

namespace {
void FillCommon(InstanceCullConstants& constants, const InstancingState& state, const RenderSettings& settings, size_t maxObjects, size_t shaderCount, float lodBias, int forcedLod, uint32_t statsGroup) {
    constants.counts[0] = state.allocator.highWater();
    constants.counts[1] = static_cast<uint32_t>(VisibleEntryCapacity(maxObjects));
    constants.counts[2] = static_cast<uint32_t>(shaderCount);
    constants.counts[3] = BucketSlotCount();
    constants.config[0] = ObjectVisibilityWords(maxObjects);
    constants.config[1] = forcedLod >= 0 ? static_cast<uint32_t>(forcedLod) : kForcedNone;
    constants.config[2] = ActiveBucketCount(shaderCount);
    constants.config[3] = (settings.instanceOcclusion ? kFlagOcclusion : 0u) | (settings.instanceCellCull ? kFlagCells : 0u) | kFlagStats;
    constants.params = glm::vec4(lodBias, 0.0f, std::max(settings.instanceDistanceScale, 0.0f), static_cast<float>(kMaxPoolCells));
    constants.stats[0] = std::min(statsGroup, kStatsGroups - 1) * kStatsGroupWords;
}
}

void Renderer::cullInstances(int phaseIndex, Diligent::ITexture* hiz, uint32_t statsGroup) {
    if (!instancingActive() || (phaseIndex != kPhaseOpaque && phaseIndex != kPhaseRetest) || !hiz) return;
    auto& state = *Instancing(m_instancing);
    InstanceCullConstants constants{};
    FillCommon(constants, state, m_lighting.settings(), m_maxObjects, m_numDrawingShaders, m_lodBias, m_forcedLod, statsGroup);
    dispatchInstanceCull(phaseIndex == kPhaseOpaque ? kModeEarly : kModeRetest, phaseIndex, hiz, &constants);
}

void Renderer::cullInstanceCascade(int cascadeIndex) {
    if (!instancingActive()) return;
    auto& state = *Instancing(m_instancing);
    const Cascade& cascade = m_lighting.cascades()[static_cast<size_t>(cascadeIndex)];
    InstanceCullConstants constants{};
    FillCommon(constants, state, m_lighting.settings(), m_maxObjects, m_numDrawingShaders, m_lodBias, m_forcedLod, 2);
    constants.config[3] &= ~kFlagOcclusion;
    constants.params.y = cascade.texelsPerUnit;
    for (int plane = 0; plane < 6; ++plane) constants.planes[plane] = cascade.planes[plane];
    dispatchInstanceCull(kModeCascade, kPhaseShadow, nullptr, &constants);
}

void Renderer::cullInstanceLocal(uint32_t firstView, uint32_t viewCount, std::span<const glm::vec4> origins) {
    if (!instancingActive()) return;
    auto& state = *Instancing(m_instancing);
    InstanceCullConstants constants{};
    FillCommon(constants, state, m_lighting.settings(), m_maxObjects, m_numDrawingShaders, m_lodBias, m_forcedLod, 2);
    constants.config[3] &= ~kFlagOcclusion;
    const int casters = std::min<int>(m_lighting.localShadowCasterCount(), kMaxLocalShadowLights);
    for (int index = 0; index < casters; ++index) {
        const LocalShadowCaster& caster = m_lighting.localShadowCasters()[static_cast<size_t>(index)];
        constants.localSpheres[index] = caster.sphere;
        constants.localCones[index] = caster.cone;
        constants.localSlots[index][0] = caster.faceBase;
        constants.localSlots[index][1] = caster.faceMask;
        constants.localSlots[index][2] = caster.spot;
    }
    constants.localInfo[0] = static_cast<uint32_t>(casters);
    constants.localInfo[1] = firstView;
    constants.localInfo[2] = viewCount;
    for (size_t view = 0; view < origins.size() && view < static_cast<size_t>(kMaxLocalShadowViews); ++view) constants.localOrigins[view] = origins[view];
    dispatchInstanceCull(kModeLocal, kPhaseShadow, nullptr, &constants);
}

InstanceRenderStats Renderer::instanceStats() const {
    InstanceRenderStats stats;
    if (!m_instancing) return stats;
    const auto& state = *Instancing(m_instancing);
    stats = state.lastStats;
    stats.enabled = instancingActive();
    stats.batches = state.liveBatches;
    stats.instances = state.liveInstances;
    stats.poolCells = state.allocator.highWater();
    stats.poolCapacity = state.poolCapacityCells * kInstanceCellSize;
    stats.uploadedBytes = state.uploadedBytes;
    return stats;
}

void Renderer::releaseInstancing() { m_instancing.reset(); }
