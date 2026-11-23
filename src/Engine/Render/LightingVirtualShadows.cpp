module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

module Engine.Render.lighting;

import Engine.glm;
import Engine.Render.settings;
import Engine.Render.material;

#include "LightingInternal.hpp"

// Virtual (paged) shadow maps for local lights. Overview (details in docs/Lighting.md):
//   CPU, once per frame: every point/spot light gets stable face slots (1 or 6); a light keeps its
//   slots while its parameters are unchanged (clean) or while it moves (matched by colour/range/type
//   near its old position, dirty). Faces are uploaded with their projection and dirty flag.
//   GPU, per rendered view after its depth pass: requests from this view's depth buffer -> release of
//   stale pages / invalidation of dirty faces -> allocation from the physical pool -> per-page caster
//   culling -> instanced draws into the atlas. Nothing reads a previous frame's image; the only state
//   kept across frames is the page cache, invalidated by schedule (moved lights, moved casters).

using namespace lighting_detail;

namespace {

constexpr uint32_t kInvalid = 0xFFFFFFFFu;
constexpr uint32_t kMaxPoolPages = (8192 / kVsmPage) * (8192 / kVsmPage);
constexpr uint32_t kGroup = 64;
constexpr uint32_t kTileGroup = 8;
constexpr float kFaceBorder = 1.02f;
constexpr float kIdentityCell = 4.0f;
constexpr float kMinSpotCosine = 0.0872f;
constexpr float kMaxSpotCosine = 0.9999f;
constexpr float kMaxSpotTanHalf = 11.4f;
constexpr int kCubeFaces = 6;
constexpr uint32_t kDisabledPool = 64;
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint32_t kStampOffset = 2;
constexpr uint32_t kStatsLag = kVsmReadbackRing - 1;

uint32_t groups(uint32_t count, uint32_t size) { return std::max((count + size - 1) / size, 1u); }

uint64_t hashFloats(std::initializer_list<float> values) {
    uint64_t hash = kFnvOffset;
    for (float value : values) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        hash = (hash ^ bits) * kFnvPrime;
    }
    return hash;
}

uint64_t cellKey(const glm::ivec3& cell) { return (static_cast<uint64_t>(static_cast<uint32_t>(cell.x)) * 73856093ull) ^ (static_cast<uint64_t>(static_cast<uint32_t>(cell.y)) * 19349663ull) ^ (static_cast<uint64_t>(static_cast<uint32_t>(cell.z)) * 83492791ull); }

glm::ivec3 cellOf(const glm::vec3& position) { return glm::ivec3(static_cast<int>(std::floor(position.x / kIdentityCell)), static_cast<int>(std::floor(position.y / kIdentityCell)), static_cast<int>(std::floor(position.z / kIdentityCell))); }

bool sphereVisible(const glm::vec4* planes, const glm::vec3& center, float radius) {
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) {
        if (glm::dot(glm::vec3(planes[planeIndex]), center) + planes[planeIndex].w < -radius) return false;
    }
    return true;
}

RefCntAutoPtr<Diligent::IBuffer> indirectBuffer(Diligent::IRenderDevice* device, const char* name, uint32_t stride, uint32_t count) {
    std::vector<uint8_t> zeros(static_cast<size_t>(stride) * count, 0);
    return litgpu::structuredBuffer(device, name, stride, count, zeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
}

RefCntAutoPtr<Diligent::IBuffer> zeroBuffer(Diligent::IRenderDevice* device, const char* name, uint32_t stride, uint32_t count) {
    std::vector<uint8_t> zeros(static_cast<size_t>(stride) * count, 0);
    return litgpu::structuredBuffer(device, name, stride, count, zeros.data());
}

int wantedPool(const Lighting::Data& state) { return virtualShadowsEnabled(state) ? state.settings.vsmPoolSize : static_cast<int>(kDisabledPool); }

void ensurePool(Lighting::Data& state) {
    VirtualShadowGpu& vsm = state.vsm;
    const int size = wantedPool(state);
    if (size == vsm.poolSize && vsm.atlas) return;
    Diligent::TextureDesc desc;
    desc.Name = "Virtual Shadow Atlas";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = static_cast<uint32_t>(size);
    desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
    vsm.atlas.Release();
    state.device->CreateTexture(desc, nullptr, &vsm.atlas);
    if (!vsm.atlas) {
        Lit::Log::Error("Failed to create the virtual shadow atlas ({}x{})", size, size);
        vsm.poolSize = 0;
        return;
    }
    if (vsm.compareSampler) litgpu::srv(vsm.atlas)->SetSampler(vsm.compareSampler);
    vsm.poolSize = size;
    vsm.poolSide = static_cast<uint32_t>(size) / kVsmPage;
    vsm.resetPending = true;
    for (auto& binding : vsm.bindings) binding.key.clear();
    ++state.bindingVersion;
}

// Empties the page cache: all page table entries, stamps and owners cleared, every physical page free.
void resetPool(Lighting::Data& state) {
    VirtualShadowGpu& vsm = state.vsm;
    Diligent::IDeviceContext* context = state.context;
    const uint32_t pool = vsm.poolSide * vsm.poolSide;
    std::vector<uint32_t> physical(static_cast<size_t>(kMaxPoolPages) * 2, kInvalid);
    for (uint32_t page = 0; page < pool; ++page) physical[pool + page] = page;
    context->UpdateBuffer(vsm.physical, 0, sizeof(uint32_t) * physical.size(), physical.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const std::vector<uint32_t> zeros(static_cast<size_t>(kVsmMaxFaces) * kVsmPagesPerFace, 0u);
    context->UpdateBuffer(vsm.pageTable, 0, sizeof(uint32_t) * zeros.size(), zeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->UpdateBuffer(vsm.stamps, 0, sizeof(uint32_t) * zeros.size(), zeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    std::array<uint32_t, kVsmCounterWords> counters{};
    counters[0] = pool;
    context->UpdateBuffer(vsm.counters, 0, sizeof(counters), counters.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    vsm.resetPending = false;
    vsm.allDirty = true;
}

void ensureObjectCapacity(Lighting::Data& state, uint32_t objectCount) {
    VirtualShadowGpu& vsm = state.vsm;
    if (objectCount <= vsm.objectCapacity && vsm.previousObjects) return;
    const uint32_t capacity = std::max(objectCount + objectCount / 2, 1024u);
    vsm.previousObjects = zeroBuffer(state.device, "VSM Previous Objects", sizeof(glm::vec4), capacity * 5);
    if (capacity > vsm.stampCapacity) {
        vsm.noStamps = zeroBuffer(state.device, "VSM Empty Deform Stamps", sizeof(uint32_t), capacity);
        vsm.stampCapacity = capacity;
    }
    vsm.objectCapacity = capacity;
    vsm.allDirty = true;
    for (auto& binding : vsm.bindings) binding.key.clear();
}

void writeFace(VsmFaceGpu& face, const glm::vec3& position, const glm::vec3& forward, const glm::vec3& upHint, float tanHalf, float range, uint32_t lightIndex, bool dirty, const glm::vec4& clipPlane) {
    face.clipPlane = clipPlane;
    const float nearZ = std::clamp(range * 0.004f, 0.02f, 0.5f);
    const glm::mat4 lightView = glm::lookAt(position, position + forward, upHint);
    const glm::mat4 projection = glm::perspectiveRH_ZO(2.0f * std::atan(tanHalf), 1.0f, nearZ, range);
    face.viewProjection = projection * lightView;
    face.info = glm::vec4(tanHalf, static_cast<float>(lightIndex), dirty ? 1.0f : 0.0f, 1.0f);
    face.sphere = glm::vec4(position, range);
}

void writeLightFaces(VirtualShadowGpu& vsm, const GpuLight& light, uint32_t lightIndex, uint32_t base, bool dirty, const glm::vec4& clipPlane) {
    const glm::vec3 position(light.positionRange);
    const float range = light.positionRange.w;
    if (light.colorType.w > 1.5f) {
        const glm::vec3 direction = glm::normalize(glm::vec3(light.directionCos));
        const float cosOuter = std::clamp(light.directionCos.w, kMinSpotCosine, kMaxSpotCosine);
        const float tanHalf = std::min(std::sqrt(1.0f - cosOuter * cosOuter) / cosOuter, kMaxSpotTanHalf) * kFaceBorder;
        const glm::vec3 upHint = std::abs(direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        writeFace(vsm.cpuFaces[base], position, direction, upHint, tanHalf, range, lightIndex, dirty, clipPlane);
        return;
    }
    const glm::vec3 axes[kCubeFaces] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const glm::vec3 ups[kCubeFaces] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    for (int face = 0; face < kCubeFaces; ++face) writeFace(vsm.cpuFaces[base + static_cast<uint32_t>(face)], position, axes[face], ups[face], kFaceBorder, range, lightIndex, dirty, clipPlane);
}

bool allocateSlots(VirtualShadowGpu& vsm, uint32_t count, uint32_t& base) {
    std::vector<uint32_t>& freeList = count == 1 ? vsm.freeSingle : vsm.freeCube;
    if (!freeList.empty()) {
        base = freeList.back();
        freeList.pop_back();
        return true;
    }
    if (vsm.highWater + count > kVsmMaxFaces) return false;
    base = vsm.highWater;
    vsm.highWater += count;
    return true;
}

}

namespace lighting_detail {

bool virtualShadowsEnabled(const Lighting::Data& state) { return state.settings.virtualShadows && state.settings.localShadows; }

void createVirtualShadowResources(Lighting::Data& state) {
    VirtualShadowGpu& vsm = state.vsm;
    Diligent::IRenderDevice* device = state.device;
    Diligent::SamplerDesc samplerDesc = litgpu::shadowCompare();
    device->CreateSampler(samplerDesc, &vsm.compareSampler);
    vsm.noStamps = zeroBuffer(device, "VSM Empty Deform Stamps", sizeof(uint32_t), 1024);
    vsm.stampCapacity = 1024;
    vsm.faces = zeroBuffer(device, "VSM Faces", sizeof(VsmFaceGpu), kVsmMaxFaces);
    vsm.pageTable = zeroBuffer(device, "VSM Page Table", sizeof(uint32_t), kVsmMaxFaces * kVsmPagesPerFace);
    vsm.stamps = zeroBuffer(device, "VSM Page Stamps", sizeof(uint32_t), kVsmMaxFaces * kVsmPagesPerFace);
    vsm.physical = zeroBuffer(device, "VSM Physical Pages", sizeof(uint32_t), kMaxPoolPages * 2);
    vsm.counters = zeroBuffer(device, "VSM Counters", sizeof(uint32_t), kVsmCounterWords);
    vsm.args = indirectBuffer(device, "VSM Indirect Args", sizeof(uint32_t), kVsmArgsWords);
    vsm.requests = litgpu::structuredBuffer(device, "VSM Requests", sizeof(uint32_t), kVsmRequestCapacity);
    vsm.renderList = litgpu::structuredBuffer(device, "VSM Render List", sizeof(uint32_t) * 2, kMaxPoolPages);
    vsm.faceDirty = zeroBuffer(device, "VSM Face Dirty", sizeof(uint32_t), kVsmMaxFaces);
    vsm.moved = litgpu::structuredBuffer(device, "VSM Moved Objects", sizeof(glm::vec4), kVsmMovedCapacity);
    vsm.instances = litgpu::structuredBuffer(device, "VSM Instances", sizeof(uint32_t) * 2, kVsmInstanceCapacity);
    vsm.sorted = litgpu::structuredBuffer(device, "VSM Sorted Instances", sizeof(uint32_t) * 2, kVsmInstanceCapacity);
    vsm.buckets = zeroBuffer(device, "VSM Buckets", sizeof(uint32_t), kVsmBuckets * 3);
    vsm.commands = indirectBuffer(device, "VSM Draw Commands", sizeof(uint32_t) * 5, kVsmBuckets);
    vsm.constants = litgpu::uniformBuffer(device, "VSM Constants", sizeof(VsmConstantsGpu));
    for (uint32_t ring = 0; ring < kVsmReadbackRing; ++ring) {
        Diligent::BufferDesc desc;
        desc.Name = "VSM Stats Readback";
        desc.Usage = Diligent::USAGE_STAGING;
        desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
        desc.Size = sizeof(uint32_t) * kVsmCounterWords;
        device->CreateBuffer(desc, nullptr, &vsm.readback[ring]);
        vsm.readbackFrame[ring] = 0;
    }
    vsm.cpuFaces.assign(kVsmMaxFaces, VsmFaceGpu{});
    ensurePool(state);
}

void loadVirtualShadowPipelines(Lighting::Data& state) {
    VirtualShadowGpu& vsm = state.vsm;
    Diligent::IRenderDevice* device = state.device;
    const char* pages = "resources/shaders/lighting/vsm_pages.comp";
    vsm.motionPso = litgpu::computePipeline(device, pages, {}, {{"MOTION", "1"}});
    vsm.faceDirtyPso = litgpu::computePipeline(device, pages, {}, {{"FACE_DIRTY", "1"}});
    vsm.clearDirtyPso = litgpu::computePipeline(device, pages, {}, {{"CLEAR_DIRTY", "1"}});
    vsm.releasePso = litgpu::computePipeline(device, pages, {}, {{"RELEASE", "1"}});
    vsm.histPso = litgpu::computePipeline(device, pages, {}, {{"HIST", "1"}});
    vsm.planPso = litgpu::computePipeline(device, pages, {}, {{"PLAN", "1"}});
    vsm.allocatePso = litgpu::computePipeline(device, pages, {}, {{"ALLOCATE", "1"}});
    vsm.finalizePso = litgpu::computePipeline(device, pages, {}, {{"FINALIZE", "1"}});
    vsm.requestPso = litgpu::computePipeline(device, "resources/shaders/lighting/vsm_request.comp", {{"u_depth", litgpu::pointClamp()}, {"u_gbuffer1", litgpu::pointClamp()}});
    vsm.cullPso = litgpu::computePipeline(device, "resources/shaders/lighting/vsm_cull.comp");
    vsm.prefixPso = litgpu::computePipeline(device, "resources/shaders/lighting/vsm_sort.comp", {}, {{"PREFIX", "1"}});
    vsm.scatterPso = litgpu::computePipeline(device, "resources/shaders/lighting/vsm_sort.comp");

    auto graphics = [&](const char* name, const char* path, bool caster) {
        RefCntAutoPtr<Diligent::IPipelineState> pso;
        RefCntAutoPtr<Diligent::IShader> vertexShader = litgpu::compileFile(device, path, Diligent::SHADER_TYPE_VERTEX);
        if (!vertexShader) return pso;
        Diligent::GraphicsPipelineStateCreateInfo createInfo;
        createInfo.PSODesc.Name = name;
        createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
        auto& pipeline = createInfo.GraphicsPipeline;
        pipeline.NumRenderTargets = 0;
        pipeline.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
        pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
        pipeline.RasterizerDesc.FrontCounterClockwise = true;
        pipeline.DepthStencilDesc.DepthEnable = true;
        pipeline.DepthStencilDesc.DepthWriteEnable = true;
        pipeline.DepthStencilDesc.DepthFunc = caster ? Diligent::COMPARISON_FUNC_LESS_EQUAL : Diligent::COMPARISON_FUNC_ALWAYS;
        const Diligent::LayoutElement layout[] = {
            {0, 0, 3, Diligent::VT_FLOAT32, false},
            {1, 0, 3, Diligent::VT_FLOAT32, false},
            {2, 0, 2, Diligent::VT_FLOAT32, false}
        };
        if (caster) {
            pipeline.InputLayout.LayoutElements = layout;
            pipeline.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layout));
        }
        createInfo.pVS = vertexShader;
        const litgpu::PipelineLayout resourceLayout = litgpu::makeLayout({vertexShader});
        resourceLayout.apply(createInfo.PSODesc.ResourceLayout);
        createInfo.pPSOCache = litgpu::pipelineCache();
        device->CreateGraphicsPipelineState(createInfo, &pso);
        if (!pso) Lit::Log::Error("Failed to create pipeline '{}'", name);
        return pso;
    };
    vsm.casterPso = graphics("VSM Caster PSO", "resources/shaders/lighting/vsm_caster.vert", true);
    vsm.clearPso = graphics("VSM Page Clear PSO", "resources/shaders/lighting/vsm_clear.vert", false);
    for (auto& binding : vsm.bindings) binding.key.clear();
}

void assignVirtualShadowFaces(Lighting::Data& state, SceneUniforms& uniforms) {
    VirtualShadowGpu& vsm = state.vsm;
    ensurePool(state);
    // Portal views call fitShadows again within the frame: the assignment (and the params.w encoding
    // already written into cpuLights) stays the one made for the main camera.
    if (vsm.assignedFrame == state.frame) return;
    vsm.assignedFrame = state.frame;
    for (uint32_t face = 0; face < vsm.highWater; ++face) vsm.cpuFaces[face].info.z = 0.0f;

    // Lights in the main frustum first (they are the ones that request pages), then the rest in the
    // collector's distance order so portal views still find shadows for nearby off-screen lights.
    std::vector<uint32_t> order;
    order.reserve(state.cpuLights.size());
    for (uint32_t index = 0; index < state.cpuLights.size(); ++index) {
        const GpuLight& light = state.cpuLights[index];
        if (light.positionRange.w <= 0.0f || glm::dot(glm::vec3(light.colorType), glm::vec3(1.0f)) <= 0.0f) continue;
        order.push_back(index);
    }
    std::stable_partition(order.begin(), order.end(), [&](uint32_t index) { return sphereVisible(uniforms.frustumPlanes, glm::vec3(state.cpuLights[index].positionRange), state.cpuLights[index].positionRange.w); });

    std::unordered_map<uint64_t, std::vector<uint32_t>> exact;
    std::unordered_map<uint64_t, std::vector<uint32_t>> identity;
    for (uint32_t slot = 0; slot < vsm.slots.size(); ++slot) {
        VsmLightSlot& previous = vsm.slots[slot];
        previous.matched = false;
        exact[previous.exactKey].push_back(slot);
        identity[previous.identityKey ^ cellKey(cellOf(previous.position))].push_back(slot);
    }
    std::vector<VsmLightSlot> next;
    next.reserve(order.size());
    uint32_t visibleLights = 0;
    struct Ranked {
        float score;
        uint32_t index;
        uint32_t slot;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(order.size());
    for (uint32_t index : order) {
        GpuLight& light = state.cpuLights[index];
        const bool spot = light.colorType.w > 1.5f;
        const uint32_t faceCount = spot ? 1u : static_cast<uint32_t>(kCubeFaces);
        const glm::vec3 position(light.positionRange);
        VsmLightSlot slot;
        slot.exactKey = hashFloats({light.positionRange.x, light.positionRange.y, light.positionRange.z, light.positionRange.w, light.colorType.x, light.colorType.y, light.colorType.z, light.colorType.w, light.directionCos.x, light.directionCos.y, light.directionCos.z,
                                    light.directionCos.w, light.params.x, light.params.y, light.params.z});
        slot.identityKey = hashFloats({light.positionRange.w, light.colorType.x, light.colorType.y, light.colorType.z, light.colorType.w, light.directionCos.w, light.params.x, light.params.y, light.params.z});
        slot.position = position;
        slot.faceCount = faceCount;
        int match = -1;
        bool dirty = false;
        if (const auto found = exact.find(slot.exactKey); found != exact.end()) {
            for (uint32_t candidate : found->second) {
                if (!vsm.slots[candidate].matched && vsm.slots[candidate].faceCount == faceCount) {
                    match = static_cast<int>(candidate);
                    break;
                }
            }
        }
        if (match < 0) {
            // A moving light: same colour, range and type, nearest previous light within one cell.
            dirty = true;
            float best = kIdentityCell * kIdentityCell;
            const glm::ivec3 cell = cellOf(position);
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const auto found = identity.find(slot.identityKey ^ cellKey(cell + glm::ivec3(dx, dy, dz)));
                        if (found == identity.end()) continue;
                        for (uint32_t candidate : found->second) {
                            const VsmLightSlot& previous = vsm.slots[candidate];
                            const glm::vec3 offset = previous.position - position;
                            const float distance2 = glm::dot(offset, offset);
                            if (previous.matched || previous.faceCount != faceCount || distance2 >= best) continue;
                            best = distance2;
                            match = static_cast<int>(candidate);
                        }
                    }
                }
            }
        }
        if (match >= 0) {
            vsm.slots[static_cast<size_t>(match)].matched = true;
            slot.faceBase = vsm.slots[static_cast<size_t>(match)].faceBase;
        } else if (!allocateSlots(vsm, faceCount, slot.faceBase)) {
            continue;
        }
        if (!state.settings.vsmCache) dirty = true;
        // Portal lights only see casters on the receiving side of their portal plane (the other side is not their cell).
        const bool portalLight = index >= state.firstPortalLight && index - state.firstPortalLight < state.lightApertures.size();
        const glm::vec4 clipPlane = portalLight ? state.lightApertures[index - state.firstPortalLight].plane : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        writeLightFaces(vsm, light, index, slot.faceBase, dirty, clipPlane);
        light.params.w = -2.0f - static_cast<float>(slot.faceBase);
        // Overflow priority: projected angular size of the light's range (range / distance), lights outside the
        // main frustum last (they only serve portal views and reflections).
        const bool inFrustum = sphereVisible(uniforms.frustumPlanes, position, light.positionRange.w);
        visibleLights += inFrustum ? 1u : 0u;
        ranked.push_back({(inFrustum ? 1.0f : 1e-3f) * light.positionRange.w / (glm::distance(position, uniforms.viewPos) + 1.0f), index, static_cast<uint32_t>(next.size())});
        next.push_back(slot);
    }
    for (const VsmLightSlot& previous : vsm.slots) {
        if (previous.matched) continue;
        for (uint32_t face = 0; face < previous.faceCount; ++face) vsm.cpuFaces[previous.faceBase + face].info = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);
        (previous.faceCount == 1 ? vsm.freeSingle : vsm.freeCube).push_back(previous.faceBase);
    }
    // Deterministic tiers: rank by score (ties by light index), equal-sized tiers; tier 0 gets pages first on overflow.
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.score != b.score ? a.score > b.score : a.index < b.index; });
    for (size_t rank = 0; rank < ranked.size(); ++rank) {
        const VsmLightSlot& slot = next[ranked[rank].slot];
        const float tier = static_cast<float>(rank * kVsmTiers / ranked.size());
        for (uint32_t face = 0; face < slot.faceCount; ++face) vsm.cpuFaces[slot.faceBase + face].info.w = 1.0f + tier;
    }
    // Automatic resolution scale: coarser shadow texels as the visible light count outgrows what the pool can serve
    // (about pool / 32 lights at scale 1, then +1 per 4x more lights, clamped to 1..4).
    const float lightBudget = std::max(static_cast<float>(vsm.poolSide * vsm.poolSide) / 32.0f, 1.0f);
    const float autoFactor = state.settings.vsmAutoScale ? std::clamp(1.0f + 0.5f * std::log2(std::max(static_cast<float>(visibleLights) / lightBudget, 1.0f)), 1.0f, 4.0f) : 1.0f;
    vsm.effectiveScale = std::clamp(state.settings.vsmResolutionScale * autoFactor, 0.25f, 8.0f);
    vsm.slots = std::move(next);
    vsm.faceSlots = vsm.highWater;
    if (vsm.faceSlots > 0) state.context->UpdateBuffer(vsm.faces, 0, sizeof(VsmFaceGpu) * vsm.faceSlots, vsm.cpuFaces.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void fillVirtualShadowUniforms(const Lighting::Data& state, SceneUniforms& uniforms) {
    const VirtualShadowGpu& vsm = state.vsm;
    const bool on = virtualShadowsEnabled(state) && vsm.poolSize > static_cast<int>(kDisabledPool);
    uniforms.vsmParams = glm::vec4(on ? 1.0f : 0.0f, static_cast<float>(std::max(vsm.poolSize, 1)), static_cast<float>(std::max(vsm.poolSide, 1u)), vsm.effectiveScale);
    uniforms.vsmParams2 = glm::vec4(static_cast<float>(state.settings.vsmMaxMip), 0.0f, 0.0f, 0.0f);
}

}

bool Lighting::virtualShadowsActive() const {
    const Data& state = *m_data;
    const VirtualShadowGpu& vsm = state.vsm;
    return virtualShadowsEnabled(state) && vsm.poolSize > static_cast<int>(kDisabledPool) && !vsm.slots.empty() && vsm.motionPso && vsm.faceDirtyPso && vsm.clearDirtyPso && vsm.releasePso && vsm.histPso && vsm.planPso && vsm.allocatePso && vsm.finalizePso && vsm.requestPso && vsm.cullPso && vsm.prefixPso &&
           vsm.scatterPso && vsm.casterPso && vsm.clearPso;
}

Diligent::ITexture* Lighting::virtualShadowAtlas() const { return m_data->vsm.atlas; }
Diligent::IBuffer* Lighting::virtualShadowFaces() const { return m_data->vsm.faces; }
Diligent::IBuffer* Lighting::virtualShadowPageTable() const { return m_data->vsm.pageTable; }

VirtualShadowStats Lighting::virtualShadowStats() const {
    const VirtualShadowGpu& vsm = m_data->vsm;
    VirtualShadowStats stats;
    stats.faces = vsm.faceSlots;
    stats.requests = vsm.stats[1];
    stats.renderedPages = vsm.stats[9];
    stats.residentPages = vsm.stats[8];
    stats.poolPages = vsm.poolSide * vsm.poolSide;
    stats.overflow = vsm.stats[4];
    stats.casterInstances = vsm.stats[5];
    stats.movedObjects = vsm.stats[6];
    stats.resolutionScale = vsm.effectiveScale;
    return stats;
}

void Lighting::renderVirtualShadows(const VirtualShadowInputs& inputs) {
    Data& state = *m_data;
    if (!virtualShadowsActive() || !inputs.depth || !inputs.sceneUniforms || inputs.objectCount == 0) return;
    VirtualShadowGpu& vsm = state.vsm;
    Diligent::IDeviceContext* context = state.context;
    context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const bool firstRun = vsm.lastRunFrame != state.frame;
    if (firstRun) {
        // Diagnostics: counters of the run kStatsLag frames ago (the GPU is done with it by then).
        const uint32_t old = (state.frame + 1) % kVsmReadbackRing;
        if (vsm.readbackFrame[old] != 0 && state.frame - vsm.readbackFrame[old] >= kStatsLag) {
            Diligent::MapHelper<uint32_t> mapped(context, vsm.readback[old], Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
            if (const uint32_t* values = mapped) {
                std::memcpy(vsm.stats, values, sizeof(vsm.stats));
                vsm.statsFrame = vsm.readbackFrame[old];
                static const bool logStats = std::getenv("LIT_VSM_STATS") != nullptr;
                if (logStats && vsm.statsFrame % 30 == 0) {
                    Lit::Log::Info("VSM frame {}: faces {} requests {} rendered {} resident {}/{} overflow {} instances {} moved {}", vsm.statsFrame, vsm.faceSlots, vsm.stats[1], vsm.stats[9], vsm.stats[8], vsm.poolSide * vsm.poolSide, vsm.stats[4], vsm.stats[5], vsm.stats[6]);
                }
            }
        }
        if (vsm.resetPending) resetPool(state);
        ensureObjectCapacity(state, inputs.objectCount);
        if (inputs.objectCount < vsm.lastObjectCount) vsm.allDirty = true;
        vsm.lastObjectCount = inputs.objectCount;
    }

    const uint32_t pool = vsm.poolSide * vsm.poolSide;
    VsmConstantsGpu constants{};
    constants.counts[0] = vsm.faceSlots;
    constants.counts[1] = state.frame + kStampOffset;
    constants.counts[2] = inputs.objectCount;
    constants.counts[3] = pool;
    constants.limits[0] = kVsmRequestCapacity;
    constants.limits[1] = kVsmInstanceCapacity;
    constants.limits[2] = kVsmMovedCapacity;
    constants.limits[3] = (vsm.allDirty || !state.settings.vsmCache) ? 1u : 0u;
    constants.misc[0] = inputs.opaqueShaderCount;
    constants.misc[1] = pool;
    constants.tuning = glm::vec4(inputs.lodBias, 0.0f, 0.0f, 0.0f);
    context->UpdateBuffer(vsm.constants, 0, sizeof(constants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    // Per-run counters (request, pop, render, overflow, instance, moved, move-overflow); the free count persists.
    const uint32_t zeros[7] = {};
    context->UpdateBuffer(vsm.counters, sizeof(uint32_t), sizeof(zeros), zeros, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const std::array<uint32_t, kVsmHistWords> histZeros{};
    context->UpdateBuffer(vsm.counters, sizeof(uint32_t) * 16, sizeof(histZeros), histZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (firstRun) {
        const uint32_t zero = 0;
        context->UpdateBuffer(vsm.counters, sizeof(uint32_t) * 9, sizeof(zero), &zero, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    const std::vector<uint32_t> bucketZeros(kVsmBuckets, 0u);
    context->UpdateBuffer(vsm.buckets, 0, sizeof(uint32_t) * kVsmBuckets, bucketZeros.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const uint32_t drawCount = 0;
    context->UpdateBuffer(vsm.args, sizeof(uint32_t) * 12, sizeof(drawCount), &drawCount, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Bindings: two cached sets (the renderer double-buffers its per-frame object buffers).
    Diligent::ITextureView* depthView = litgpu::srv(inputs.depth);
    const std::vector<const void*> key = {inputs.sceneUniforms, inputs.worldMatrices, inputs.cullSpheres, inputs.renderables, inputs.meshInfos, inputs.dynamicStamps, depthView, vsm.atlas.RawPtr(), vsm.previousObjects.RawPtr(), state.gbuffer[1].RawPtr(), state.lights.RawPtr()};
    VirtualShadowGpu::Bindings* bindings = nullptr;
    for (auto& candidate : vsm.bindings) {
        if (candidate.key == key) bindings = &candidate;
    }
    if (!bindings) {
        bindings = &vsm.bindings[vsm.nextBinding];
        vsm.nextBinding = 1 - vsm.nextBinding;
        bindings->key = key;
        const std::vector<litgpu::Named> resources = {
            {"SceneData",          inputs.sceneUniforms              },
            {"VsmConstants",       vsm.constants                     },
            {"VsmCounters",        vsm.counters                      },
            {"VsmArgs",            vsm.args                          },
            {"VsmFaceBuffer",      vsm.faces                         },
            {"VsmPageTable",       vsm.pageTable                     },
            {"VsmPageStamp",       vsm.stamps                        },
            {"VsmPhysical",        vsm.physical                      },
            {"VsmRequests",        vsm.requests                      },
            {"VsmRenderList",      vsm.renderList                    },
            {"VsmFaceDirty",       vsm.faceDirty                     },
            {"VsmPreviousObjects", vsm.previousObjects               },
            {"VsmMoved",           vsm.moved                         },
            {"VsmInstances",       vsm.instances                     },
            {"VsmSortedInstances", vsm.sorted                        },
            {"VsmBuckets",         vsm.buckets                       },
            {"VsmDrawCommands",    vsm.commands                      },
            {"WorldMatrixBuffer",  inputs.worldMatrices              },
            {"CullSphereBuffer",   inputs.cullSpheres                },
            {"RenderableBuffer",   inputs.renderables                },
            {"MeshInfoBuffer",     inputs.meshInfos                  },
            {"DynamicStampBuffer", inputs.dynamicStamps ? inputs.dynamicStamps : vsm.noStamps.RawPtr()},
            {"MaterialBuffer",     state.materials                   },
            {"LightBuffer",        state.lights                      },
            {"ClusterBuffer",      state.clusters                    },
            {"u_depth",            depthView                         },
            {"u_gbuffer1",         litgpu::srv(state.gbuffer[1])     },
        };
        auto make = [&](Diligent::IPipelineState* pso) {
            RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
            if (!pso) return srb;
            pso->CreateShaderResourceBinding(&srb, true);
            litgpu::bindList(srb, resources);
            return srb;
        };
        bindings->motion = make(vsm.motionPso);
        bindings->faceDirty = make(vsm.faceDirtyPso);
        bindings->clearDirty = make(vsm.clearDirtyPso);
        bindings->release = make(vsm.releasePso);
        bindings->hist = make(vsm.histPso);
        bindings->plan = make(vsm.planPso);
        bindings->allocate = make(vsm.allocatePso);
        bindings->finalize = make(vsm.finalizePso);
        bindings->request = make(vsm.requestPso);
        bindings->cull = make(vsm.cullPso);
        bindings->prefix = make(vsm.prefixPso);
        bindings->scatter = make(vsm.scatterPso);
        bindings->caster = make(vsm.casterPso);
        bindings->clear = make(vsm.clearPso);
    }

    if (firstRun) {
        litgpu::dispatch(context, vsm.motionPso, bindings->motion, groups(inputs.objectCount, kGroup));
        litgpu::dispatch(context, vsm.faceDirtyPso, bindings->faceDirty, groups(vsm.faceSlots, kGroup));
    }
    litgpu::dispatch(context, vsm.requestPso, bindings->request, groups((static_cast<uint32_t>(state.width) + 1) / 2, kTileGroup), groups((static_cast<uint32_t>(state.height) + 1) / 2, kTileGroup));
    litgpu::dispatch(context, vsm.releasePso, bindings->release, groups(pool, kGroup));
    if (firstRun) litgpu::dispatch(context, vsm.clearDirtyPso, bindings->clearDirty, groups(vsm.faceSlots, kGroup));
    // Overflow is decided before anything is popped: unallocated requests are counted per (mip, face priority tier) and
    // whole groups are granted, coarse mips first and by tier within a mip, while free pages last. The outcome does not
    // depend on thread order, and the finest pages of the least important faces are the ones that go missing.
    litgpu::dispatch(context, vsm.histPso, bindings->hist, groups(kVsmRequestCapacity, kGroup));
    litgpu::dispatch(context, vsm.planPso, bindings->plan, 1);
    litgpu::dispatch(context, vsm.allocatePso, bindings->allocate, groups(kVsmRequestCapacity, kGroup));
    litgpu::dispatch(context, vsm.finalizePso, bindings->finalize, 1);

    auto indirectDispatch = [&](Diligent::IPipelineState* pso, Diligent::IShaderResourceBinding* srb, uint32_t wordOffset) {
        context->SetPipelineState(pso);
        context->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::DispatchComputeIndirectAttribs attribs{vsm.args, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, wordOffset * sizeof(uint32_t)};
        context->DispatchComputeIndirect(attribs);
    };
    indirectDispatch(vsm.cullPso, bindings->cull, 4);
    litgpu::dispatch(context, vsm.prefixPso, bindings->prefix, 1);
    indirectDispatch(vsm.scatterPso, bindings->scatter, 8);

    // Rasterise: clear the queued pages, then all caster instances in one multi-draw.
    Diligent::ITextureView* dsv = litgpu::dsv(vsm.atlas);
    context->SetRenderTargets(0, nullptr, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport;
    viewport.Width = viewport.Height = static_cast<float>(vsm.poolSize);
    context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(vsm.poolSize), static_cast<Diligent::Uint32>(vsm.poolSize));
    context->SetPipelineState(vsm.clearPso);
    context->CommitShaderResources(bindings->clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DrawIndirectAttribs clearAttribs;
    clearAttribs.pAttribsBuffer = vsm.args;
    clearAttribs.DrawArgsOffset = 0;
    clearAttribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->DrawIndirect(clearAttribs);

    Diligent::IBuffer* vertexBuffers[] = {inputs.vertices};
    context->SetVertexBuffers(0, 1, vertexBuffers, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    context->SetIndexBuffer(inputs.indices, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetPipelineState(vsm.casterPso);
    context->CommitShaderResources(bindings->caster, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DrawIndexedIndirectAttribs drawAttribs;
    drawAttribs.IndexType = Diligent::VT_UINT32;
    drawAttribs.pAttribsBuffer = vsm.commands;
    drawAttribs.DrawArgsOffset = 0;
    drawAttribs.DrawCount = kVsmBuckets;
    drawAttribs.DrawArgsStride = sizeof(uint32_t) * 5;
    drawAttribs.pCounterBuffer = vsm.args;
    drawAttribs.CounterOffset = sizeof(uint32_t) * 12;
    drawAttribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    drawAttribs.CounterBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->DrawIndexedIndirect(drawAttribs);
    context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const uint32_t ring = state.frame % kVsmReadbackRing;
    context->CopyBuffer(vsm.counters, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, vsm.readback[ring], 0, sizeof(uint32_t) * kVsmCounterWords, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    vsm.readbackFrame[ring] = state.frame;
    if (firstRun) vsm.allDirty = false;
    vsm.lastRunFrame = state.frame;
}
