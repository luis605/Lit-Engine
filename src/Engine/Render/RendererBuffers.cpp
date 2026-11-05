module;

#include "RendererPrelude.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

using BufferPtr = Diligent::RefCntAutoPtr<Diligent::IBuffer>;

constexpr size_t kUniformAlignment = 256;
constexpr uint32_t kShaderBoundsCount = 16;
constexpr uint32_t kCascadeCullHeaderVectors = 25;
constexpr uint32_t kLocalCullVectorsPerLight = 3;
constexpr uint32_t kObjectDispatchWorkgroupSize = 256;
constexpr size_t kShadowCullUniformBytes = sizeof(glm::vec4) * 8 + sizeof(glm::mat4);
constexpr size_t kShadowPassUniformBytes = sizeof(glm::mat4) + sizeof(glm::vec4);

struct PerFrameBufferSpec {
    BufferPtr* buffers;
    const char* name;
    Diligent::Uint32 stride;
    Diligent::Uint32 count;
};

void CreateSharedAcrossFrames(Diligent::IRenderDevice* device, const PerFrameBufferSpec& spec, int frameCount) {
    spec.buffers[0] = CreateStructuredBuffer(device, spec.name, spec.stride, spec.count);
    for (int frameIndex = 1; frameIndex < frameCount; ++frameIndex) spec.buffers[frameIndex] = spec.buffers[0];
}

}

void Renderer::releaseBindings() {
    auto& gpu = *m_diligent;
    for (auto& phase : gpu.visibility) {
        for (int frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) {
            phase.cullSRB[frameIndex].Release();
            phase.prefixSumSRB[frameIndex].Release();
            phase.scatterDispatchArgsSRB[frameIndex].Release();
            phase.scatterSRB[frameIndex].Release();
            phase.commandGenSRB[frameIndex].Release();
            phase.opaqueSRBs[frameIndex].clear();
            phase.pointSRBs[frameIndex].clear();
        }
    }
    for (int frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) {
        gpu.pTransformSRB[frameIndex].Release();
        gpu.pTransformWorkSRB[frameIndex].Release();
        gpu.pApplyDirtySRB[frameIndex].Release();
        gpu.pCasterDispatchArgsSRB[frameIndex].Release();
        gpu.pLocalCasterDispatchArgsSRB[frameIndex].Release();
        gpu.pShadowRetestDispatchArgsSRB[frameIndex].Release();
        gpu.pEarlyCompactSRB[frameIndex].Release();
        gpu.pEarlyDispatchArgsSRB[frameIndex].Release();
        gpu.pTransparentDispatchArgsSRB[frameIndex].Release();
        for (int cascade = 0; cascade < kCascadeShadowViews; ++cascade) {
            gpu.shadowCullSRB[frameIndex][cascade].Release();
            gpu.shadowLateCullSRB[frameIndex][cascade].Release();
        }
        gpu.localCullSRB[frameIndex].Release();
        for (int view = 0; view < kShadowViews; ++view) {
            gpu.shadowSRBs[frameIndex][view].clear();
            gpu.shadowPointSRBs[frameIndex][view].clear();
        }
    }
}

void Renderer::releaseObjectBuffers() {
    auto& gpu = *m_diligent;
    for (auto& phase : gpu.visibility) {
        phase.visibleEntries.Release();
        phase.sortedEntries.Release();
    }
    for (int frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) {
        gpu.pObjectBuffer[frameIndex].Release();
        gpu.pWorldMatrixBuffer[frameIndex].Release();
        gpu.pCullOrientationBuffer[frameIndex].Release();
        gpu.pCullSphereBuffer[frameIndex].Release();
        gpu.pTouchedEpochBuffer[frameIndex].Release();
        gpu.pHierarchyBuffer[frameIndex].Release();
        gpu.pRenderableBuffer[frameIndex].Release();
        gpu.pSortedHierarchyBuffer[frameIndex].Release();
    }
    gpu.pCasterIds.Release();
    gpu.pLocalCasterIds.Release();
    gpu.pShadowRetestIds.Release();
    gpu.pEarlyCandidateCounter.Release();
    gpu.pEarlyCandidateIds.Release();
    gpu.pEarlyCandidateDispatchArgs.Release();
    gpu.pTransparentCandidateIds.Release();
    gpu.pAlphaBuffer.Release();
    gpu.pVisibilityBits.Release();
    releaseBindings();
    m_custom.clearBindings();
    gpu.pImmediateContext->Flush();
    gpu.pDevice->IdleGPU();
    gpu.pDevice->ReleaseStaleResources(true);
    gpu.pDevice->ReleaseStaleResources(true);
}

void Renderer::allocateMeshBuffers() {
    auto& gpu = *m_diligent;
    gpu.pMeshInfoBuffer = CreateStructuredBuffer(gpu.pDevice, "Mesh Info Buffer", sizeof(MeshInfo), kMaxMeshes);
    gpu.pTransformWorkBuffer = CreateStructuredBuffer(gpu.pDevice, "Transform Work Buffer", sizeof(Entity), kMaxTransformWork);
    gpu.pNormalSampleBuffer = CreateStructuredBuffer(gpu.pDevice, "Normal Sample Buffer", sizeof(glm::vec4), kMaxMeshes * kNormalSamples);
    m_meshInfoDirty = true;

    const uint32_t totalBuckets = static_cast<uint32_t>(m_numDrawingShaders) * kMaxMeshes;
    m_bucketZeros.assign(std::max(totalBuckets, kShadowBucketCapacity), 0);
}

void Renderer::allocateVisibilityBuffers() {
    auto& gpu = *m_diligent;
    const auto objectCapacity = static_cast<Diligent::Uint32>(m_maxObjects);
    const auto shaderCount = static_cast<Diligent::Uint32>(m_numDrawingShaders);
    const uint32_t totalBuckets = shaderCount * kMaxMeshes;
    const size_t commandBytes = static_cast<size_t>(kMaxMeshes) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand);
    unsigned int zero = 0;
    for (int phaseIndex = kPhaseOpaque; phaseIndex <= kPhaseTransparent; ++phaseIndex) {
        auto& phase = gpu.visibility[phaseIndex];
        phase.cullingUniforms = CreateUniformBuffer(gpu.pDevice, "Culling Uniforms", sizeof(CullingUniforms), Diligent::USAGE_DEFAULT);
        phase.visibleCounter = CreateStructuredBuffer(gpu.pDevice, "Visible Counter", sizeof(unsigned int), 1, &zero);
        phase.visibleEntries = CreateStructuredBuffer(gpu.pDevice, "Visible Entries", sizeof(unsigned int) * 2, objectCapacity);
        phase.sortedEntries = CreateStructuredBuffer(gpu.pDevice, "Sorted Visible Entries", sizeof(unsigned int) * 2, objectCapacity);
        phase.bucketCounts = CreateStructuredBuffer(gpu.pDevice, "Bucket Counts", sizeof(unsigned int), totalBuckets);
        phase.bucketOffsets = CreateStructuredBuffer(gpu.pDevice, "Bucket Offsets", sizeof(unsigned int), totalBuckets);
        phase.bucketWriteHeads = CreateStructuredBuffer(gpu.pDevice, "Bucket Write Heads", sizeof(unsigned int), totalBuckets);
        phase.scatterDispatchArgs = CreateDispatchArgsBuffer(gpu.pDevice, "Scatter Dispatch Args");
        phase.drawCommands = CreateIndirectBuffer(gpu.pDevice, "Draw Commands", commandBytes);
        phase.drawCounters = CreateStructuredBuffer(gpu.pDevice, "Draw Counters", sizeof(unsigned int), shaderCount, m_bucketZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
        phase.pointCommands = CreateIndirectBuffer(gpu.pDevice, "Point Commands", commandBytes);
        phase.pointCounters = CreateStructuredBuffer(gpu.pDevice, "Point Counters", sizeof(unsigned int), shaderCount, m_bucketZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
    }
}

void Renderer::allocateShadowBuffers() {
    auto& gpu = *m_diligent;
    const size_t commandBytes = static_cast<size_t>(kMaxMeshes) * m_numDrawingShaders * sizeof(DrawElementsIndirectCommand);
    auto& shadow = gpu.visibility[kPhaseShadow];
    const auto& shared = gpu.visibility[kPhaseTransparent];
    shadow.cullingUniforms = CreateUniformBuffer(gpu.pDevice, "Shadow Culling Uniforms", sizeof(CullingUniforms), Diligent::USAGE_DEFAULT);
    shadow.visibleCounter = shared.visibleCounter;
    shadow.visibleEntries = shared.visibleEntries;
    shadow.sortedEntries = shared.sortedEntries;
    shadow.bucketCounts = CreateStructuredBuffer(gpu.pDevice, "Shadow Bucket Counts", sizeof(unsigned int), kShadowBucketCapacity);
    shadow.bucketOffsets = CreateStructuredBuffer(gpu.pDevice, "Shadow Bucket Offsets", sizeof(unsigned int), kShadowBucketCapacity);
    shadow.bucketWriteHeads = CreateStructuredBuffer(gpu.pDevice, "Shadow Bucket Write Heads", sizeof(unsigned int), kShadowBucketCapacity);
    shadow.scatterDispatchArgs = shared.scatterDispatchArgs;
    const auto viewCounters = static_cast<Diligent::Uint32>(m_numDrawingShaders * kMaxLocalShadowViews);
    shadow.drawCommands = CreateIndirectBuffer(gpu.pDevice, "Shadow Draw Commands", commandBytes * kMaxLocalShadowViews);
    shadow.drawCounters = CreateStructuredBuffer(gpu.pDevice, "Shadow Draw Counters", sizeof(unsigned int), viewCounters, m_bucketZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);
    shadow.pointCommands = CreateIndirectBuffer(gpu.pDevice, "Shadow Point Commands", commandBytes * kMaxLocalShadowViews);
    shadow.pointCounters = CreateStructuredBuffer(gpu.pDevice, "Shadow Point Counters", sizeof(unsigned int), viewCounters, m_bucketZeros.data(), Diligent::BIND_INDIRECT_DRAW_ARGS);

    for (int view = 0; view < kShadowViews; ++view) {
        gpu.pShadowCullUBO[view] = CreateUniformBuffer(gpu.pDevice, "Shadow Cull", kShadowCullUniformBytes, Diligent::USAGE_DEFAULT);
        gpu.pShadowPassUBO[view] = CreateUniformBuffer(gpu.pDevice, "Shadow Pass", kShadowPassUniformBytes, Diligent::USAGE_DEFAULT);
    }
    std::vector<float> bounds(kShaderBoundsCount, 0.0f);
    for (size_t surfaceIndex = 0; surfaceIndex < gpu.surfaces.size() && surfaceIndex < bounds.size(); ++surfaceIndex) bounds[surfaceIndex] = gpu.surfaces[surfaceIndex].bounds;
    gpu.pShaderBounds = CreateStructuredBuffer(gpu.pDevice, "Shader Bounds", sizeof(float), kShaderBoundsCount, bounds.data());
    gpu.pLocalShadowViewsUBO = CreateUniformBuffer(gpu.pDevice, "Local Shadow Views", sizeof(glm::vec4) * (kMaxLocalShadowViews + 1), Diligent::USAGE_DEFAULT);
    gpu.pCascadeCullUBO = CreateUniformBuffer(gpu.pDevice, "Cascade Cull", sizeof(glm::vec4) * (kCascadeCullHeaderVectors + kLocalCullVectorsPerLight * kMaxLocalShadowLights), Diligent::USAGE_DEFAULT);
}

void Renderer::allocateCandidateBuffers() {
    auto& gpu = *m_diligent;
    const auto objectCapacity = static_cast<Diligent::Uint32>(m_maxObjects);
    unsigned int zero = 0;
    gpu.pLocalCasterCounter = CreateStructuredBuffer(gpu.pDevice, "Local Caster Counter", sizeof(unsigned int), 1, &zero);
    gpu.pLocalCasterIds = CreateStructuredBuffer(gpu.pDevice, "Local Caster Ids", sizeof(unsigned int) * 2, objectCapacity);
    gpu.pLocalCasterDispatchArgs = CreateDispatchArgsBuffer(gpu.pDevice, "Local Caster Dispatch Args");
    gpu.pCasterCounter = CreateStructuredBuffer(gpu.pDevice, "Caster Counter", sizeof(unsigned int), 1, &zero);
    gpu.pCasterIds = CreateStructuredBuffer(gpu.pDevice, "Caster Ids", sizeof(unsigned int), objectCapacity);
    gpu.pCasterDispatchArgs = CreateDispatchArgsBuffer(gpu.pDevice, "Caster Dispatch Args");
    gpu.pShadowRetestCounter = CreateStructuredBuffer(gpu.pDevice, "Shadow Retest Counter", sizeof(unsigned int), 1, &zero);
    gpu.pShadowRetestIds = CreateStructuredBuffer(gpu.pDevice, "Shadow Retest Ids", sizeof(unsigned int), objectCapacity);
    gpu.pShadowRetestDispatchArgs = CreateDispatchArgsBuffer(gpu.pDevice, "Shadow Retest Dispatch Args");
    gpu.pEarlyCandidateCounter = CreateStructuredBuffer(gpu.pDevice, "Early Candidate Counter", sizeof(unsigned int), 1, &zero);
    gpu.pEarlyCandidateIds = CreateStructuredBuffer(gpu.pDevice, "Early Candidate Ids", sizeof(unsigned int), objectCapacity);
    gpu.pEarlyCandidateDispatchArgs = CreateDispatchArgsBuffer(gpu.pDevice, "Early Candidate Dispatch Args");
    gpu.shadowHiZSourceMap = nullptr;

    std::vector<uint32_t> clearedBits(static_cast<size_t>(objectCapacity + kWorkgroupSize - 1) / kWorkgroupSize * kVisibilityBitWordsPerGroup, 0u);
    gpu.pVisibilityBits = CreateStructuredBuffer(gpu.pDevice, "Visibility Bits", sizeof(uint32_t), static_cast<Diligent::Uint32>(clearedBits.size()), clearedBits.data());
    gpu.pTransparentCandidateCounter = CreateStructuredBuffer(gpu.pDevice, "Transparent Candidate Counter", sizeof(unsigned int), 1, &zero);
    gpu.pTransparentCandidateIds = CreateStructuredBuffer(gpu.pDevice, "Transparent Candidate Ids", sizeof(unsigned int), objectCapacity);
    gpu.pTransparentDispatchArgs = CreateDispatchArgsBuffer(gpu.pDevice, "Transparent Dispatch Args");
    gpu.pAlphaBuffer = CreateStructuredBuffer(gpu.pDevice, "Alpha Buffer", sizeof(uint32_t), (objectCapacity + 3) / 4);
}

void Renderer::allocateObjectBuffers() {
    auto& gpu = *m_diligent;
    const auto objectCapacity = static_cast<Diligent::Uint32>(m_maxObjects);
    const PerFrameBufferSpec specs[] = {
        {gpu.pObjectBuffer,          "Object Buffer",           sizeof(TransformComponent),  objectCapacity   },
        {gpu.pWorldMatrixBuffer,     "World Matrix Buffer",     sizeof(glm::vec4) * 3,       objectCapacity   },
        {gpu.pCullOrientationBuffer, "Cull Orientation Buffer", sizeof(uint32_t),            objectCapacity   },
        {gpu.pCullSphereBuffer,      "Cull Sphere Buffer",      sizeof(glm::vec4),           objectCapacity   },
        {gpu.pDirtyIndexBuffer,      "Dirty Index Buffer",      sizeof(unsigned int),        kMaxDirtyPerFrame},
        {gpu.pDirtyPayloadBuffer,    "Dirty Payload Buffer",    sizeof(glm::mat4),           kMaxDirtyPerFrame},
        {gpu.pTouchedEpochBuffer,    "Touched Epoch Buffer",    sizeof(unsigned int),        objectCapacity   },
        {gpu.pHierarchyBuffer,       "Hierarchy Buffer",        sizeof(HierarchyComponent),  objectCapacity   },
        {gpu.pRenderableBuffer,      "Renderable Buffer",       sizeof(uint32_t),            objectCapacity   },
        {gpu.pSortedHierarchyBuffer, "Sorted Hierarchy Buffer", sizeof(unsigned int),        objectCapacity   }
    };
    for (const PerFrameBufferSpec& spec : specs) CreateSharedAcrossFrames(gpu.pDevice, spec, NUM_FRAMES_IN_FLIGHT);

    Diligent::BufferDesc sceneUniformDesc;
    sceneUniformDesc.Name = "Scene UBO";
    sceneUniformDesc.Usage = Diligent::USAGE_DEFAULT;
    sceneUniformDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    sceneUniformDesc.Size = (sizeof(SceneUniforms) + kUniformAlignment - 1) & ~(kUniformAlignment - 1);
    for (auto& sceneUniforms : gpu.pSceneUBO) sceneUniforms.Release();
    gpu.pDevice->CreateBuffer(sceneUniformDesc, nullptr, &gpu.pSceneUBO[0]);
    for (int frameIndex = 1; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) gpu.pSceneUBO[frameIndex] = gpu.pSceneUBO[0];
}

bool Renderer::objectBuffersAllocated() const {
    const auto& gpu = *m_diligent;
    const auto& opaquePhase = gpu.visibility[kPhaseOpaque];
    const auto& retestPhase = gpu.visibility[kPhaseRetest];
    for (int frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) {
        const bool perFrameOk = gpu.pObjectBuffer[frameIndex] && gpu.pWorldMatrixBuffer[frameIndex] && gpu.pCullSphereBuffer[frameIndex] && gpu.pCullOrientationBuffer[frameIndex] && gpu.pHierarchyBuffer[frameIndex] && gpu.pRenderableBuffer[frameIndex] && gpu.pSortedHierarchyBuffer[frameIndex] && gpu.pTouchedEpochBuffer[frameIndex] && gpu.pDirtyIndexBuffer[frameIndex] && gpu.pDirtyPayloadBuffer[frameIndex] && gpu.pSceneUBO[frameIndex];
        if (!perFrameOk) return false;
    }
    return gpu.pTransparentCandidateIds && gpu.pAlphaBuffer && opaquePhase.visibleEntries && opaquePhase.sortedEntries && retestPhase.visibleEntries && retestPhase.sortedEntries && gpu.pVisibilityBits && gpu.pEarlyCandidateIds;
}

bool Renderer::reallocateBuffers(size_t numObjects) {
    auto& gpu = *m_diligent;
    if (m_maxObjects > 0) releaseObjectBuffers();
    m_maxObjects = numObjects;
    Lit::Log::Info("Reallocating renderer buffers for {} objects.", m_maxObjects);

    allocateMeshBuffers();
    allocateVisibilityBuffers();
    allocateShadowBuffers();
    allocateCandidateBuffers();
    allocateObjectBuffers();

    if (!objectBuffersAllocated()) {
        Lit::Log::Error("Renderer buffer allocation failed for {} objects.", m_maxObjects);
        return false;
    }

    if (!gpu.pBasePositionBuffer) {
        glm::vec4 origin(0.0f);
        gpu.pBasePositionBuffer = CreateStructuredBuffer(gpu.pDevice, "Base Position Buffer", sizeof(glm::vec4), 1, &origin);
    }

    for (int frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) {
        bindComputeResources(frameIndex);
        bindVisibilityResources(frameIndex);
        bindShadowResources(frameIndex);
    }
    rebindSurfaceResources();

    if (gpu.pObjectDispatchArgsConstants) {
        DispatchArgsConstants dispatchArgsConstants{};
        dispatchArgsConstants.maxCount = static_cast<uint32_t>(m_maxObjects);
        dispatchArgsConstants.workgroupSize = kObjectDispatchWorkgroupSize;
        gpu.pImmediateContext->UpdateBuffer(gpu.pObjectDispatchArgsConstants, 0, sizeof(dispatchArgsConstants), &dispatchArgsConstants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    m_fullTransformUpdateCounter = 1;
    m_transformUpdateCounter = 0;
    m_renderableUpdateCounter = 1;
    m_hierarchyUpdateCounter = 1;
    return true;
}
