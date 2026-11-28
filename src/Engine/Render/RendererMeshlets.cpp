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

namespace {

using BufferPtr = Diligent::RefCntAutoPtr<Diligent::IBuffer>;
using PipelinePtr = Diligent::RefCntAutoPtr<Diligent::IPipelineState>;
using BindingPtr = Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>;

constexpr uint32_t kMeshletGroupSize = 64;
constexpr uint32_t kMeshletMaxWorkItems = 1u << 19;
constexpr std::array<uint32_t, 4> kMeshletDrawBudget = {1u << 21, 1u << 20, 1u << 18, 1u << 22};
constexpr uint32_t kMeshletRetestCapacity = 1u << 19;
constexpr uint32_t kMeshletStatsWords = 20;
constexpr uint32_t kMeshletStatsPerView = 10;
constexpr uint32_t kMeshletShadowStatsBase = 10;
constexpr uint32_t kMeshletNodeQueueCapacity = 1u << 21;
constexpr uint32_t kMeshletMaxDepth = 28;
constexpr uint32_t kMeshletNodeCounterWords = kMeshletMaxDepth + 4;
constexpr uint32_t kMeshletLodViews = kMaxLocalShadowViews;
constexpr uint32_t kMeshletLodSlots = kLodLevelCount - 1;
constexpr uint32_t kFlagFrustum = 1;
constexpr uint32_t kFlagHiZ = 2;
constexpr uint32_t kFlagCone = 4;
constexpr uint32_t kFlagRetestOutput = 8;
constexpr uint32_t kFlagViewLod = 16;
constexpr uint32_t kFlagHierarchy = 32;
constexpr uint32_t kFlagCasterCull = 64;
constexpr uint32_t kCasterPlaneCount = 12;
constexpr int kStagingFrames = 3;
constexpr float kMeshletShadowErrorScale = 4.0f;
constexpr uint64_t kMeshletOverflowWarnFrames = 600;

struct GpuClusterMesh {
    glm::vec4 lodCenter{0.0f};
    uint32_t info[4] = {0, 0, 0, 0};
};
static_assert(sizeof(GpuClusterMesh) == 32);

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
    uint32_t queueCapacity;
    uint32_t orthoMask;
    uint32_t casterExemptMask;
    uint32_t pad;
    glm::vec4 lodViews[kMeshletLodViews];
    glm::vec4 casterPlanes[kCasterPlaneCount];
    glm::vec4 casterDirection;
};
static_assert(sizeof(MeshletConstants) == 64 + 16 * (kMeshletLodViews + kCasterPlaneCount + 1), "MeshletConstants must match meshlet.glsl (std140)");

uint32_t PhaseSlotCapacity(int phaseIndex) { return static_cast<uint32_t>(kMaxSurfaceShaders) * (phaseIndex == kPhaseShadow ? static_cast<uint32_t>(kMaxLocalShadowViews) : 1u); }

BufferPtr Structured(Diligent::IRenderDevice* device, const char* name, uint32_t stride, size_t count, const void* data = nullptr, Diligent::BIND_FLAGS extra = Diligent::BIND_NONE) {
    return CreateStructuredBuffer(device, name, stride, static_cast<Diligent::Uint32>(std::max<size_t>(count, 1)), const_cast<void*>(data), extra);
}

struct ClusterSpan {
    uint32_t clusterBase = 0;
    uint32_t clusterCount = 0;
    uint32_t triangleBase = 0;
    uint32_t triangleCount = 0;
    uint32_t chunkBase = 0;
    uint32_t chunkCount = 0;
    uint32_t nodeBase = 0;
    uint32_t nodeCount = 0;
    uint64_t sourceTriangles = 0;
};

template <typename T>
void EraseRange(std::vector<T>& values, size_t first, size_t count) {
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(first), values.begin() + static_cast<std::ptrdiff_t>(first + count));
}

ClusterViewStats ReadViewStats(const uint32_t* words) {
    ClusterViewStats stats;
    stats.instances = words[0];
    stats.workItems = words[1];
    stats.nodesTested = words[8];
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
        BufferPtr drawCounters;
        BufferPtr drawCommands;
        BufferPtr cullArgs;
        BindingPtr instancesSRB[DiligentData::NumFrames];
        BindingPtr cullSRB[DiligentData::NumFrames];
        BindingPtr argsSRB[DiligentData::NumFrames];
        BindingPtr retestSRB[DiligentData::NumFrames];
        BindingPtr nodesSRB[DiligentData::NumFrames];
        BindingPtr nodeArgsSRB[DiligentData::NumFrames];
        std::array<const void*, 8> keys[DiligentData::NumFrames]{};
        uint32_t slots = 0;
        uint32_t perSlot = 0;
        bool built = false;
    };

    std::vector<MeshCluster> clusters;
    std::vector<uint32_t> indices;
    std::vector<GpuClusterChunk> chunks;
    std::vector<ClusterNode> nodes;
    uint32_t maxDepth = 0;
    std::vector<GpuClusterMesh> meshes = std::vector<GpuClusterMesh>(kMaxMeshes);
    std::vector<uint32_t> meshIds;
    std::vector<ClusterSpan> spans = std::vector<ClusterSpan>(kMaxMeshes);
    uint64_t nextOverflowWarning = 0;
    uint64_t residentTriangles = 0;
    bool geometryDirty = true;
    bool patchedMeshInfo = false;
    bool failed = false;

    BufferPtr clusterBuffer;
    BufferPtr indexBuffer;
    BufferPtr meshBuffer;
    BufferPtr levelBuffer;
    BufferPtr statsBuffer;
    BufferPtr retestCounter;
    BufferPtr retestItems;
    BufferPtr nodeBuffer;
    BufferPtr nodeQueue;
    BufferPtr nodeCounters;
    BufferPtr nodeArgs;
    BufferPtr staging[kStagingFrames];
    bool stagingValid[kStagingFrames] = {};
    uint64_t stagingFrame[kStagingFrames] = {};
    uint64_t statsFrame = 0;
    ClusterRenderStats lastStats;

    PipelinePtr instancesPSO;
    PipelinePtr cullPSO;
    PipelinePtr retestPSO;
    PipelinePtr argsPSO;
    PipelinePtr nodesPSO;
    PipelinePtr nodeArgsPSO;
    bool hierarchyFailed = false;
    Phase phases[kPhaseCount];

    std::array<glm::vec4, kMeshletLodViews> shadowLodViews{};
    uint32_t shadowOrthoMask = 0;
    bool shadowLodValid = false;
    std::array<glm::vec4, kCasterPlaneCount> casterPlanes{};
    glm::vec4 casterDirection{0.0f};
    bool casterCullValid = false;
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
    state.nodesPSO = CreateComputePipeline(device, "Meshlet Nodes PSO", "resources/shaders/meshlet_nodes.comp", {"u_hizTexture"});
    state.nodeArgsPSO = CreateComputePipeline(device, "Meshlet Node Args PSO", "resources/shaders/meshlet_node_args.comp");
    state.failed = !state.instancesPSO || !state.cullPSO || !state.retestPSO || !state.argsPSO;
    state.hierarchyFailed = !state.nodesPSO || !state.nodeArgsPSO;
    if (state.failed) Lit::Log::Error("Cluster LOD pipelines failed to build; falling back to per-object LOD.");
    else if (state.hierarchyFailed) Lit::Log::Error("Cluster hierarchy pipelines failed to build; using the flat cluster chunk path.");
    for (auto& phase : state.phases) {
        for (auto& keys : phase.keys) keys.fill(nullptr);
    }
}

void Renderer::uploadClusterMesh(uint32_t meshId, const ClusterMesh& source) {
    if (meshId >= gMeshInfos.size() || meshId >= kMaxMeshes || source.empty()) return;
    if (!m_meshlets) m_meshlets = std::make_shared<MeshletState>();
    auto& state = *Meshlets(m_meshlets);
    if (state.meshes[meshId].info[1] != 0) return;

    ClusterMesh ordered = source;
    const ClusterHierarchy hierarchy = buildClusterHierarchy(ordered);
    if (hierarchy.depth > kMeshletMaxDepth) Lit::Log::Error("Cluster hierarchy of mesh {} is {} levels deep (max {}); deeper leaves are not drawn", meshId, hierarchy.depth, kMeshletMaxDepth);

    const auto clusterBase = static_cast<uint32_t>(state.clusters.size());
    const auto chunkBase = static_cast<uint32_t>(state.chunks.size());
    const auto nodeBase = static_cast<uint32_t>(state.nodes.size());
    const auto triangleBase = static_cast<uint32_t>(state.indices.size() / 3);
    for (ClusterNode node : hierarchy.nodes) {
        node.first += node.leaf() ? clusterBase : nodeBase;
        state.nodes.push_back(node);
    }
    state.maxDepth = std::max(state.maxDepth, std::min(hierarchy.depth, kMeshletMaxDepth));
    for (MeshCluster cluster : ordered.clusters) {
        const uint32_t first = static_cast<uint32_t>(state.indices.size() / 3);
        for (uint32_t t = 0; t < cluster.triangleCount; ++t) {
            const uint32_t packed = ordered.triangles[cluster.triangleOffset + t];
            for (uint32_t corner = 0; corner < 3; ++corner) state.indices.push_back(ordered.vertices[cluster.vertexOffset + ((packed >> (corner * 8)) & 0xFFu)]);
        }
        cluster.vertexOffset = 0;
        cluster.triangleOffset = first;
        state.clusters.push_back(cluster);
    }
    for (const ClusterLevel& level : ordered.levels) {
        for (uint32_t first = level.firstCluster; first < level.firstCluster + level.clusterCount; first += kMeshletGroupSize) {
            const uint32_t count = std::min(kMeshletGroupSize, level.firstCluster + level.clusterCount - first);
            glm::vec3 center(0.0f);
            for (uint32_t id = first; id < first + count; ++id) center += glm::vec3(ordered.clusters[id].boundsCenter[0], ordered.clusters[id].boundsCenter[1], ordered.clusters[id].boundsCenter[2]);
            center /= static_cast<float>(count);
            GpuClusterChunk chunk;
            chunk.minSelfError = kClusterRootError;
            chunk.firstCluster = clusterBase + first;
            chunk.clusterCount = count;
            float radius = 0.0f;
            for (uint32_t id = first; id < first + count; ++id) {
                const MeshCluster& cluster = ordered.clusters[id];
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
    mesh.info[3] = nodeBase + hierarchy.root;
    state.meshIds.push_back(meshId);
    state.spans[meshId] = {clusterBase, static_cast<uint32_t>(state.clusters.size()) - clusterBase, triangleBase, static_cast<uint32_t>(state.indices.size() / 3) - triangleBase, chunkBase, mesh.info[1], nodeBase, static_cast<uint32_t>(state.nodes.size()) - nodeBase, source.triangleCount(0)};
    state.residentTriangles += source.triangleCount(0);
    state.geometryDirty = true;
    m_meshInfoDirty = true;
    Lit::Log::Info("Cluster LOD mesh {}: {} clusters, {} levels, {} -> {} triangles; hierarchy {} nodes, {} leaves, depth {}", meshId, source.clusters.size(), source.levels.size(), source.triangleCount(0), source.triangleCount(static_cast<uint32_t>(source.levels.size() - 1)), hierarchy.nodes.size(), hierarchy.leaves,
                   hierarchy.depth);
}

void Renderer::releaseClusterMesh(uint32_t meshId) {
    if (!m_meshlets || meshId >= kMaxMeshes) return;
    auto& state = *Meshlets(m_meshlets);
    if (state.meshes[meshId].info[1] == 0) return;
    const ClusterSpan span = state.spans[meshId];
    state.spans[meshId] = ClusterSpan{};
    state.meshes[meshId] = GpuClusterMesh{};
    state.meshIds.erase(std::remove(state.meshIds.begin(), state.meshIds.end(), meshId), state.meshIds.end());
    EraseRange(state.clusters, span.clusterBase, span.clusterCount);
    EraseRange(state.indices, static_cast<size_t>(span.triangleBase) * 3, static_cast<size_t>(span.triangleCount) * 3);
    EraseRange(state.chunks, span.chunkBase, span.chunkCount);
    EraseRange(state.nodes, span.nodeBase, span.nodeCount);
    for (size_t id = span.clusterBase; id < state.clusters.size(); ++id) state.clusters[id].triangleOffset -= span.triangleCount;
    for (size_t id = span.chunkBase; id < state.chunks.size(); ++id) state.chunks[id].firstCluster -= span.clusterCount;
    for (size_t id = span.nodeBase; id < state.nodes.size(); ++id) state.nodes[id].first -= state.nodes[id].leaf() ? span.clusterCount : span.nodeCount;
    for (const uint32_t other : state.meshIds) {
        ClusterSpan& later = state.spans[other];
        if (later.clusterBase < span.clusterBase) continue;
        later.clusterBase -= span.clusterCount;
        later.triangleBase -= span.triangleCount;
        later.chunkBase -= span.chunkCount;
        later.nodeBase -= span.nodeCount;
        state.meshes[other].info[0] -= span.chunkCount;
        state.meshes[other].info[3] -= span.nodeCount;
    }
    state.residentTriangles -= span.sourceTriangles;
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
    LIT_UPDATE_DIFF(m_diligent->pImmediateContext, m_diligent->pMeshInfoBuffer, 0, count * sizeof(MeshInfo), infos.data());
    return true;
}

void Renderer::ensureMeshletBuffers() {
    auto& gpu = *m_diligent;
    auto& state = *Meshlets(m_meshlets);
    auto* device = gpu.pDevice.RawPtr();
    if (state.geometryDirty) {
        state.clusterBuffer = Structured(device, "Cluster Buffer", sizeof(MeshCluster), state.clusters.size(), state.clusters.data());
        state.indexBuffer = Structured(device, "Cluster Index Buffer", sizeof(uint32_t), state.indices.size(), state.indices.data(), Diligent::BIND_INDEX_BUFFER);
        state.levelBuffer = Structured(device, "Cluster Chunk Buffer", sizeof(GpuClusterChunk), state.chunks.size(), state.chunks.data());
        state.meshBuffer = Structured(device, "Cluster Mesh Buffer", sizeof(GpuClusterMesh), state.meshes.size(), state.meshes.data());
        state.nodeBuffer = Structured(device, "Cluster Node Buffer", sizeof(ClusterNode), state.nodes.size(), state.nodes.data());
        if (!state.clusterBuffer || !state.indexBuffer || !state.levelBuffer || !state.meshBuffer || !state.nodeBuffer) {
            state.failed = true;
            Lit::Log::Error("Cluster LOD geometry buffers could not be allocated; falling back to per-object LOD.");
            return;
        }
        state.geometryDirty = false;
        for (auto& phase : state.phases) {
            for (auto& keys : phase.keys) keys.fill(nullptr);
        }
    }
    if (!state.statsBuffer) {
        const std::vector<uint32_t> zeros(1024, 0u);
        state.statsBuffer = Structured(device, "Meshlet Stats", sizeof(uint32_t), kMeshletStatsWords, zeros.data());
        state.retestCounter = Structured(device, "Meshlet Retest Counter", sizeof(uint32_t), 1, zeros.data());
        state.retestItems = Structured(device, "Meshlet Retest Items", sizeof(uint32_t) * 4, static_cast<size_t>(kMeshletRetestCapacity) * 2);
        state.nodeQueue = Structured(device, "Meshlet Node Queue", sizeof(uint32_t) * 4, static_cast<size_t>(kMeshletNodeQueueCapacity) * 2);
        state.nodeCounters = Structured(device, "Meshlet Node Counters", sizeof(uint32_t), kMeshletNodeCounterWords, zeros.data());
        state.nodeArgs = CreateDispatchArgsBuffer(device, "Meshlet Node Args");
        if (!state.nodeQueue || !state.nodeCounters || !state.nodeArgs) state.hierarchyFailed = true;
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
            phase.drawCounters = Structured(device, "Meshlet Draw Counters", sizeof(uint32_t), slotCapacity, zeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
            phase.drawCommands = CreateIndirectBuffer(device, "Meshlet Draw Commands", static_cast<size_t>(kMeshletDrawBudget[static_cast<size_t>(phaseIndex)]) * sizeof(DrawElementsIndirectCommand));
            phase.cullArgs = CreateDispatchArgsBuffer(device, "Meshlet Cull Args");
            if (!phase.workItems || !phase.drawCommands) state.failed = true;
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
                                                                              {"InstanceBuffer",            gpu.pInstanceBuffer            },
                                                                              {"InstanceCellBuffer",        gpu.pInstanceCellBuffer        },
                                                                              {"InstanceBatchBuffer",       gpu.pInstanceBatchBuffer       },
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
                                                                              {"NodeCounterBuffer",         state.nodeCounters             },
                                                                              {"NodeQueueBuffer",           state.nodeQueue                },
                                                                              {"MeshletStatsBuffer",        state.statsBuffer              }
    });
    if (!state.hierarchyFailed) {
        phase.nodesSRB[frame] = litgpu::makeBinding(state.nodesPSO, {
                                                                              {"InstanceBuffer",            gpu.pInstanceBuffer            },
                                                                              {"InstanceCellBuffer",        gpu.pInstanceCellBuffer        },
                                                                              {"InstanceBatchBuffer",       gpu.pInstanceBatchBuffer       },
                                                                        {"SceneData",                 gpu.pSceneUBO[frame]         },
                                                                        {"MeshletConstants",          phase.constants              },
                                                                        {"SortedVisibleObjectBuffer", visibility.sortedEntries     },
                                                                        {"WorldMatrixBuffer",         gpu.pWorldMatrixBuffer[frame]},
                                                                        {"ClusterNodeBuffer",         state.nodeBuffer             },
                                                                        {"NodeCounterBuffer",         state.nodeCounters           },
                                                                        {"NodeQueueBuffer",           state.nodeQueue              },
                                                                        {"WorkCounterBuffer",         phase.workCounter            },
                                                                        {"WorkItemBuffer",            phase.workItems              },
                                                                        {"RetestCounterBuffer",       state.retestCounter          },
                                                                        {"RetestBuffer",              state.retestItems            },
                                                                        {"MeshletStatsBuffer",        state.statsBuffer            }
        });
        phase.nodeArgsSRB[frame] = litgpu::makeBinding(state.nodeArgsPSO, {
                                                                              {"SceneData",          gpu.pSceneUBO[frame]},
                                                                              {"MeshletConstants",   phase.constants     },
                                                                              {"NodeCounterBuffer",  state.nodeCounters  },
                                                                              {"DispatchArgsBuffer", state.nodeArgs      }
        });
    }
    phase.argsSRB[frame] = litgpu::makeBinding(state.argsPSO, {
                                                                  {"SceneData",          gpu.pSceneUBO[frame]},
                                                                  {"MeshletConstants",   phase.constants     },
                                                                  {"WorkCounterBuffer",  phase.workCounter   },
                                                                  {"DispatchArgsBuffer", phase.cullArgs      }
    });
    phase.cullSRB[frame] = litgpu::makeBinding(state.cullPSO, {
                                                                              {"InstanceBuffer",            gpu.pInstanceBuffer            },
                                                                              {"InstanceCellBuffer",        gpu.pInstanceCellBuffer        },
                                                                              {"InstanceBatchBuffer",       gpu.pInstanceBatchBuffer       },
                                                                  {"SceneData",                 gpu.pSceneUBO[frame]         },
                                                                  {"MeshletConstants",          phase.constants              },
                                                                  {"SortedVisibleObjectBuffer", visibility.sortedEntries     },
                                                                  {"WorldMatrixBuffer",         gpu.pWorldMatrixBuffer[frame]},
                                                                  {"ClusterBuffer",             state.clusterBuffer          },
                                                                  {"ClusterMeshBuffer",         state.meshBuffer             },
                                                                  {"WorkCounterBuffer",         phase.workCounter            },
                                                                  {"WorkItemBuffer",            phase.workItems              },
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
                                                                          {"NodeCounterBuffer",         state.nodeCounters       },
                                                                          {"NodeQueueBuffer",           state.nodeQueue          },
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
            const uint64_t cameraDropped = state.lastStats.camera.overflows;
            const uint64_t shadowDropped = state.lastStats.shadow.overflows;
            if (cameraDropped + shadowDropped != 0 && m_frameCounter >= state.nextOverflowWarning) {
                state.nextOverflowWarning = m_frameCounter + kMeshletOverflowWarnFrames;
                Lit::Log::Warn("Cluster LOD capacity exceeded (camera {}, shadow {} overflows); some geometry is not drawn. Raise clusterErrorPixels or clusterShadowErrorTexels.", cameraDropped, shadowDropped);
            }
        }
    }
    if (state.statsFrame != 0 && state.staging[slot]) {
        context->CopyBuffer(state.statsBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, state.staging[slot], 0, kMeshletStatsWords * sizeof(uint32_t), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        state.stagingValid[slot] = true;
        state.stagingFrame[slot] = state.statsFrame;
    }
    const std::array<uint32_t, kMeshletStatsWords> zeros{};
    LIT_UP(context)->UpdateBuffer(state.statsBuffer, 0, sizeof(zeros), zeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
    phase.perSlot = std::min(kMeshletDrawBudget[static_cast<size_t>(phaseIndex)] / phase.slots, gpu.pDevice->GetAdapterInfo().DrawCommand.MaxDrawIndirectCount);

    uint32_t twoSided = 0;
    uint32_t deforming = 0;
    for (size_t shader = 0; shader < gpu.surfaces.size() && shader < 32; ++shader) {
        if (gpu.surfaces[shader].twoSided) twoSided |= 1u << shader;
        if (gpu.surfaces[shader].deformsVertices) deforming |= 1u << shader;
    }
    const RenderSettings& settings = m_lighting.settings();
    const bool hierarchy = settings.clusterHierarchy && !state.hierarchyFailed && phase.nodesSRB[frame] && state.maxDepth > 0;
    const bool shadowTexels = phaseIndex == kPhaseShadow && settings.clusterShadowTexelLod && state.shadowLodValid;
    MeshletConstants constants{};
    constants.maxDraws = static_cast<uint32_t>(VisibleEntryCapacity(m_maxObjects));
    constants.views = std::max(views, 1u);
    constants.viewBuckets = ActiveBucketCount(m_numDrawingShaders);
    constants.shaderCount = shaderCount;
    constants.flags = phaseIndex == kPhaseShadow ? 0u : (kFlagFrustum | kFlagHiZ | kFlagCone | (phaseIndex == kPhaseOpaque ? kFlagRetestOutput : 0u));
    if (hierarchy) constants.flags |= kFlagHierarchy;
    if (shadowTexels) constants.flags |= kFlagViewLod;
    constants.maxWorkItems = kMeshletMaxWorkItems;
    constants.drawsPerSlot = phase.perSlot;
    const float shadowScale = phaseIndex == kPhaseShadow ? kMeshletShadowErrorScale : 1.0f;
    constants.errorThreshold = shadowTexels ? std::max(settings.clusterShadowErrorTexels, 0.05f) / std::max(m_lodBias, 0.01f) : shadowScale * std::max(settings.clusterErrorPixels, 0.05f) / std::max(m_lodBias, 0.01f);
    constants.twoSidedMask = twoSided;
    constants.statsBase = phaseIndex == kPhaseShadow ? kMeshletShadowStatsBase : 0u;
    constants.retestCapacity = kMeshletRetestCapacity;
    constants.queueCapacity = kMeshletNodeQueueCapacity;
    if (shadowTexels) {
        constants.orthoMask = state.shadowOrthoMask;
        std::copy(state.shadowLodViews.begin(), state.shadowLodViews.end(), constants.lodViews);
    }
    if (phaseIndex == kPhaseShadow && state.casterCullValid) {
        constants.flags |= kFlagCasterCull;
        constants.casterExemptMask = deforming;
        std::copy(state.casterPlanes.begin(), state.casterPlanes.end(), constants.casterPlanes);
        constants.casterDirection = state.casterDirection;
    }
    LIT_UP(context)->UpdateBuffer(phase.constants, 0, sizeof(constants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (hierarchy) {
        std::array<uint32_t, kMeshletNodeCounterWords> counters{};
        counters[0] = 0xFFFFFFFFu;
        LIT_UP(context)->UpdateBuffer(state.nodeCounters, 0, sizeof(counters), counters.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    ClearCounter(context, phase.workCounter);
    litgpu::zeroFill(context, m_diligent->pZeroBuffer, phase.drawCounters, 0, sizeof(uint32_t) * phase.slots, m_bucketZeros.data());
    if (phaseIndex == kPhaseOpaque) ClearCounter(context, state.retestCounter);

    if (phaseIndex == kPhaseRetest && phase.retestSRB[frame]) {
        context->SetPipelineState(state.retestPSO);
        context->CommitShaderResources(phase.retestSRB[frame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute(Diligent::DispatchComputeAttribs((kMeshletRetestCapacity + kMeshletGroupSize - 1) / kMeshletGroupSize, 1, 1));
    }

    context->SetPipelineState(state.instancesPSO);
    context->CommitShaderResources(phase.instancesSRB[frame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{visibility.scatterDispatchArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});

    Diligent::ITexture* hiz = phaseIndex != kPhaseShadow && m_meshletHiZ ? m_meshletHiZ : gpu.pHiZTextures[m_hiZSlot].RawPtr();
    if (hierarchy) {
        SetDynamicVariable(phase.nodesSRB[frame], "u_hizTexture", hiz->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
        for (uint32_t level = 0; level < state.maxDepth; ++level) {
            DispatchSingleGroup(context, state.nodeArgsPSO, phase.nodeArgsSRB[frame]);
            context->SetPipelineState(state.nodesPSO);
            context->CommitShaderResources(phase.nodesSRB[frame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs{state.nodeArgs, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        }
    }

    DispatchSingleGroup(context, state.argsPSO, phase.argsSRB[frame]);

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
    context->SetIndexBuffer(Meshlets(m_meshlets)->indexBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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

void Renderer::setMeshletShadowLod(std::span<const glm::vec4> views, uint32_t orthoMask) {
    if (!m_meshlets) return;
    auto& state = *Meshlets(m_meshlets);
    state.shadowLodViews.fill(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    std::copy_n(views.begin(), std::min<size_t>(views.size(), kMeshletLodViews), state.shadowLodViews.begin());
    state.shadowOrthoMask = orthoMask;
    state.shadowLodValid = !views.empty();
    state.casterCullValid = false;
}

void Renderer::setMeshletCasterCull(std::span<const glm::vec4> casterPlanes, const glm::vec3& lightTravel) {
    if (!m_meshlets) return;
    auto& state = *Meshlets(m_meshlets);
    std::copy_n(casterPlanes.begin(), std::min<size_t>(casterPlanes.size(), kCasterPlaneCount), state.casterPlanes.begin());
    state.casterDirection = glm::vec4(lightTravel, 0.0f);
    state.casterCullValid = casterPlanes.size() == kCasterPlaneCount;
}

void Renderer::releaseMeshlets() { m_meshlets.reset(); }

void Renderer::invalidateMeshletBindings() {
    if (!m_meshlets) return;
    for (auto& phase : Meshlets(m_meshlets)->phases) {
        for (auto& keys : phase.keys) keys.fill(nullptr);
    }
}
