#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.renderer;
import Engine.World;
import Engine.Render.entity;
import Engine.Instancing;
import Engine.glm;

// GPU instancing, engine side (docs/Instancing.md): InstanceBatch components are the source of truth (editor, scene files,
// snapshots, deltas); syncInstanceBatches mirrors them into the renderer every frame (new and destroyed components, owner
// entity index after compaction, visibility, dirty ranges, layout changes).

EntityHandle Engine::createInstanceBatch(uint32_t mesh, uint32_t material, std::span<const InstanceData> instances, const InstanceBatchSettings& settings, uint32_t shader, const glm::vec3& position, EntityHandle parent) {
    EntityDesc desc;
    desc.name = "InstanceBatch";
    desc.position = position;
    desc.parent = parent;
    const EntityHandle entity = m_world.create(desc);
    InstanceBatch batch;
    batch.mesh = mesh;
    batch.material = material;
    batch.shader = shader;
    batch.settings = settings;
    batch.assign(instances);
    m_world.add<InstanceBatch>(entity, std::move(batch));
    return entity;
}

bool Engine::updateInstances(EntityHandle entity, uint32_t first, std::span<const InstanceData> instances) {
    InstanceBatch* batch = m_world.get<InstanceBatch>(entity);
    if (!batch) return false;
    batch->update(first, instances);
    return true;
}

bool Engine::removeInstance(EntityHandle entity, uint32_t index) {
    InstanceBatch* batch = m_world.get<InstanceBatch>(entity);
    return batch && batch->remove(index);
}

std::optional<uint64_t> Engine::swapRemoveInstance(EntityHandle entity, uint32_t index) {
    InstanceBatch* batch = m_world.get<InstanceBatch>(entity);
    if (!batch) return std::nullopt;
    return batch->swapRemove(index);
}

void Engine::destroyInstanceBatch(EntityHandle entity) {
    if (m_world.isAlive(entity)) m_world.destroy(entity);
}

void Engine::syncInstanceBatches(World& world) {
    if (!m_renderer.initialized()) return;
    const uint64_t epoch = ++m_instanceEpoch;
    world.view<InstanceBatch>([&](EntityHandle handle, InstanceBatch& batch) {
        InstanceMirror& mirror = m_instanceMirrors[handle.index];
        if (mirror.batch != 0xFFFFFFFFu && mirror.generation != handle.generation) {
            m_renderer.destroyInstanceBatch(mirror.batch);
            mirror.batch = 0xFFFFFFFFu;
        }
        if (mirror.batch == 0xFFFFFFFFu) {
            mirror.batch = m_renderer.createInstanceBatch();
            mirror.generation = handle.generation;
        }
        mirror.epoch = epoch;
        m_renderer.syncInstanceBatch(mirror.batch, batch, handle.index, world.isVisible(handle));
        batch.clearDirty();
    });
    for (auto it = m_instanceMirrors.begin(); it != m_instanceMirrors.end();) {
        if (it->second.epoch == epoch) {
            ++it;
            continue;
        }
        if (it->second.batch != 0xFFFFFFFFu) m_renderer.destroyInstanceBatch(it->second.batch);
        it = m_instanceMirrors.erase(it);
    }
}
