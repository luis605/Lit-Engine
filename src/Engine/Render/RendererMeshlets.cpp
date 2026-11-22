module;

#include "RendererPrelude.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.meshlet;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

// Cluster LOD rendering (dense geometry). See resources/shaders/meshlet_*.comp.
//
// Per visibility phase, after the object cull built its sorted visible entries
// (buildVisibilityDraws), cluster meshes are expanded into work items (meshlet_instances), culled
// per cluster with the DAG cut, frustum, cone and Hi-Z (meshlet_cull) and written as one indexed
// draw per work item into a phase-owned expanded index buffer. drawSurfaces then issues those draws
// with the phase's existing surface pipelines and bindings, so G-buffer, transparency, cascaded and
// local shadows and portal views all render clusters. The early opaque phase records clusters that
// failed only the previous-frame Hi-Z; the retest phase tests them again against the new Hi-Z.

namespace {

using BufferPtr = Diligent::RefCntAutoPtr<Diligent::IBuffer>;
using PipelinePtr = Diligent::RefCntAutoPtr<Diligent::IPipelineState>;
using BindingPtr = Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>;

constexpr uint32_t kMeshletGroupSize = 64;
constexpr uint32_t kMeshletMaxWorkItems = 1u << 19;
constexpr std::array<uint32_t, 4> kMeshletDrawBudget = {1u << 18, 1u << 16, 1u << 16, 1u << 20};
constexpr uint32_t kMeshletRetestCapacity = 1u << 18;
constexpr uint32_t kMeshletStatsWords = 16;
constexpr uint32_t kMeshletStatsPerView = 8;
constexpr uint32_t kMeshletShadowStatsBase = 8;
constexpr uint32_t kMeshletLodSlots = kLodLevelCount - 1;  // triangle LOD slots replaced by clusters (not the point slot)
constexpr std::array<uint32_t, kPhaseCount> kMeshletIndexCapacity = {24u << 20, 8u << 20, 4u << 20, 40u << 20};
constexpr uint32_t kFlagFrustum = 1;
constexpr uint32_t kFlagHiZ = 2;
constexpr uint32_t kFlagCone = 4;
constexpr uint32_t kFlagRetestOutput = 8;
constexpr int kStagingFrames = 3;
constexpr float kMeshletShadowErrorScale = 4.0f;

struct GpuClusterMesh {
    glm::vec4 lodCenter{0.0f};
    uint32_t info[4] = {0, 0, 0, 0};
};
static_assert(sizeof(GpuClusterMesh) == 32);

// Spatial chunk of up to 64 consecutive clusters of one DAG level: a sphere enclosing every culling
// and LOD sphere of its clusters plus their error range. meshlet_instances tests chunks (distance-
// bounded LOD range, frustum) so only chunks that can contain selected clusters become work items.
struct GpuClusterChunk {
    glm::vec4 sphere{0.0f};
    float minSelfError = 0.0f;
    float maxParentError = 0.0f;
    uint32_t firstCluster = 0;
    uint32_t clusterCount = 0;
};
static_assert(sizeof(GpuClusterChunk) == 32);

float SphereReach(const glm::vec3& center, const float sphereCenter[3], float radius) { return glm::length(glm::vec3(sphereCenter[0], sphereCenter[1], sphereCenter[2]) - center) + radius; }

struct MeshletConstants {
    uint32_t maxDraws;
    uint32_t views;
    uint32_t viewBuckets;
    uint32_t shaderCount;
    uint32_t flags;
    uint32_t maxWorkItems;
    uint32_t indexCapacity;
    uint32_t drawsPerSlot;
    float errorThreshold;
    uint32_t twoSidedMask;
    uint32_t statsBase;
    uint32_t retestCapacity;
};

uint32_t PhaseSlotCapacity(int phaseIndex) { return static_cast<uint32_t>(kMaxSurfaceShaders) * (phaseIndex == kPhaseShadow ? static_cast<uint32_t>(kMaxLocalShadowViews) : 1u); }

BufferPtr Structured(Diligent::IRenderDevice* device, const char* name, uint32_t stride, size_t count, const void* data = nullptr, Diligent::BIND_FLAGS extra = Diligent::BIND_NONE) {
    return CreateStructuredBuffer(device, name, stride, static_cast<Diligent::Uint32>(std::max<size_t>(count, 1)), const_cast<void*>(data), extra);
}

ClusterViewStats ReadViewStats(const uint32_t* words) {
    ClusterViewStats stats;
    stats.instances = words[0];
    stats.workItems = words[1];
    stats.clustersTested = words[2];
    stats.clustersSelected = words[3];
    stats.clustersDrawn = words[4];
    stats.trianglesDrawn = words[5];
    stats.draws = words[6];
    stats.overflows = words[7];
    return stats;
}

}

struct MeshletState {
    struct Phase {
        BufferPtr constants;
        BufferPtr workCounter;
        BufferPtr workItems;
        BufferPtr indexCounter;
        BufferPtr indices;
        BufferPtr drawCounters;
        BufferPtr drawCommands;
        BufferPtr cullArgs;
        BindingPtr instancesSRB[DiligentData::NumFrames];
        BindingPtr cullSRB[DiligentData::NumFrames];
        BindingPtr argsSRB[DiligentData::NumFrames];
        BindingPtr retestSRB[DiligentData::NumFrames];
        std::array<const void*, 8> keys[DiligentData::NumFrames]{};
        uint32_t slots = 0;
        uint32_t perSlot = 0;
        bool built = false;
    };

    std::vector<MeshCluster> clusters;
    std::vector<uint32_t> vertices;
    std::vector<uint32_t> triangles;
    std::vector<GpuClusterChunk> chunks;
    std::vector<GpuClusterMesh> meshes = std::vector<GpuClusterMesh>(kMaxMeshes);
    std::vector<uint32_t> meshIds;
    uint64_t residentTriangles = 0;
    bool geometryDirty = true;
    bool patchedMeshInfo = false;
    bool failed = false;

    BufferPtr clusterBuffer;
    BufferPtr vertexBuffer;
    BufferPtr triangleBuffer;
    BufferPtr meshBuffer;
    BufferPtr levelBuffer;
    BufferPtr statsBuffer;
    BufferPtr retestCounter;
    BufferPtr retestItems;
    BufferPtr staging[kStagingFrames];
    bool stagingValid[kStagingFrames] = {};
    uint64_t stagingFrame[kStagingFrames] = {};
    uint64_t statsFrame = 0;
    ClusterRenderStats lastStats;

    PipelinePtr instancesPSO;
    PipelinePtr cullPSO;
    PipelinePtr retestPSO;
    PipelinePtr argsPSO;
    Phase phases[kPhaseCount];
};

namespace {
MeshletState* Meshlets(const std::shared_ptr<void>& state) { return static_cast<MeshletState*>(state.get()); }
}

void Renderer::createMeshletPipelines() {
    if (!m_diligent) return;
    if (!m_meshlets) m_meshlets = std::make_shared<MeshletState>();
    auto& state = *Meshlets(m_meshlets);
    auto* device = m_diligent->pDevice.RawPtr();
    state.instancesPSO = CreateComputePipeline(device, "Meshlet Instances PSO", "resources/shaders/meshlet_instances.comp");
    state.cullPSO = CreateComputePipeline(device, "Meshlet Cull PSO", "resources/shaders/meshlet_cull.comp", {"u_hizTexture"});
    state.retestPSO = CreateComputePipeline(device, "Meshlet Retest PSO", "resources/shaders/meshlet_retest.comp");
    state.argsPSO = CreateComputePipeline(device, "Meshlet Args PSO", "resources/shaders/meshlet_args.comp");
    state.failed = !state.instancesPSO || !state.cullPSO || !state.retestPSO || !state.argsPSO;
    if (state.failed) Lit::Log::Error("Cluster LOD pipelines failed to build; falling back to per-object LOD.");
    for (auto& phase : state.phases) {
        for (auto& keys : phase.keys) keys.fill(nullptr);
    }
}

void Renderer::uploadClusterMesh(uint32_t meshId, const ClusterMesh& source) {
    if (meshId >= gMeshInfos.size() || meshId >= kMaxMeshes || source.empty()) return;
    if (!m_meshlets) m_meshlets = std::make_shared<MeshletState>();
    auto& state = *Meshlets(m_meshlets);
    if (state.meshes[meshId].info[1] != 0) return;

    const auto clusterBase = static_cast<uint32_t>(state.clusters.size());
    const auto vertexBase = static_cast<uint32_t>(state.vertices.size());
    const auto triangleBase = static_cast<uint32_t>(state.triangles.size());
    const auto chunkBase = static_cast<uint32_t>(state.chunks.size());
    for (MeshCluster cluster : source.clusters) {
        cluster.vertexOffset += vertexBase;
        cluster.triangleOffset += triangleBase;
        state.clusters.push_back(cluster);
    }
    state.vertices.insert(state.vertices.end(), source.vertices.begin(), source.vertices.end());
    state.triangles.insert(state.triangles.end(), source.triangles.begin(), source.triangles.end());
    for (const ClusterLevel& level : source.levels) {
        for (uint32_t first = level.firstCluster; first < level.firstCluster + level.clusterCount; first += kMeshletGroupSize) {
            const uint32_t count = std::min(kMeshletGroupSize, level.firstCluster + level.clusterCount - first);
            glm::vec3 center(0.0f);
            for (uint32_t id = first; id < first + count; ++id) center += glm::vec3(source.clusters[id].boundsCenter[0], source.clusters[id].boundsCenter[1], source.clusters[id].boundsCenter[2]);
            center /= static_cast<float>(count);
            GpuClusterChunk chunk;
            chunk.minSelfError = kClusterRootError;
            chunk.firstCluster = clusterBase + first;
            chunk.clusterCount = count;
            float radius = 0.0f;
            for (uint32_t id = first; id < first + count; ++id) {
                const MeshCluster& cluster = source.clusters[id];
                radius = std::max({radius, SphereReach(center, cluster.boundsCenter, cluster.boundsRadius), SphereReach(center, cluster.selfCenter, cluster.selfRadius), SphereReach(center, cluster.parentCenter, cluster.parentRadius)});
                chunk.minSelfError = std::min(chunk.minSelfError, cluster.selfError);
                chunk.maxParentError = std::max(chunk.maxParentError, cluster.parentError);
            }
            chunk.sphere = glm::vec4(center, radius * 1.0001f + 1e-6f);
            state.chunks.push_back(chunk);
        }
    }
    GpuClusterMesh& mesh = state.meshes[meshId];
    mesh.lodCenter = glm::vec4(source.lodCenter[0], source.lodCenter[1], source.lodCenter[2], source.lodExtent);
    mesh.info[0] = chunkBase;
    mesh.info[1] = static_cast<uint32_t>(state.chunks.size()) - chunkBase;
    mesh.info[2] = gMeshInfos[meshId].baseVertex;
    state.meshIds.push_back(meshId);
    state.residentTriangles += source.triangleCount(0);
    state.geometryDirty = true;
    m_meshInfoDirty = true;
    Lit::Log::Info("Cluster LOD mesh {}: {} clusters, {} levels, {} -> {} triangles", meshId, source.clusters.size(), source.levels.size(), source.triangleCount(0), source.triangleCount(static_cast<uint32_t>(source.levels.size() - 1)));
}

void Renderer::releaseClusterMesh(uint32_t meshId) {
    if (!m_meshlets || meshId >= kMaxMeshes) return;
    auto& state = *Meshlets(m_meshlets);
    if (state.meshes[meshId].info[1] == 0) return;
    // The cluster arrays are append-only (their space is not reused); the mesh just stops referencing them so the slot can hold another mesh.
    state.meshes[meshId] = GpuClusterMesh{};
    state.meshIds.erase(std::remove(state.meshIds.begin(), state.meshIds.end(), meshId), state.meshIds.end());
    state.geometryDirty = true;
    m_meshInfoDirty = true;
}

bool Renderer::meshletsActive() const {
    return m_meshlets && !Meshlets(m_meshlets)->failed && Meshlets(m_meshlets)->cullPSO && !Meshlets(m_meshlets)->meshIds.empty() && const_cast<Lighting&>(m_lighting).settings().clusterLod;
}

bool Renderer::meshletMeshInfoStale() const { return m_meshlets && Meshlets(m_meshlets)->patchedMeshInfo != meshletsActive(); }

bool Renderer::uploadMeshletMeshInfo(size_t count) {
    if (!m_meshlets) return false;
    auto& state = *Meshlets(m_meshlets);
    state.patchedMeshInfo = meshletsActive();
    if (!state.patchedMeshInfo) return false;
    std::vector<MeshInfo> infos(gMeshInfos.begin(), gMeshInfos.begin() + static_cast<std::ptrdiff_t>(count));
    for (const uint32_t meshId : state.meshIds) {
        for (uint32_t slot = meshId; slot < meshId + kMeshletLodSlots && slot < count; ++slot) infos[slot].indexCount = 0;
    }
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pMeshInfoBuffer, 0, count * sizeof(MeshInfo), infos.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    return true;
}

void Renderer::ensureMeshletBuffers() {
    auto& gpu = *m_diligent;
    auto& state = *Meshlets(m_meshlets);
    auto* device = gpu.pDevice.RawPtr();
    if (state.geometryDirty) {
        state.clusterBuffer = Structured(device, "Cluster Buffer", sizeof(MeshCluster), state.clusters.size(), state.clusters.data());
        state.vertexBuffer = Structured(device, "Cluster Vertex Buffer", sizeof(uint32_t), state.vertices.size(), state.vertices.data());
        state.triangleBuffer = Structured(device, "Cluster Triangle Buffer", sizeof(uint32_t), state.triangles.size(), state.triangles.data());
        state.levelBuffer = Structured(device, "Cluster Chunk Buffer", sizeof(GpuClusterChunk), state.chunks.size(), state.chunks.data());
        state.meshBuffer = Structured(device, "Cluster Mesh Buffer", sizeof(GpuClusterMesh), state.meshes.size(), state.meshes.data());
        state.geometryDirty = false;
        for (auto& phase : state.phases) {
            for (auto& keys : phase.keys) keys.fill(nullptr);
        }
    }
    if (!state.statsBuffer) {
        const std::vector<uint32_t> zeros(1024, 0u);  // counters and draw counters (<= kMaxSurfaceShaders * kMaxLocalShadowViews)
        state.statsBuffer = Structured(device, "Meshlet Stats", sizeof(uint32_t), kMeshletStatsWords, zeros.data());
        state.retestCounter = Structured(device, "Meshlet Retest Counter", sizeof(uint32_t), 1, zeros.data());
        state.retestItems = Structured(device, "Meshlet Retest Items", sizeof(uint32_t) * 4, static_cast<size_t>(kMeshletRetestCapacity) * 2);
        for (int slot = 0; slot < kStagingFrames; ++slot) {
            Diligent::BufferDesc desc;
            desc.Name = "Meshlet Stats Readback";
            desc.Usage = Diligent::USAGE_STAGING;
            desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
            desc.Size = kMeshletStatsWords * sizeof(uint32_t);
            gpu.pDevice->CreateBuffer(desc, nullptr, &state.staging[slot]);
            state.stagingValid[slot] = false;
        }
        for (int phaseIndex = 0; phaseIndex < kPhaseCount; ++phaseIndex) {
            auto& phase = state.phases[phaseIndex];
            const uint32_t slotCapacity = PhaseSlotCapacity(phaseIndex);
            phase.constants = CreateUniformBuffer(device, "Meshlet Constants", sizeof(MeshletConstants), Diligent::USAGE_DEFAULT);
            phase.workCounter = Structured(device, "Meshlet Work Counter", sizeof(uint32_t), 1, zeros.data());
            phase.workItems = Structured(device, "Meshlet Work Items", sizeof(uint32_t) * 4, static_cast<size_t>(kMeshletMaxWorkItems) * 2);
            phase.indexCounter = Structured(device, "Meshlet Index Counter", sizeof(uint32_t), 1, zeros.data());
            phase.indices = Structured(device, "Meshlet Expanded Indices", sizeof(uint32_t), kMeshletIndexCapacity[static_cast<size_t>(phaseIndex)], nullptr, Diligent::BIND_INDEX_BUFFER);
            phase.drawCounters = Structured(device, "Meshlet Draw Counters", sizeof(uint32_t), slotCapacity, zeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
            phase.drawCommands = CreateIndirectBuffer(device, "Meshlet Draw Commands", static_cast<size_t>(kMeshletDrawBudget[static_cast<size_t>(phaseIndex)]) * sizeof(DrawElementsIndirectCommand));
            phase.cullArgs = CreateDispatchArgsBuffer(device, "Meshlet Cull Args");
            if (!phase.indices || !phase.workItems || !phase.drawCommands) state.failed = true;
        }
        if (state.failed) Lit::Log::Error("Cluster LOD buffers could not be allocated; falling back to per-object LOD.");
    }
}

void Renderer::bindMeshletPhase(int phaseIndex) {
    auto& gpu = *m_diligent;
    auto& state = *Meshlets(m_meshlets);
    auto& phase = state.phases[phaseIndex];
    auto& visibility = gpu.visibility[phaseIndex];
    const int frame = static_cast<int>(m_currentFrame);
    const std::array<const void*, 8> keys = {visibility.sortedEntries.RawPtr(), visibility.visibleCounter.RawPtr(), visibility.bucketOffsets.RawPtr(), gpu.pRenderableBuffer[frame].RawPtr(), gpu.pWorldMatrixBuffer[frame].RawPtr(), gpu.pSceneUBO[frame].RawPtr(), state.clusterBuffer.RawPtr(), gpu.visibility[kPhaseOpaque].sortedEntries.RawPtr()};
    if (keys == phase.keys[frame] && phase.cullSRB[frame]) return;
    phase.keys[frame] = keys;
    phase.instancesSRB[frame] = litgpu::makeBinding(state.instancesPSO, {
                                                                              {"SceneData",                 gpu.pSceneUBO[frame]           },
                                                                              {"MeshletConstants",          phase.constants                },
                                                                              {"AtomicCounterBuffer",       visibility.visibleCounter      },
                                                                              {"SortedVisibleObjectBuffer", visibility.sortedEntries       },
                                                                              {"BucketOffsetBuffer",        visibility.bucketOffsets       },
                                                                              {"RenderableBuffer",          gpu.pRenderableBuffer[frame]   },
                                                                              {"WorldMatrixBuffer",         gpu.pWorldMatrixBuffer[frame]  },
                                                                              {"ClusterMeshBuffer",         state.meshBuffer               },
                                                                              {"ClusterChunkBuffer",        state.levelBuffer              },
                                                                              {"WorkCounterBuffer",         phase.workCounter              },
                                                                              {"WorkItemBuffer",            phase.workItems                },
                                                                              {"MeshletStatsBuffer",        state.statsBuffer              }
    });
    phase.argsSRB[frame] = litgpu::makeBinding(state.argsPSO, {
                                                                  {"SceneData",          gpu.pSceneUBO[frame]},
                                                                  {"MeshletConstants",   phase.constants     },
                                                                  {"WorkCounterBuffer",  phase.workCounter   },
                                                                  {"DispatchArgsBuffer", phase.cullArgs      }
    });
    phase.cullSRB[frame] = litgpu::makeBinding(state.cullPSO, {
                                                                  {"SceneData",                 gpu.pSceneUBO[frame]         },
                                                                  {"MeshletConstants",          phase.constants              },
                                                                  {"SortedVisibleObjectBuffer", visibility.sortedEntries     },
                                                                  {"WorldMatrixBuffer",         gpu.pWorldMatrixBuffer[frame]},
                                                                  {"ClusterBuffer",             state.clusterBuffer          },
                                                                  {"ClusterVertexBuffer",       state.vertexBuffer           },
                                                                  {"ClusterTriangleBuffer",     state.triangleBuffer         },
                                                                  {"ClusterMeshBuffer",         state.meshBuffer             },
                                                                  {"WorkCounterBuffer",         phase.workCounter            },
                                                                  {"WorkItemBuffer",            phase.workItems              },
                                                                  {"IndexCounterBuffer",        phase.indexCounter           },
                                                                  {"ExpandedIndexBuffer",       phase.indices                },
                                                                  {"MeshletDrawCounterBuffer",  phase.drawCounters           },
                                                                  {"MeshletDrawCommandBuffer",  phase.drawCommands           },
                                                                  {"RetestCounterBuffer",       state.retestCounter          },
                                                                  {"RetestBuffer",              state.retestItems            },
                                                                  {"MeshletStatsBuffer",        state.statsBuffer            }
    });
    if (phaseIndex == kPhaseRetest) {
        phase.retestSRB[frame] = litgpu::makeBinding(state.retestPSO, {
                                                                          {"SceneData",                 gpu.pSceneUBO[frame]     },
                                                                          {"MeshletConstants",          phase.constants          },
                                                                          {"AtomicCounterBuffer",       visibility.visibleCounter},
                                                                          {"SortedVisibleObjectBuffer", visibility.sortedEntries },
                                                                          {"RetestCounterBuffer",       state.retestCounter      },
                                                                          {"RetestBuffer",              state.retestItems        },
                                                                          {"WorkCounterBuffer",         phase.workCounter        },
                                                                          {"WorkItemBuffer",            phase.workItems          },
                                                                          {"MeshletStatsBuffer",        state.statsBuffer        }
        });
    }
}

void Renderer::rollMeshletStats() {
    auto& gpu = *m_diligent;
    auto& state = *Meshlets(m_meshlets);
    if (state.statsFrame == m_frameCounter) return;
    auto* context = gpu.pImmediateContext.RawPtr();
    const int slot = static_cast<int>(m_frameCounter % kStagingFrames);
    if (state.stagingValid[slot] && state.staging[slot]) {
        Diligent::MapHelper<uint32_t> mapped(context, state.staging[slot], Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
        if (mapped) {
            const uint32_t* words = mapped;
            state.lastStats.camera = ReadViewStats(words);
            state.lastStats.shadow = ReadViewStats(words + kMeshletStatsPerView);
            state.lastStats.frame = state.stagingFrame[slot];
        }
    }
    if (state.statsFrame != 0 && state.staging[slot]) {
        context->CopyBuffer(state.statsBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, state.staging[slot], 0, kMeshletStatsWords * sizeof(uint32_t), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        state.stagingValid[slot] = true;
        state.stagingFrame[slot] = state.statsFrame;
    }
    const std::array<uint32_t, kMeshletStatsWords> zeros{};
    context->UpdateBuffer(state.statsBuffer, 0, sizeof(zeros), zeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    state.statsFrame = m_frameCounter;
}

void Renderer::buildMeshletDraws(int phaseIndex, uint32_t views) {
    if (!m_meshlets || phaseIndex < 0 || phaseIndex >= kPhaseCount) return;
    auto& state = *Meshlets(m_meshlets);
    auto& phase = state.phases[phaseIndex];
    phase.built = false;
    if (!meshletsActive()) return;
    ensureMeshletBuffers();
    if (state.failed) return;

    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    auto& visibility = gpu.visibility[phaseIndex];
    rollMeshletStats();
    bindMeshletPhase(phaseIndex);
    const int frame = static_cast<int>(m_currentFrame);

    const auto shaderCount = static_cast<uint32_t>(std::min(m_numDrawingShaders, kMaxSurfaceShaders));
    phase.slots = std::max(1u, shaderCount * std::max(views, 1u));
    if (phase.slots > PhaseSlotCapacity(phaseIndex)) return;
    phase.perSlot = kMeshletDrawBudget[static_cast<size_t>(phaseIndex)] / phase.slots;

    uint32_t twoSided = 0;
    for (size_t shader = 0; shader < gpu.surfaces.size() && shader < 32; ++shader) {
        if (gpu.surfaces[shader].twoSided) twoSided |= 1u << shader;
    }
    const RenderSettings& settings = m_lighting.settings();
    MeshletConstants constants{};
    constants.maxDraws = static_cast<uint32_t>(m_maxObjects);
    constants.views = std::max(views, 1u);
    constants.viewBuckets = ActiveBucketCount(m_numDrawingShaders);
    constants.shaderCount = shaderCount;
    constants.flags = phaseIndex == kPhaseShadow ? 0u : (kFlagFrustum | kFlagHiZ | kFlagCone | (phaseIndex == kPhaseOpaque ? kFlagRetestOutput : 0u));
    constants.maxWorkItems = kMeshletMaxWorkItems;
    constants.indexCapacity = kMeshletIndexCapacity[static_cast<size_t>(phaseIndex)];
    constants.drawsPerSlot = phase.perSlot;
    // Shadow views reuse the camera's cut metric (no shadow-space error yet) with a coarser threshold:
    // shadow maps resolve less detail than the G-buffer and the cascades cover objects far outside the view.
    const float shadowScale = phaseIndex == kPhaseShadow ? kMeshletShadowErrorScale : 1.0f;
    constants.errorThreshold = shadowScale * std::max(settings.clusterErrorPixels, 0.05f) / std::max(m_lodBias, 0.01f);
    constants.twoSidedMask = twoSided;
    constants.statsBase = phaseIndex == kPhaseShadow ? kMeshletShadowStatsBase : 0u;
    constants.retestCapacity = kMeshletRetestCapacity;
    context->UpdateBuffer(phase.constants, 0, sizeof(constants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ClearCounter(context, phase.workCounter);
    ClearCounter(context, phase.indexCounter);
    context->UpdateBuffer(phase.drawCounters, 0, sizeof(uint32_t) * phase.slots, m_bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (phaseIndex == kPhaseOpaque) ClearCounter(context, state.retestCounter);

    if (phaseIndex == kPhaseRetest && phase.retestSRB[frame]) {
        context->SetPipelineState(state.retestPSO);
        context->CommitShaderResources(phase.retestSRB[frame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute(Diligent::DispatchComputeAttribs((kMeshletRetestCapacity + kMeshletGroupSize - 1) / kMeshletGroupSize, 1, 1));
    }

    context->SetPipelineState(state.instancesPSO);
    context->CommitShaderResources(phase.instancesSRB[frame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{visibility.scatterDispatchArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});

    DispatchSingleGroup(context, state.argsPSO, phase.argsSRB[frame]);

    Diligent::ITexture* hiz = phaseIndex != kPhaseShadow && m_meshletHiZ ? m_meshletHiZ : gpu.pHiZTextures[m_currentFrame].RawPtr();
    SetDynamicVariable(phase.cullSRB[frame], "u_hizTexture", hiz->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
    context->SetPipelineState(state.cullPSO);
    context->CommitShaderResources(phase.cullSRB[frame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{phase.cullArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    phase.built = true;
}

void Renderer::drawMeshletSurfaces(int phaseIndex, int cascade, uint32_t view) {
    if (!m_meshlets || phaseIndex < 0 || phaseIndex >= kPhaseCount) return;
    auto& phase = Meshlets(m_meshlets)->phases[phaseIndex];
    if (!phase.built || phase.perSlot == 0) return;
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const auto& bindings = cascade >= 0 ? gpu.shadowSRBs[m_currentFrame][cascade] : gpu.visibility[phaseIndex].opaqueSRBs[m_currentFrame];
    const auto shaderCount = static_cast<uint32_t>(std::min(m_numDrawingShaders, kMaxSurfaceShaders));
    context->SetIndexBuffer(phase.indices, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    for (uint32_t shaderId = 0; shaderId < gpu.surfaces.size() && shaderId < shaderCount; ++shaderId) {
        const uint32_t commandSlot = view * shaderCount + shaderId;
        if (commandSlot >= phase.slots) break;
        Diligent::IPipelineState* pipeline = SurfacePipeline(gpu.surfaces[shaderId], phaseIndex, false);
        if (!pipeline || shaderId >= bindings.size() || !bindings[shaderId]) continue;
        context->SetPipelineState(pipeline);
        context->CommitShaderResources(bindings[shaderId], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::DrawIndexedIndirectAttribs attribs;
        attribs.IndexType = Diligent::VT_UINT32;
        attribs.pAttribsBuffer = phase.drawCommands;
        attribs.DrawArgsOffset = static_cast<Diligent::Uint64>(commandSlot) * phase.perSlot * sizeof(DrawElementsIndirectCommand);
        attribs.DrawCount = phase.perSlot;
        attribs.DrawArgsStride = sizeof(DrawElementsIndirectCommand);
        attribs.pCounterBuffer = phase.drawCounters;
        attribs.CounterOffset = static_cast<Diligent::Uint64>(commandSlot) * sizeof(uint32_t);
        attribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        attribs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->DrawIndexedIndirect(attribs);
    }
    context->SetIndexBuffer(gpu.pEBO, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

ClusterRenderStats Renderer::clusterStats() const {
    ClusterRenderStats stats;
    if (!m_meshlets) return stats;
    stats = Meshlets(m_meshlets)->lastStats;
    stats.enabled = meshletsActive();
    stats.clusterMeshes = static_cast<uint32_t>(Meshlets(m_meshlets)->meshIds.size());
    stats.residentClusters = Meshlets(m_meshlets)->clusters.size();
    stats.residentTriangles = Meshlets(m_meshlets)->residentTriangles;
    return stats;
}

void Renderer::releaseMeshlets() { m_meshlets.reset(); }
