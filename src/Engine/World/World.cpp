module;

#include <cmath>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.glm;

#include "Engine/World/WorldInternal.hpp"

World::World() = default;

bool World::isAlive(EntityHandle entity) const { return valid(entity); }

EntityHandle World::handleOf(Entity index) const {
    if (index >= m_alive.size() || !m_alive[index]) return NULL_ENTITY;
    return {index, m_generation[index]};
}

EntityBuilder World::spawn() { return EntityBuilder(*this); }

void World::reserve(size_t count) {
    m_db.transforms.reserve(count);
    m_db.hierarchies.reserve(count);
    m_db.renderables.reserve(count);
    m_alive.reserve(count);
    m_generation.reserve(count);
    m_visible.reserve(count);
    m_mesh.reserve(count);
    m_layer.reserve(count);
    m_position.reserve(count);
    m_tags.reserve(count);
    m_db.firstChild.reserve(count);
    m_db.nextSibling.reserve(count);
    m_prevSibling.reserve(count);
    m_names.reserve(count);
}

void World::link(Entity index, Entity parent) {
    Entity& head = parent == INVALID_ENTITY ? m_firstRoot : m_db.firstChild[parent];
    m_prevSibling[index] = INVALID_ENTITY;
    m_db.nextSibling[index] = head;
    if (head != INVALID_ENTITY) m_prevSibling[head] = index;
    head = index;
    m_db.hierarchies[index].parent = parent;
    m_db.hierarchies[index].level = parent == INVALID_ENTITY ? 0 : m_db.hierarchies[parent].level + 1;
}

void World::relevelSubtree(Entity root) {
    m_relevelStack.assign(1, root);
    while (!m_relevelStack.empty()) {
        const Entity current = m_relevelStack.back();
        m_relevelStack.pop_back();
        const Entity parent = m_db.hierarchies[current].parent;
        m_db.hierarchies[current].level = parent == INVALID_ENTITY ? 0 : m_db.hierarchies[parent].level + 1;
        m_db.structureDirty.mark(current);
        m_db.forEachChild(current, [&](Entity child) { m_relevelStack.push_back(child); });
    }
    m_db.markStructureChanged();
}

void World::unlink(Entity index) {
    const Entity parent = m_db.hierarchies[index].parent;
    const Entity prev = m_prevSibling[index];
    const Entity next = m_db.nextSibling[index];
    if (prev != INVALID_ENTITY) {
        m_db.nextSibling[prev] = next;
    } else if (parent == INVALID_ENTITY) {
        m_firstRoot = next;
    } else {
        m_db.firstChild[parent] = next;
    }
    if (next != INVALID_ENTITY) m_prevSibling[next] = prev;
    m_prevSibling[index] = INVALID_ENTITY;
    m_db.nextSibling[index] = INVALID_ENTITY;
}

void World::touchStructure() {
    m_db.markHierarchyDirty();
    m_db.markDataDirty();
}

void World::touchRenderable(Entity index) {
    m_db.renderableDirty.mark(index);
    m_db.transformDirty.mark(index);
}

EntityHandle World::create(const EntityDesc& desc) {
    const EntityHandle handle = createImpl(desc);
    m_db.structureDirty.mark(handle.index);
    touchRenderable(handle.index);
    m_db.markStructureChanged();
    m_events.emit(EntityCreated{handle});
    return handle;
}

EntityHandle World::create(std::string name, uint32_t mesh, const glm::vec3& position, const glm::vec3& scale, EntityHandle parent) {
    EntityDesc desc;
    desc.name = std::move(name);
    desc.mesh = mesh;
    desc.position = position;
    desc.scale = scale;
    desc.parent = parent;
    return create(desc);
}

EntityHandle World::createImpl(const EntityDesc& desc) {
    Entity index;
    if (!m_freeList.empty()) {
        index = m_freeList.back();
        m_freeList.pop_back();
        m_db.transforms[index] = TransformComponent{};
        m_db.hierarchies[index] = HierarchyComponent{INVALID_ENTITY, 0};
        m_db.renderables[index] = RenderableComponent{};
        m_db.renderables[index].objectId = index;
        m_alive[index] = 1;
        m_db.firstChild[index] = INVALID_ENTITY;
        m_db.nextSibling[index] = INVALID_ENTITY;
        m_prevSibling[index] = INVALID_ENTITY;
    } else {
        index = m_db.createEntity();
        m_alive.push_back(1);
        m_generation.push_back(m_generationBase);
        m_visible.push_back(1);
        m_mesh.push_back(0);
        m_layer.push_back(1);
        m_position.emplace_back(0.0f);
        m_tags.grow();
        m_prevSibling.push_back(INVALID_ENTITY);
        m_names.grow();
    }
    ++m_aliveCount;

    m_db.transforms[index].localMatrix = composeTransform(desc.position, desc.rotation, desc.scale);
    syncPosition(index);
    if (index < m_worldDirty.size()) m_worldDirty[index] = 1;
    RenderableComponent& renderable = m_db.renderables[index];
    m_visible[index] = 1;
    m_layer[index] = 1;
    m_mesh[index] = desc.mesh;
    renderable.mesh_uuid = desc.mesh;
    renderable.material_uuid = desc.material;
    renderable.shaderId = desc.shader;
    renderable.alpha = desc.alpha;
    m_names.set(index, desc.name);
    link(index, valid(desc.parent) ? desc.parent.index : INVALID_ENTITY);
    queueSpatial(index);
    return {index, m_generation[index]};
}

void World::destroyRecursive(Entity index) {
    for (Entity child = m_db.firstChild[index]; child != INVALID_ENTITY;) {
        const Entity next = m_db.nextSibling[child];
        destroyRecursive(child);
        child = next;
    }
    m_events.emit(EntityDestroyed{{index, m_generation[index]}});
    queueSpatial(index);
    runScriptDestroy(index);
    clearTag(index);
    for (auto& [type, componentPool] : m_pools) componentPool->remove(index);
    m_alive[index] = 0;
    ++m_generation[index];
    m_db.firstChild[index] = INVALID_ENTITY;
    m_db.hierarchies[index] = HierarchyComponent{INVALID_ENTITY, 0};
    m_visible[index] = 0;
    m_db.renderables[index].flags |= RENDER_HIDDEN;
    m_db.structureDirty.mark(index);
    touchRenderable(index);
    m_names.erase(index);
    m_freeList.push_back(index);
    --m_aliveCount;
}

void World::destroy(EntityHandle entity) {
    if (!valid(entity)) return;
    if (m_updating) {
        m_pendingDestroy.push_back(entity);
        return;
    }
    unlink(entity.index);
    destroyRecursive(entity.index);
    trimTail();
    m_db.markStructureChanged();
}

void World::destroyBatch(std::span<const EntityHandle> entities) {
    if (m_updating) {
        for (EntityHandle entity : entities) {
            if (valid(entity)) m_pendingDestroy.push_back(entity);
        }
        return;
    }
    m_freeList.reserve(m_freeList.size() + entities.size());
    bool destroyedAny = false;
    for (EntityHandle entity : entities) {
        if (!valid(entity)) continue;
        unlink(entity.index);
        destroyRecursive(entity.index);
        destroyedAny = true;
    }
    if (!destroyedAny) return;
    trimTail();
    touchStructure();
}

void World::shrinkSlots(size_t count) {
    if (count >= m_alive.size()) return;
    for (size_t slot = count; slot < m_alive.size(); ++slot) {
        if (m_spatialActive && slot < m_spatial.size()) removeSpatialEntry(static_cast<Entity>(slot));
        m_generationBase = std::max(m_generationBase, m_generation[slot] + 1);
    }
    m_db.transforms.resize(count);
    m_db.hierarchies.resize(count);
    m_db.renderables.resize(count);
    m_db.transformDirty.truncate(count);
    m_db.renderableDirty.truncate(count);
    m_db.structureDirty.truncate(count);
    m_db.markStructureChanged();
    m_alive.resize(count);
    m_generation.resize(count);
    m_visible.resize(count);
    m_mesh.resize(count);
    m_layer.resize(count);
    m_position.resize(count);
    m_tags.resize(count);
    m_names.resize(count);
    m_db.firstChild.resize(count);
    m_db.nextSibling.resize(count);
    m_prevSibling.resize(count);
    if (m_spatial.size() > count) m_spatial.resize(count);
    if (m_spatialQueued.size() > count) m_spatialQueued.resize(count);
    if (m_spatialRootQueued.size() > count) m_spatialRootQueued.resize(count);
    if (m_worldCache.size() > count) {
        m_worldCache.resize(count);
        m_worldDirty.resize(count);
    }
    std::erase_if(m_freeList, [count](Entity freeSlot) { return freeSlot >= count; });
}

void World::trimTail() {
    size_t count = m_alive.size();
    while (count > 0 && !m_alive[count - 1]) --count;
    shrinkSlots(count);
}

void World::moveEntity(Entity from, Entity to) {
    m_db.transforms[to] = m_db.transforms[from];
    m_db.hierarchies[to] = m_db.hierarchies[from];
    m_db.renderables[to] = m_db.renderables[from];
    m_db.renderables[to].objectId = to;
    m_position[to] = m_position[from];
    m_alive[to] = 1;
    m_visible[to] = m_visible[from];
    m_mesh[to] = m_mesh[from];
    m_layer[to] = m_layer[from];
    m_names.move(from, to);

    if (!m_tags.get(from).empty()) {
        auto& bucket = m_tagIndex[m_tags.get(from)];
        std::replace(bucket.begin(), bucket.end(), from, to);
        m_tags.move(from, to);
    }

    const Entity parent = m_db.hierarchies[from].parent;
    const Entity prev = m_prevSibling[from];
    const Entity next = m_db.nextSibling[from];
    m_prevSibling[to] = prev;
    m_db.nextSibling[to] = next;
    if (prev != INVALID_ENTITY) {
        m_db.nextSibling[prev] = to;
    } else if (parent == INVALID_ENTITY) {
        m_firstRoot = to;
    } else {
        m_db.firstChild[parent] = to;
    }
    if (next != INVALID_ENTITY) m_prevSibling[next] = to;
    m_db.firstChild[to] = m_db.firstChild[from];
    for (Entity child = m_db.firstChild[to]; child != INVALID_ENTITY; child = m_db.nextSibling[child]) m_db.hierarchies[child].parent = to;

    for (auto& [type, componentPool] : m_pools) componentPool->rename(from, to);
    if (const auto scriptsIt = m_scripts.find(from); scriptsIt != m_scripts.end()) {
        auto scripts = std::move(scriptsIt->second);
        m_scripts.erase(scriptsIt);
        m_scripts[to] = std::move(scripts);
    }
    if (m_activeCamera.index == from) m_activeCamera = {to, m_generation[to]};

    if (m_spatialActive && from < m_spatial.size()) removeSpatialEntry(from);
    if (to < m_worldDirty.size()) m_worldDirty[to] = 0;
    invalidateWorld(to);
    markSpatialSubtree(to);

    m_alive[from] = 0;
    m_db.firstChild[from] = INVALID_ENTITY;
    m_db.nextSibling[from] = INVALID_ENTITY;
    m_prevSibling[from] = INVALID_ENTITY;
}

std::vector<EntityMoved> World::compact() {
    std::vector<EntityMoved> moved;
    if (m_updating) return moved;

    m_freeList.clear();
    size_t hole = 0;
    size_t tail = m_alive.size();
    while (true) {
        while (hole < tail && m_alive[hole]) ++hole;
        while (tail > hole && !m_alive[tail - 1]) --tail;
        if (hole >= tail || tail == 0) break;
        const Entity from = static_cast<Entity>(tail - 1);
        const Entity to = static_cast<Entity>(hole);
        const EntityHandle oldHandle{from, m_generation[from]};
        moveEntity(from, to);
        ++m_generation[from];
        moved.push_back({oldHandle, {to, m_generation[to]}});
        --tail;
    }
    shrinkSlots(m_aliveCount);
    touchStructure();
    for (const EntityMoved& movement : moved) m_events.emit(movement);
    return moved;
}

void World::setParent(EntityHandle entity, EntityHandle parent, bool keepWorldTransform) {
    if (!valid(entity)) return;
    if (!parent.isNull() && (!valid(parent) || parent == entity || isDescendantOf(parent, entity))) return;
    if (m_db.hierarchies[entity.index].parent == parent.index) return;

    const EntityHandle oldParent = getParent(entity);
    const glm::mat4 world = keepWorldTransform ? getWorldMatrix(entity) : glm::mat4(1.0f);
    unlink(entity.index);
    link(entity.index, parent.index);
    relevelSubtree(entity.index);
    m_db.transformDirty.mark(entity.index);
    invalidateWorld(entity.index);
    markSpatialSubtree(entity.index);
    if (keepWorldTransform) {
        const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : getWorldMatrix(parent);
        m_db.transforms[entity.index].localMatrix = glm::inverse(parentWorld) * world;
        syncPosition(entity.index);
    }
    m_events.emit(EntityReparented{entity, oldParent, parent});
}

EntityHandle World::getParent(EntityHandle entity) const {
    if (!valid(entity)) return NULL_ENTITY;
    return handleOf(m_db.hierarchies[entity.index].parent);
}

std::vector<EntityHandle> World::getChildren(EntityHandle entity) const {
    std::vector<EntityHandle> children;
    if (!entity.isNull() && !valid(entity)) return children;
    for (Entity child = entity.isNull() ? m_firstRoot : m_db.firstChild[entity.index]; child != INVALID_ENTITY; child = m_db.nextSibling[child]) {
        children.push_back({child, m_generation[child]});
    }
    return children;
}

std::vector<EntityHandle> World::getRoots() const { return getChildren(NULL_ENTITY); }

bool World::isDescendantOf(EntityHandle entity, EntityHandle ancestor) const {
    for (EntityHandle current = getParent(entity); !current.isNull(); current = getParent(current)) {
        if (current == ancestor) return true;
    }
    return false;
}

void World::forEach(const std::function<void(EntityHandle)>& fn) const {
    for (Entity index = 0; index < m_alive.size(); ++index) {
        if (m_alive[index]) fn({index, m_generation[index]});
    }
}
