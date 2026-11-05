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

constexpr int kBarrierBufferCount = 2;

}

bool Renderer::animationActive() const { return m_animMovingCount > 0 && m_diligent->pBasePositionBuffer; }

bool Renderer::animatedEntitiesAreLeaves(const SceneDatabase& sceneDatabase) {
    const std::array<uint64_t, 3> key = {sceneDatabase.m_structureVersion, m_animEntityOffset, m_animMovingCount};
    if (key == m_animLeafCheckKey) return m_animatedEntitiesAreLeaves;
    m_animLeafCheckKey = key;
    m_animatedEntitiesAreLeaves = true;
    const size_t end = std::min<size_t>(static_cast<size_t>(m_animEntityOffset) + m_animMovingCount, sceneDatabase.hierarchies.size());
    for (size_t entityIndex = m_animEntityOffset; entityIndex < end && m_animatedEntitiesAreLeaves; ++entityIndex) m_animatedEntitiesAreLeaves = !sceneDatabase.hasChildren(static_cast<Entity>(entityIndex));
    return m_animatedEntitiesAreLeaves;
}

bool Renderer::collectTransformWork(const SceneDatabase& sceneDatabase, std::span<const Entity> dirty) {
    const size_t entityCount = sceneDatabase.hierarchies.size();
    if (m_transformWorkStamp.size() < entityCount) m_transformWorkStamp.resize(entityCount, 0);
    if (++m_transformWorkEpoch == 0) {
        std::ranges::fill(m_transformWorkStamp, 0u);
        m_transformWorkEpoch = 1;
    }

    m_transformWorkScratch.clear();
    auto enqueue = [&](Entity entityIndex) {
        if (entityIndex >= entityCount || m_transformWorkStamp[entityIndex] == m_transformWorkEpoch) return;
        m_transformWorkStamp[entityIndex] = m_transformWorkEpoch;
        m_transformWorkScratch.push_back(entityIndex);
    };
    for (Entity entityIndex : dirty) enqueue(entityIndex);
    for (size_t next = 0; next < m_transformWorkScratch.size(); ++next) {
        if (m_transformWorkScratch.size() > kMaxTransformWork) return false;
        sceneDatabase.forEachChild(m_transformWorkScratch[next], enqueue);
    }
    if (m_transformWorkScratch.size() > kMaxTransformWork) return false;

    m_transformWorkLevelCounts.clear();
    for (Entity entityIndex : m_transformWorkScratch) {
        const uint32_t level = sceneDatabase.hierarchies[entityIndex].level;
        if (m_transformWorkLevelCounts.size() <= level) m_transformWorkLevelCounts.resize(level + 1, 0);
        m_transformWorkLevelCounts[level]++;
    }
    std::vector<uint32_t>& levelHeads = m_transformWorkLevelHeads;
    levelHeads.assign(m_transformWorkLevelCounts.size(), 0);
    for (size_t level = 1; level < levelHeads.size(); ++level) levelHeads[level] = levelHeads[level - 1] + m_transformWorkLevelCounts[level - 1];
    m_transformWork.resize(m_transformWorkScratch.size());
    for (Entity entityIndex : m_transformWorkScratch) m_transformWork[levelHeads[sceneDatabase.hierarchies[entityIndex].level]++] = entityIndex;
    return true;
}

void Renderer::markDirtyLevels(const SceneDatabase& sceneDatabase) {
    m_levelNeedsProcessing.resize(sceneDatabase.m_maxHierarchyDepth + 1, 0);
    for (Entity entityIndex : m_dirtyIndexScratch) {
        const uint32_t level = sceneDatabase.hierarchies[entityIndex].level;
        if (level < m_levelNeedsProcessing.size()) m_levelNeedsProcessing[level] = 1;
    }
    for (size_t level = 1; level < m_levelNeedsProcessing.size(); ++level) {
        if (m_levelNeedsProcessing[level - 1]) m_levelNeedsProcessing[level] = 1;
    }
}

void Renderer::applyDirtyTransforms(const SceneDatabase& sceneDatabase) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const size_t dirtyCount = m_dirtyIndexScratch.size();
    m_dirtyPayloadScratch.resize(dirtyCount);
    for (size_t dirtyIndex = 0; dirtyIndex < dirtyCount; ++dirtyIndex) m_dirtyPayloadScratch[dirtyIndex] = sceneDatabase.transforms[m_dirtyIndexScratch[dirtyIndex]].localMatrix;
    for (size_t first = 0; first < dirtyCount; first += kMaxDirtyPerFrame) {
        const size_t chunk = std::min<size_t>(kMaxDirtyPerFrame, dirtyCount - first);
        context->UpdateBuffer(gpu.pDirtyIndexBuffer[m_currentFrame], 0, chunk * sizeof(unsigned int), m_dirtyIndexScratch.data() + first, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->UpdateBuffer(gpu.pDirtyPayloadBuffer[m_currentFrame], 0, chunk * sizeof(glm::mat4), m_dirtyPayloadScratch.data() + first, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        {
            Diligent::MapHelper<ApplyDirtyConstants> constants(gpu.pImmediateContext, gpu.pApplyDirtyConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            constants->dirtyCount = static_cast<uint32_t>(chunk);
            constants->epoch = m_transformEpoch;
        }
        context->SetPipelineState(gpu.pApplyDirtyPSO);
        context->CommitShaderResources(gpu.pApplyDirtySRB[m_currentFrame], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute(Diligent::DispatchComputeAttribs((static_cast<unsigned int>(chunk) + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1));
    }
}

void Renderer::planTransformWork(SceneDatabase& sceneDatabase, TransformPlan& plan) {
    plan.forceAllLevels = plan.hierarchyChanged || plan.bulkUpload || plan.fullUpdate || (m_animMovingCount > 0 && !animatedEntitiesAreLeaves(sceneDatabase));
    if (plan.forceAllLevels) ensureLevelOrder(sceneDatabase);
    m_levelNeedsProcessing.assign(sceneDatabase.m_maxHierarchyDepth + 1, plan.forceAllLevels ? 1u : 0u);
    if (plan.bulkUpload || m_pendingDirty.empty()) return;

    m_dirtyIndexScratch.clear();
    m_dirtyIndexScratch.reserve(m_pendingDirty.size());
    for (Entity entityIndex : m_pendingDirty) {
        if (entityIndex < m_maxObjects && entityIndex < sceneDatabase.transforms.size()) m_dirtyIndexScratch.push_back(entityIndex);
    }

    plan.sparse = !plan.forceAllLevels && collectTransformWork(sceneDatabase, m_dirtyIndexScratch);
    if (plan.sparse) {
        m_diligent->pImmediateContext->UpdateBuffer(m_diligent->pTransformWorkBuffer, 0, m_transformWork.size() * sizeof(Entity), m_transformWork.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    } else if (!plan.hierarchyChanged) {
        ensureLevelOrder(sceneDatabase);
        markDirtyLevels(sceneDatabase);
    }
    applyDirtyTransforms(sceneDatabase);
    m_pendingDirty.clear();
}

void Renderer::dispatchTransformGroup(Diligent::IShaderResourceBinding* binding, uint32_t listOffset, uint32_t count, bool forceRecompute, bool directEntityRange) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    TransformUniforms uniforms{};
    uniforms.objectCount = count;
    uniforms.sortedListOffset = listOffset;
    uniforms.epoch = m_transformEpoch;
    uniforms.forceFullRecompute = forceRecompute ? 1u : 0u;
    uniforms.animTime = m_animTime;
    uniforms.animMovingCount = animationActive() ? m_animMovingCount : 0u;
    uniforms.animEntityOffset = m_animEntityOffset;
    uniforms.directEntityRange = directEntityRange ? 1u : 0u;
    context->UpdateBuffer(gpu.pTransformUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    context->SetPipelineState(gpu.pTransformPSO);
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs((count + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1));

    Diligent::IDeviceObject* written[kBarrierBufferCount] = {gpu.pWorldMatrixBuffer[m_currentFrame].RawPtr(), gpu.pTouchedEpochBuffer[m_currentFrame].RawPtr()};
    Diligent::StateTransitionDesc barriers[kBarrierBufferCount];
    for (int barrierIndex = 0; barrierIndex < kBarrierBufferCount; ++barrierIndex) {
        barriers[barrierIndex].pResource = written[barrierIndex];
        barriers[barrierIndex].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierIndex].NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierIndex].TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
        barriers[barrierIndex].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    }
    context->TransitionResourceStates(kBarrierBufferCount, barriers);
}

void Renderer::dispatchTransforms(const SceneDatabase& sceneDatabase, const TransformPlan& plan) {
    auto& gpu = *m_diligent;
    const std::vector<uint32_t>& levelCounts = plan.sparse ? m_transformWorkLevelCounts : sceneDatabase.m_levelCounts;
    auto* levelBinding = plan.sparse ? gpu.pTransformWorkSRB[m_currentFrame].RawPtr() : gpu.pTransformSRB[m_currentFrame].RawPtr();
    const bool forceLevelRecompute = plan.sparse || plan.hierarchyChanged || plan.bulkUpload || plan.localMatricesUploading;
    uint32_t levelOffset = 0;
    for (uint32_t level = 0; level < levelCounts.size(); ++level) {
        const uint32_t levelCount = levelCounts[level];
        const bool processLevel = plan.sparse || (level < m_levelNeedsProcessing.size() && m_levelNeedsProcessing[level]);
        if (levelCount > 0 && processLevel) dispatchTransformGroup(levelBinding, levelOffset, levelCount, forceLevelRecompute, false);
        levelOffset += levelCount;
    }

    if (!animationActive() || plan.forceAllLevels) return;
    const size_t transformCount = sceneDatabase.transforms.size();
    const auto animCount = static_cast<uint32_t>(std::min<size_t>(m_animMovingCount, transformCount - std::min<size_t>(m_animEntityOffset, transformCount)));
    if (animCount > 0) dispatchTransformGroup(gpu.pTransformSRB[m_currentFrame], m_animEntityOffset, animCount, true, true);
}
