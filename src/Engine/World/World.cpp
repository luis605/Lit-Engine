module;

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
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
    m_visible.reserve(count);
    m_mesh.reserve(count);
    m_firstChild.reserve(count);
    m_nextSibling.reserve(count);
    m_prevSibling.reserve(count);
    m_names.reserve(count);
}

void World::link(Entity idx, Entity parent) {
    Entity& head = parent == INVALID_ENTITY ? m_firstRoot : m_firstChild[parent];
    m_prevSibling[idx] = INVALID_ENTITY;
    m_nextSibling[idx] = head;
    if (head != INVALID_ENTITY) m_prevSibling[head] = idx;
    head = idx;
    m_db.hierarchies[idx].parent = parent;
}

void World::unlink(Entity idx) {
    const Entity parent = m_db.hierarchies[idx].parent;
    const Entity prev = m_prevSibling[idx];
    const Entity next = m_nextSibling[idx];
    if (prev != INVALID_ENTITY) {
        m_nextSibling[prev] = next;
    } else if (parent == INVALID_ENTITY) {
        m_firstRoot = next;
    } else {
        m_firstChild[parent] = next;
    }
    if (next != INVALID_ENTITY) m_prevSibling[next] = prev;
    m_prevSibling[idx] = INVALID_ENTITY;
    m_nextSibling[idx] = INVALID_ENTITY;
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
        m_firstChild[idx] = INVALID_ENTITY;
        m_nextSibling[idx] = INVALID_ENTITY;
        m_prevSibling[idx] = INVALID_ENTITY;
    } else {
        idx = m_db.createEntity();
        m_alive.push_back(1);
        m_generation.push_back(m_generationBase);
        m_visible.push_back(1);
        m_mesh.push_back(0);
        m_firstChild.push_back(INVALID_ENTITY);
        m_nextSibling.push_back(INVALID_ENTITY);
        m_prevSibling.push_back(INVALID_ENTITY);
        m_names.emplace_back();
    }
    ++m_aliveCount;

    m_db.transforms[idx].localMatrix = compose(desc.position, desc.rotation, desc.scale);
    auto& r = m_db.renderables[idx];
    m_visible[idx] = 1;
    m_mesh[idx] = desc.mesh;
    r.mesh_uuid = desc.mesh;
    r.material_uuid = desc.material;
    r.shaderId = desc.shader;
    r.alpha = desc.alpha;
    m_names[idx] = desc.name;
    link(idx, valid(desc.parent) ? desc.parent.index : INVALID_ENTITY);
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
    for (Entity child = m_firstChild[idx]; child != INVALID_ENTITY;) {
        const Entity next = m_nextSibling[child];
        destroyRecursive(child);
        child = next;
    }
    m_alive[idx] = 0;
    ++m_generation[idx];
    m_firstChild[idx] = INVALID_ENTITY;
    m_db.hierarchies[idx].parent = INVALID_ENTITY;
    m_visible[idx] = 0;
    m_db.renderables[idx].mesh_uuid = HIDDEN_MESH;
    m_names[idx].clear();
    m_freeList.push_back(idx);
    --m_aliveCount;
}

void World::destroy(EntityHandle e) {
    if (!valid(e)) return;
    unlink(e.index);
    destroyRecursive(e.index);
    touchStructure();
}

void World::setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform) {
    if (!valid(e)) return;
    if (!parent.isNull() && (!valid(parent) || parent == e || isDescendantOf(parent, e))) return;
    if (m_db.hierarchies[e.index].parent == parent.index) return;

    const glm::mat4 world = keepWorldTransform ? getWorldMatrix(e) : glm::mat4(1.0f);
    unlink(e.index);
    link(e.index, parent.index);
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
    for (Entity c = e.isNull() ? m_firstRoot : m_firstChild[e.index]; c != INVALID_ENTITY; c = m_nextSibling[c]) {
        out.push_back({c, m_generation[c]});
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

void World::setVisible(EntityHandle e, bool visible) {
    if (!valid(e) || (m_visible[e.index] != 0) == visible) return;
    m_visible[e.index] = visible ? 1 : 0;
    m_db.renderables[e.index].mesh_uuid = visible ? m_mesh[e.index] : HIDDEN_MESH;
    touchData();
}

bool World::isVisible(EntityHandle e) const { return valid(e) && m_visible[e.index]; }

uint32_t World::getMesh(EntityHandle e) const { return valid(e) ? m_mesh[e.index] : 0; }

void World::setMesh(EntityHandle e, uint32_t mesh) {
    if (!valid(e)) return;
    m_mesh[e.index] = mesh;
    if (m_visible[e.index]) {
        m_db.renderables[e.index].mesh_uuid = mesh;
        touchData();
    }
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

void World::clear() {
    m_db.transforms.clear();
    m_db.hierarchies.clear();
    m_db.renderables.clear();
    m_db.sortedHierarchyList.clear();
    m_db.clearDirty();
    m_db.m_dirtyStamp.clear();
    touchStructure();
    m_alive.clear();
    for (uint32_t g : m_generation) m_generationBase = std::max(m_generationBase, g + 1);
    m_generation.clear();
    m_visible.clear();
    m_mesh.clear();
    m_firstChild.clear();
    m_nextSibling.clear();
    m_prevSibling.clear();
    m_names.clear();
    m_freeList.clear();
    m_firstRoot = INVALID_ENTITY;
    m_aliveCount = 0;
}

bool World::saveScene(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out.precision(9);
    out << "LITSCENE 1\n" << m_aliveCount << "\n";
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (!m_alive[i]) continue;
        const auto& r = m_db.renderables[i];
        const Entity parent = m_db.hierarchies[i].parent;
        out << i << ' ' << (parent == INVALID_ENTITY ? -1 : static_cast<long long>(parent)) << ' ' << int(m_visible[i]) << ' ' << m_mesh[i] << ' ' << r.material_uuid << ' ' << r.shaderId << ' ' << r.alpha;
        const float* m = &m_db.transforms[i].localMatrix[0][0];
        for (int k = 0; k < 16; ++k) out << ' ' << m[k];
        out << '\t' << m_names[i] << '\n';
    }
    return static_cast<bool>(out);
}

bool World::loadScene(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string magic;
    int version = 0;
    size_t count = 0;
    in >> magic >> version >> count;
    if (!in || magic != "LITSCENE" || version != 1) return false;
    in.ignore(1, '\n');

    struct Record {
        long long id;
        long long parent;
    };
    std::vector<Record> records;
    std::unordered_map<long long, EntityHandle> handles;
    records.reserve(count);

    clear();
    reserve(count);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const size_t tab = line.find('\t');
        std::istringstream fields(line.substr(0, tab));
        long long id, parent;
        int visible;
        EntityDesc desc;
        fields >> id >> parent >> visible >> desc.mesh >> desc.material >> desc.shader >> desc.alpha;
        glm::mat4 local;
        float* m = &local[0][0];
        for (int k = 0; k < 16; ++k) fields >> m[k];
        if (!fields) {
            clear();
            return false;
        }
        if (tab != std::string::npos) desc.name = line.substr(tab + 1);
        const EntityHandle h = create(desc);
        setLocalMatrix(h, local);
        if (!visible) setVisible(h, false);
        handles[id] = h;
        records.push_back({id, parent});
    }

    for (const Record& r : records) {
        if (r.parent < 0) continue;
        const auto it = handles.find(r.parent);
        if (it != handles.end()) setParent(handles[r.id], it->second, false);
    }
    return true;
}
