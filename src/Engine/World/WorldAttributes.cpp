module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.glm;

namespace {

const std::string kEmptyText;
const RenderableComponent kEmptyRenderable{};

}

void World::setName(EntityHandle entity, std::string name) {
    if (valid(entity)) m_names.set(entity.index, std::move(name));
}

const std::string& World::getName(EntityHandle entity) const { return valid(entity) ? m_names.get(entity.index) : kEmptyText; }

EntityHandle World::find(std::string_view name) const {
    for (Entity index = 0; index < m_alive.size(); ++index) {
        if (m_alive[index] && m_names.get(index) == name) return {index, m_generation[index]};
    }
    return NULL_ENTITY;
}

std::vector<EntityHandle> World::findAll(std::string_view name) const {
    std::vector<EntityHandle> matches;
    for (Entity index = 0; index < m_alive.size(); ++index) {
        if (m_alive[index] && m_names.get(index) == name) matches.push_back({index, m_generation[index]});
    }
    return matches;
}

void World::clearTag(Entity index) {
    if (m_tags.get(index).empty()) return;
    const auto bucketIt = m_tagIndex.find(m_tags.get(index));
    if (bucketIt != m_tagIndex.end()) {
        auto& bucket = bucketIt->second;
        bucket.erase(std::remove(bucket.begin(), bucket.end(), index), bucket.end());
        if (bucket.empty()) m_tagIndex.erase(bucketIt);
    }
    m_tags.erase(index);
}

void World::setTag(EntityHandle entity, std::string tag) {
    if (!valid(entity)) return;
    clearTag(entity.index);
    if (tag.empty()) return;
    m_tagIndex[tag].push_back(entity.index);
    m_tags.set(entity.index, std::move(tag));
}

const std::string& World::getTag(EntityHandle entity) const { return valid(entity) ? m_tags.get(entity.index) : kEmptyText; }

std::vector<EntityHandle> World::findByTag(const std::string& tag) const {
    std::vector<EntityHandle> matches;
    const auto bucketIt = m_tagIndex.find(tag);
    if (bucketIt == m_tagIndex.end()) return matches;
    matches.reserve(bucketIt->second.size());
    for (Entity index : bucketIt->second) matches.push_back({index, m_generation[index]});
    std::sort(matches.begin(), matches.end(), [](EntityHandle left, EntityHandle right) { return left.index < right.index; });
    return matches;
}

void World::setLayer(EntityHandle entity, uint32_t mask) {
    if (valid(entity)) m_layer[entity.index] = mask;
}

uint32_t World::getLayer(EntityHandle entity) const { return valid(entity) ? m_layer[entity.index] : 0; }

void World::forEachInLayer(uint32_t mask, const std::function<void(EntityHandle)>& fn) const {
    for (Entity index = 0; index < m_alive.size(); ++index) {
        if (m_alive[index] && (m_layer[index] & mask)) fn({index, m_generation[index]});
    }
}

void World::setFlagBit(Entity index, uint32_t bit, bool on) {
    uint32_t& flags = m_db.renderables[index].flags;
    flags = on ? (flags | bit) : (flags & ~bit);
}

void World::setRenderFlags(EntityHandle entity, uint32_t flags, bool enabled) {
    if (!valid(entity)) return;
    const uint32_t keepHidden = m_visible[entity.index] ? 0u : RENDER_HIDDEN;
    uint32_t& current = m_db.renderables[entity.index].flags;
    current = enabled ? (current | flags) : (current & ~flags);
    current |= keepHidden;
    touchRenderable(entity.index);
}

uint32_t World::getRenderFlags(EntityHandle entity) const {
    return valid(entity) ? m_db.renderables[entity.index].flags : 0;
}

void World::setVisible(EntityHandle entity, bool visible) {
    if (!valid(entity) || (m_visible[entity.index] != 0) == visible) return;
    m_visible[entity.index] = visible ? 1 : 0;
    queueSpatial(entity.index);
    setFlagBit(entity.index, RENDER_HIDDEN, !visible);
    touchRenderable(entity.index);
}

bool World::isVisible(EntityHandle entity) const { return valid(entity) && m_visible[entity.index]; }

uint32_t World::getMesh(EntityHandle entity) const { return valid(entity) ? m_mesh[entity.index] : 0; }

void World::setMesh(EntityHandle entity, uint32_t mesh) {
    if (!valid(entity)) return;
    m_mesh[entity.index] = mesh;
    m_db.renderables[entity.index].mesh_uuid = mesh;
    queueSpatial(entity.index);
    touchRenderable(entity.index);
}

void World::setMaterial(EntityHandle entity, uint32_t material) {
    if (!valid(entity)) return;
    m_db.renderables[entity.index].material_uuid = material;
    m_db.renderableDirty.mark(entity.index);
}

void World::setShader(EntityHandle entity, uint32_t shader) {
    if (!valid(entity)) return;
    m_db.renderables[entity.index].shaderId = shader;
    m_db.renderableDirty.mark(entity.index);
}

void World::setAlpha(EntityHandle entity, float alpha) {
    if (!valid(entity)) return;
    m_db.renderables[entity.index].alpha = alpha;
    m_db.renderableDirty.mark(entity.index);
}

const RenderableComponent& World::getRenderable(EntityHandle entity) const {
    return valid(entity) ? m_db.renderables[entity.index] : kEmptyRenderable;
}
