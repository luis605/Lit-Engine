module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.glm;

namespace {
const std::string kEmptyName;
const glm::mat4 kDegenerate(0.0f);

glm::mat4 compose(const glm::vec3& p, const glm::quat& r, const glm::vec3& s) {
    return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}
}

World::World() = default;

bool World::valid(EntityHandle h) const {
    return h.index < m_alive.size() && m_alive[h.index] && m_generation[h.index] == h.generation;
}

bool World::isAlive(EntityHandle e) const { return valid(e); }

EntityHandle World::handleOf(Entity index) const {
    if (index >= m_alive.size() || !m_alive[index]) return NULL_ENTITY;
    return {index, m_generation[index]};
}

void World::reserve(size_t count) {
    m_db.transforms.reserve(count);
    m_db.hierarchies.reserve(count);
    m_db.renderables.reserve(count);
    m_alive.reserve(count);
    m_generation.reserve(count);
    m_names.reserve(count);
}

void World::touchTransform(Entity idx) { m_db.markEntityDirty(idx); }

void World::touchStructure() {
    m_db.markHierarchyDirty();
    m_db.markDataDirty();
}

void World::touchData() { m_db.markDataDirty(); }

EntityHandle World::create(const EntityDesc& desc) {
    Entity idx;
    if (!m_freeList.empty()) {
        idx = m_freeList.back();
        m_freeList.pop_back();
        m_db.transforms[idx] = TransformComponent{};
        m_db.hierarchies[idx] = HierarchyComponent{INVALID_ENTITY, 0};
        m_db.renderables[idx] = RenderableComponent{};
        m_db.renderables[idx].objectId = idx;
        m_alive[idx] = 1;
    } else {
        idx = m_db.createEntity();
        m_alive.push_back(1);
        m_generation.push_back(0);
        m_names.emplace_back();
    }
    ++m_aliveCount;

    m_db.transforms[idx].localMatrix = compose(desc.position, desc.rotation, desc.scale);
    auto& r = m_db.renderables[idx];
    r.mesh_uuid = desc.mesh;
    r.material_uuid = desc.material;
    r.shaderId = desc.shader;
    r.alpha = desc.alpha;
    m_names[idx] = desc.name;
    m_db.hierarchies[idx].parent = valid(desc.parent) ? desc.parent.index : INVALID_ENTITY;
    touchStructure();
    return {idx, m_generation[idx]};
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

void World::destroyRecursive(Entity idx) {
    for (EntityHandle child : getChildren({idx, m_generation[idx]})) destroyRecursive(child.index);
    m_alive[idx] = 0;
    ++m_generation[idx];
    m_db.hierarchies[idx].parent = INVALID_ENTITY;
    m_db.transforms[idx].localMatrix = kDegenerate;
    m_db.renderables[idx].alpha = 0.0f;
    m_names[idx].clear();
    m_freeList.push_back(idx);
    --m_aliveCount;
}

void World::destroy(EntityHandle e) {
    if (!valid(e)) return;
    destroyRecursive(e.index);
    touchStructure();
}

void World::setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform) {
    if (!valid(e)) return;
    if (!parent.isNull() && (!valid(parent) || parent == e || isDescendantOf(parent, e))) return;
    if (m_db.hierarchies[e.index].parent == parent.index) return;

    const glm::mat4 world = keepWorldTransform ? getWorldMatrix(e) : glm::mat4(1.0f);
    m_db.hierarchies[e.index].parent = parent.index;
    if (keepWorldTransform) {
        const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : getWorldMatrix(parent);
        m_db.transforms[e.index].localMatrix = glm::inverse(parentWorld) * world;
    }
    touchStructure();
}

EntityHandle World::getParent(EntityHandle e) const {
    if (!valid(e)) return NULL_ENTITY;
    return handleOf(m_db.hierarchies[e.index].parent);
}

std::vector<EntityHandle> World::getChildren(EntityHandle e) const {
    std::vector<EntityHandle> out;
    if (!e.isNull() && !valid(e)) return out;
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && m_db.hierarchies[i].parent == e.index) out.push_back({i, m_generation[i]});
    }
    return out;
}

std::vector<EntityHandle> World::getRoots() const { return getChildren(NULL_ENTITY); }

bool World::isDescendantOf(EntityHandle e, EntityHandle ancestor) const {
    for (EntityHandle p = getParent(e); !p.isNull(); p = getParent(p)) {
        if (p == ancestor) return true;
    }
    return false;
}

void World::setName(EntityHandle e, std::string name) {
    if (valid(e)) m_names[e.index] = std::move(name);
}

const std::string& World::getName(EntityHandle e) const { return valid(e) ? m_names[e.index] : kEmptyName; }

EntityHandle World::find(std::string_view name) const {
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && m_names[i] == name) return {i, m_generation[i]};
    }
    return NULL_ENTITY;
}

std::vector<EntityHandle> World::findAll(std::string_view name) const {
    std::vector<EntityHandle> out;
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && m_names[i] == name) out.push_back({i, m_generation[i]});
    }
    return out;
}

void World::setLocalMatrix(EntityHandle e, const glm::mat4& local) {
    if (!valid(e)) return;
    m_db.transforms[e.index].localMatrix = local;
    touchTransform(e.index);
}

void World::setPosition(EntityHandle e, const glm::vec3& position) {
    if (!valid(e)) return;
    m_db.transforms[e.index].setPos(position);
    touchTransform(e.index);
}

void World::setRotation(EntityHandle e, const glm::quat& rotation) {
    if (!valid(e)) return;
    m_db.transforms[e.index].setRot(rotation);
    touchTransform(e.index);
}

void World::setScale(EntityHandle e, const glm::vec3& scale) {
    if (!valid(e)) return;
    m_db.transforms[e.index].setScale(scale);
    touchTransform(e.index);
}

void World::translate(EntityHandle e, const glm::vec3& delta) {
    if (!valid(e)) return;
    auto& t = m_db.transforms[e.index];
    t.setPos(t.getPos() + delta);
    touchTransform(e.index);
}

void World::setWorldMatrix(EntityHandle e, const glm::mat4& world) {
    if (!valid(e)) return;
    const EntityHandle parent = getParent(e);
    const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : getWorldMatrix(parent);
    setLocalMatrix(e, glm::inverse(parentWorld) * world);
}

const glm::mat4& World::getLocalMatrix(EntityHandle e) const {
    return valid(e) ? m_db.transforms[e.index].localMatrix : kDegenerate;
}

glm::mat4 World::getWorldMatrix(EntityHandle e) const {
    if (!valid(e)) return glm::mat4(1.0f);
    glm::mat4 m = m_db.transforms[e.index].localMatrix;
    for (Entity p = m_db.hierarchies[e.index].parent; p != INVALID_ENTITY && m_alive[p]; p = m_db.hierarchies[p].parent) {
        m = m_db.transforms[p].localMatrix * m;
    }
    return m;
}

glm::vec3 World::getPosition(EntityHandle e) const {
    return valid(e) ? m_db.transforms[e.index].getPos() : glm::vec3(0.0f);
}

glm::quat World::getRotation(EntityHandle e) const {
    return valid(e) ? m_db.transforms[e.index].getRot() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 World::getScale(EntityHandle e) const {
    return valid(e) ? m_db.transforms[e.index].getScale() : glm::vec3(0.0f);
}

glm::vec3 World::getWorldPosition(EntityHandle e) const { return glm::vec3(getWorldMatrix(e)[3]); }

void World::setMesh(EntityHandle e, uint32_t mesh) {
    if (!valid(e)) return;
    m_db.renderables[e.index].mesh_uuid = mesh;
    touchData();
}

void World::setMaterial(EntityHandle e, uint32_t material) {
    if (!valid(e)) return;
    m_db.renderables[e.index].material_uuid = material;
    touchData();
}

void World::setShader(EntityHandle e, uint32_t shader) {
    if (!valid(e)) return;
    m_db.renderables[e.index].shaderId = shader;
    touchData();
}

void World::setAlpha(EntityHandle e, float alpha) {
    if (!valid(e)) return;
    m_db.renderables[e.index].alpha = alpha;
    touchData();
}

const RenderableComponent& World::getRenderable(EntityHandle e) const {
    static const RenderableComponent kNone{};
    return valid(e) ? m_db.renderables[e.index] : kNone;
}

void World::forEach(const std::function<void(EntityHandle)>& fn) const {
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i]) fn({i, m_generation[i]});
    }
}
