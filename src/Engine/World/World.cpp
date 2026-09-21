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

bool World::isAlive(Entity e) const {
    return e < m_alive.size() && m_alive[e];
}

void World::touchTransform(Entity e) {
    m_db.markEntityDirty(e);
    m_db.markTransformsDirty(m_db.m_movingCount);
}

void World::touchStructure() {
    m_db.markHierarchyDirty();
    m_db.markDataDirty();
}

void World::touchData() {
    m_db.markDataDirty();
}

Entity World::create(const EntityDesc& desc) {
    Entity e;
    if (!m_freeList.empty()) {
        e = m_freeList.back();
        m_freeList.pop_back();
        m_db.transforms[e] = TransformComponent{};
        m_db.hierarchies[e] = HierarchyComponent{INVALID_ENTITY, 0};
        m_db.renderables[e] = RenderableComponent{};
        m_db.renderables[e].objectId = e;
        m_alive[e] = true;
        m_names[e].clear();
        touchStructure();
    } else {
        e = m_db.createEntity();
        m_alive.push_back(true);
        m_names.emplace_back();
    }
    ++m_aliveCount;

    m_db.transforms[e].localMatrix = compose(desc.position, desc.rotation, desc.scale);
    auto& r = m_db.renderables[e];
    r.mesh_uuid = desc.mesh;
    r.material_uuid = desc.material;
    r.shaderId = desc.shader;
    r.alpha = desc.alpha;
    m_names[e] = desc.name;
    m_db.hierarchies[e].parent = isAlive(desc.parent) ? desc.parent : INVALID_ENTITY;
    touchStructure();
    return e;
}

Entity World::create(std::string name, uint32_t mesh, const glm::vec3& position, const glm::vec3& scale, Entity parent) {
    EntityDesc desc;
    desc.name = std::move(name);
    desc.mesh = mesh;
    desc.position = position;
    desc.scale = scale;
    desc.parent = parent;
    return create(desc);
}

void World::destroyRecursive(Entity e) {
    for (Entity child : getChildren(e)) destroyRecursive(child);
    m_alive[e] = false;
    m_db.hierarchies[e].parent = INVALID_ENTITY;
    m_db.transforms[e].localMatrix = kDegenerate;
    m_db.renderables[e].alpha = 0.0f;
    m_names[e].clear();
    m_freeList.push_back(e);
    --m_aliveCount;
}

void World::destroy(Entity e) {
    if (!isAlive(e)) return;
    destroyRecursive(e);
    touchStructure();
}

void World::setParent(Entity e, Entity parent, bool keepWorldTransform) {
    if (!isAlive(e)) return;
    if (parent != INVALID_ENTITY && (!isAlive(parent) || parent == e || isDescendantOf(parent, e))) return;
    if (m_db.hierarchies[e].parent == parent) return;

    const glm::mat4 world = keepWorldTransform ? getWorldMatrix(e) : glm::mat4(1.0f);
    m_db.hierarchies[e].parent = parent;
    if (keepWorldTransform) {
        const glm::mat4 parentWorld = parent == INVALID_ENTITY ? glm::mat4(1.0f) : getWorldMatrix(parent);
        m_db.transforms[e].localMatrix = glm::inverse(parentWorld) * world;
    }
    touchStructure();
}

Entity World::getParent(Entity e) const {
    return isAlive(e) ? m_db.hierarchies[e].parent : INVALID_ENTITY;
}

std::vector<Entity> World::getChildren(Entity e) const {
    std::vector<Entity> out;
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && m_db.hierarchies[i].parent == e) out.push_back(i);
    }
    return out;
}

std::vector<Entity> World::getRoots() const {
    return getChildren(INVALID_ENTITY);
}

bool World::isDescendantOf(Entity e, Entity ancestor) const {
    for (Entity p = getParent(e); p != INVALID_ENTITY; p = getParent(p)) {
        if (p == ancestor) return true;
    }
    return false;
}

void World::setName(Entity e, std::string name) {
    if (isAlive(e)) m_names[e] = std::move(name);
}

const std::string& World::getName(Entity e) const {
    return isAlive(e) ? m_names[e] : kEmptyName;
}

Entity World::find(std::string_view name) const {
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && m_names[i] == name) return i;
    }
    return INVALID_ENTITY;
}

std::vector<Entity> World::findAll(std::string_view name) const {
    std::vector<Entity> out;
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && m_names[i] == name) out.push_back(i);
    }
    return out;
}

void World::setLocalMatrix(Entity e, const glm::mat4& local) {
    if (!isAlive(e)) return;
    m_db.transforms[e].localMatrix = local;
    touchTransform(e);
}

void World::setPosition(Entity e, const glm::vec3& position) {
    if (!isAlive(e)) return;
    m_db.transforms[e].setPos(position);
    touchTransform(e);
}

void World::setRotation(Entity e, const glm::quat& rotation) {
    if (!isAlive(e)) return;
    m_db.transforms[e].setRot(rotation);
    touchTransform(e);
}

void World::setScale(Entity e, const glm::vec3& scale) {
    if (!isAlive(e)) return;
    m_db.transforms[e].setScale(scale);
    touchTransform(e);
}

void World::translate(Entity e, const glm::vec3& delta) {
    if (!isAlive(e)) return;
    m_db.transforms[e].setPos(m_db.transforms[e].getPos() + delta);
    touchTransform(e);
}

void World::setWorldMatrix(Entity e, const glm::mat4& world) {
    if (!isAlive(e)) return;
    const Entity parent = m_db.hierarchies[e].parent;
    const glm::mat4 parentWorld = parent == INVALID_ENTITY ? glm::mat4(1.0f) : getWorldMatrix(parent);
    setLocalMatrix(e, glm::inverse(parentWorld) * world);
}

const glm::mat4& World::getLocalMatrix(Entity e) const {
    return isAlive(e) ? m_db.transforms[e].localMatrix : kDegenerate;
}

glm::mat4 World::getWorldMatrix(Entity e) const {
    if (!isAlive(e)) return glm::mat4(1.0f);
    glm::mat4 m = m_db.transforms[e].localMatrix;
    for (Entity p = m_db.hierarchies[e].parent; p != INVALID_ENTITY && isAlive(p); p = m_db.hierarchies[p].parent) {
        m = m_db.transforms[p].localMatrix * m;
    }
    return m;
}

glm::vec3 World::getPosition(Entity e) const {
    return isAlive(e) ? m_db.transforms[e].getPos() : glm::vec3(0.0f);
}

glm::quat World::getRotation(Entity e) const {
    return isAlive(e) ? m_db.transforms[e].getRot() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 World::getScale(Entity e) const {
    return isAlive(e) ? m_db.transforms[e].getScale() : glm::vec3(0.0f);
}

glm::vec3 World::getWorldPosition(Entity e) const {
    return glm::vec3(getWorldMatrix(e)[3]);
}

void World::setMesh(Entity e, uint32_t mesh) {
    if (!isAlive(e)) return;
    m_db.renderables[e].mesh_uuid = mesh;
    touchData();
}

void World::setMaterial(Entity e, uint32_t material) {
    if (!isAlive(e)) return;
    m_db.renderables[e].material_uuid = material;
    touchData();
}

void World::setShader(Entity e, uint32_t shader) {
    if (!isAlive(e)) return;
    m_db.renderables[e].shaderId = shader;
    touchData();
}

void World::setAlpha(Entity e, float alpha) {
    if (!isAlive(e)) return;
    m_db.renderables[e].alpha = alpha;
    touchData();
}

const RenderableComponent& World::getRenderable(Entity e) const {
    return m_db.renderables[e];
}

void World::forEach(const std::function<void(Entity)>& fn) const {
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i]) fn(i);
    }
}
