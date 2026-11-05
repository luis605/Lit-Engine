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

constexpr size_t kAlphaWordBytes = 4;

}

void Renderer::uploadDirtyHierarchies(SceneDatabase& sceneDatabase) {
    auto& dirty = sceneDatabase.structureDirty.list;
    std::ranges::sort(dirty);
    const size_t limit = std::min(sceneDatabase.hierarchies.size(), m_maxObjects);
    for (size_t rangeBegin = 0; rangeBegin < dirty.size();) {
        const Entity first = dirty[rangeBegin];
        size_t rangeEnd = rangeBegin + 1;
        while (rangeEnd < dirty.size() && dirty[rangeEnd] == dirty[rangeEnd - 1] + 1) ++rangeEnd;
        const size_t last = std::min<size_t>(static_cast<size_t>(dirty[rangeEnd - 1]) + 1, limit);
        if (first < last) m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pHierarchyBuffer[m_currentFrame], first * sizeof(HierarchyComponent), (last - first) * sizeof(HierarchyComponent), sceneDatabase.hierarchies.data() + first, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        rangeBegin = rangeEnd;
    }
}

void Renderer::uploadAllRenderables(const SceneDatabase& sceneDatabase) {
    const auto& renderables = sceneDatabase.renderables;
    m_packedRenderables.resize(renderables.size());
    for (size_t entityIndex = 0; entityIndex < renderables.size(); ++entityIndex) m_packedRenderables[entityIndex] = PackRenderable(renderables[entityIndex]);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pRenderableBuffer[m_currentFrame], 0, m_packedRenderables.size() * sizeof(uint32_t), m_packedRenderables.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_packedAlphas.assign((renderables.size() + 3) & ~size_t{3}, 0);
    for (size_t entityIndex = 0; entityIndex < renderables.size(); ++entityIndex) m_packedAlphas[entityIndex] = PackAlpha(renderables[entityIndex].alpha);
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pAlphaBuffer, 0, m_packedAlphas.size(), m_packedAlphas.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::uploadDirtyRenderables(SceneDatabase& sceneDatabase) {
    auto& dirty = sceneDatabase.renderableDirty.list;
    std::ranges::sort(dirty);
    auto* context = m_diligent->pImmediateContext.RawPtr();
    size_t rangeStart = 0;
    size_t rangeEnd = 0;
    auto flushRange = [&] {
        if (rangeEnd > rangeStart) context->UpdateBuffer(m_diligent->pRenderableBuffer[m_currentFrame], rangeStart * sizeof(uint32_t), (rangeEnd - rangeStart) * sizeof(uint32_t), m_packedRenderables.data() + rangeStart, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        rangeStart = rangeEnd = 0;
    };
    for (Entity entityIndex : dirty) {
        if (entityIndex >= m_packedRenderables.size() || entityIndex >= sceneDatabase.renderables.size()) continue;
        const uint32_t packed = PackRenderable(sceneDatabase.renderables[entityIndex]);
        if (packed == m_packedRenderables[entityIndex]) continue;
        m_packedRenderables[entityIndex] = packed;
        if (entityIndex != rangeEnd) flushRange();
        if (rangeEnd == rangeStart) rangeStart = entityIndex;
        rangeEnd = static_cast<size_t>(entityIndex) + 1;
    }
    flushRange();

    for (Entity entityIndex : dirty) {
        if (entityIndex >= sceneDatabase.renderables.size() || entityIndex >= m_packedAlphas.size()) continue;
        const uint8_t alpha = PackAlpha(sceneDatabase.renderables[entityIndex].alpha);
        if (alpha == m_packedAlphas[entityIndex]) continue;
        m_packedAlphas[entityIndex] = alpha;
        const size_t wordOffset = entityIndex & ~Entity{3};
        context->UpdateBuffer(m_diligent->pAlphaBuffer, wordOffset, kAlphaWordBytes, m_packedAlphas.data() + wordOffset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

void Renderer::ensureLevelOrder(SceneDatabase& sceneDatabase) {
    if (m_processedStructureVersion == sceneDatabase.m_structureVersion) return;
    sceneDatabase.updateHierarchy();
    m_processedStructureVersion = sceneDatabase.m_structureVersion;
    m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pSortedHierarchyBuffer[m_currentFrame], 0, sceneDatabase.sortedHierarchyList.size() * sizeof(Entity), sceneDatabase.sortedHierarchyList.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::syncVersions(const SceneDatabase& sceneDatabase) {
    if (m_processedHierarchyVersion < sceneDatabase.m_hierarchyVersion) {
        m_hierarchyUpdateCounter = 1;
        m_processedHierarchyVersion = sceneDatabase.m_hierarchyVersion;
    }
}

void Renderer::mergeTransformDirty(SceneDatabase& sceneDatabase, TransformPlan& plan) {
    if (m_processedTransformVersion < sceneDatabase.m_transformVersion) {
        m_transformUpdateCounter = 1;
        plan.bulkUpload = true;
        m_processedTransformVersion = sceneDatabase.m_transformVersion;
    }

    if (m_processedDataVersion < sceneDatabase.m_dataVersion) {
        m_fullTransformUpdateCounter = 1;
        m_renderableUpdateCounter = 1;
        plan.bulkUpload = true;
        m_processedDataVersion = sceneDatabase.m_dataVersion;
    }

    const std::vector<Entity>& newlyDirty = sceneDatabase.transformDirty.list;
    if (!newlyDirty.empty()) {
        if (newlyDirty.size() * 2 > sceneDatabase.transforms.size() && newlyDirty.size() > kMaxDirtyPerFrame) {
            plan.bulkUpload = true;
            m_transformUpdateCounter = 1;
        } else if (!plan.bulkUpload) {
            m_pendingDirty.insert(m_pendingDirty.end(), newlyDirty.begin(), newlyDirty.end());
        }
        sceneDatabase.transformDirty.clear();
    }

    if (plan.bulkUpload) m_pendingDirty.clear();
}

void Renderer::uploadHierarchies(SceneDatabase& sceneDatabase) {
    if (m_hierarchyUpdateCounter > 0) {
        m_processedStructureVersion = 0;
        ensureLevelOrder(sceneDatabase);
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pHierarchyBuffer[m_currentFrame], 0, sceneDatabase.hierarchies.size() * sizeof(HierarchyComponent), sceneDatabase.hierarchies.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_hierarchyUpdateCounter--;
    } else if (!sceneDatabase.structureDirty.list.empty()) {
        uploadDirtyHierarchies(sceneDatabase);
    }
    sceneDatabase.structureDirty.clear();
}

void Renderer::uploadTransforms(const SceneDatabase& sceneDatabase) {
    auto* context = m_diligent->pImmediateContext.RawPtr();
    auto* objectBuffer = m_diligent->pObjectBuffer[m_currentFrame].RawPtr();
    if (m_fullTransformUpdateCounter > 0) {
        context->UpdateBuffer(objectBuffer, 0, sceneDatabase.transforms.size() * sizeof(TransformComponent), sceneDatabase.transforms.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_fullTransformUpdateCounter--;
        if (m_transformUpdateCounter > 0) m_transformUpdateCounter--;
    } else if (m_transformUpdateCounter > 0) {
        const size_t count = (sceneDatabase.m_movingCount > 0) ? std::min<size_t>(sceneDatabase.m_movingCount, sceneDatabase.transforms.size()) : sceneDatabase.transforms.size();
        context->UpdateBuffer(objectBuffer, 0, count * sizeof(TransformComponent), sceneDatabase.transforms.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_transformUpdateCounter--;
    }
}

void Renderer::uploadRenderables(SceneDatabase& sceneDatabase) {
    if (m_renderableUpdateCounter > 0) {
        uploadAllRenderables(sceneDatabase);
        m_renderableUpdateCounter--;
    } else if (!sceneDatabase.renderableDirty.list.empty()) {
        uploadDirtyRenderables(sceneDatabase);
    }
    sceneDatabase.renderableDirty.clear();
}

void Renderer::uploadMeshInfo() {
    if (!m_meshInfoDirty) return;
    auto& gpu = *m_diligent;
    const size_t meshInfoCount = std::min<size_t>(gMeshInfos.size(), kMaxMeshes);
    gpu.pImmediateContext->UpdateBuffer(gpu.pMeshInfoBuffer, 0, meshInfoCount * sizeof(MeshInfo), gMeshInfos.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (!gNormalSamples.empty()) gpu.pImmediateContext->UpdateBuffer(gpu.pNormalSampleBuffer, 0, gNormalSamples.size() * sizeof(glm::vec4), gNormalSamples.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_meshInfoDirty = false;
}

Renderer::TransformPlan Renderer::uploadSceneData(SceneDatabase& sceneDatabase) {
    TransformPlan plan;
    plan.hierarchyChanged = m_hierarchyUpdateCounter > 0;
    mergeTransformDirty(sceneDatabase, plan);
    uploadHierarchies(sceneDatabase);
    plan.fullUpdate = m_fullTransformUpdateCounter > 0;
    plan.localMatricesUploading = plan.fullUpdate || m_transformUpdateCounter > 0;
    uploadTransforms(sceneDatabase);
    uploadRenderables(sceneDatabase);
    m_transformEpoch++;
    planTransformWork(sceneDatabase, plan);
    return plan;
}
