module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <unordered_map>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.camera;
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
    m_layer.reserve(count);
    m_tags.reserve(count);
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

void World::touchTransform(Entity idx) {
    m_db.markEntityDirty(idx);
    invalidateWorld(idx);
    markSpatialSubtree(idx);
}

void World::invalidateWorld(Entity idx) const {
    if (m_worldCache.empty() || idx >= m_worldDirty.size()) return;
    m_worldStack.clear();
    m_worldStack.push_back(idx);
    while (!m_worldStack.empty()) {
        const Entity e = m_worldStack.back();
        m_worldStack.pop_back();
        if (e >= m_worldDirty.size() || m_worldDirty[e]) continue;
        m_worldDirty[e] = 1;
        for (Entity c = m_firstChild[e]; c != INVALID_ENTITY; c = m_nextSibling[c]) m_worldStack.push_back(c);
    }
}

void World::touchStructure() {
    m_db.markHierarchyDirty();
    m_db.markDataDirty();
}

void World::touchData() { m_db.markDataDirty(); }

EntityHandle World::create(const EntityDesc& desc) {
    const EntityHandle h = createImpl(desc);
    touchStructure();
    m_events.emit(EntityCreated{h});
    return h;
}

EntityHandle World::createImpl(const EntityDesc& desc) {
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
        m_layer.push_back(1);
        m_tags.emplace_back();
        m_firstChild.push_back(INVALID_ENTITY);
        m_nextSibling.push_back(INVALID_ENTITY);
        m_prevSibling.push_back(INVALID_ENTITY);
        m_names.emplace_back();
    }
    ++m_aliveCount;

    m_db.transforms[idx].localMatrix = compose(desc.position, desc.rotation, desc.scale);
    if (idx < m_worldDirty.size()) m_worldDirty[idx] = 1;
    auto& r = m_db.renderables[idx];
    m_visible[idx] = 1;
    m_layer[idx] = 1;
    m_mesh[idx] = desc.mesh;
    r.mesh_uuid = desc.mesh;
    r.material_uuid = desc.material;
    r.shaderId = desc.shader;
    r.alpha = desc.alpha;
    m_names[idx] = desc.name;
    link(idx, valid(desc.parent) ? desc.parent.index : INVALID_ENTITY);
    queueSpatial(idx);
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
    m_events.emit(EntityDestroyed{{idx, m_generation[idx]}});
    queueSpatial(idx);
    runScriptDestroy(idx);
    clearTag(idx);
    for (auto& [type, p] : m_pools) p->remove(idx);
    m_alive[idx] = 0;
    ++m_generation[idx];
    m_firstChild[idx] = INVALID_ENTITY;
    m_db.hierarchies[idx].parent = INVALID_ENTITY;
    m_visible[idx] = 0;
    m_db.renderables[idx].flags |= RENDER_HIDDEN;
    m_names[idx].clear();
    m_freeList.push_back(idx);
    --m_aliveCount;
}

void World::destroy(EntityHandle e) {
    if (!valid(e)) return;
    if (m_updating) {
        m_pendingDestroy.push_back(e);
        return;
    }
    unlink(e.index);
    destroyRecursive(e.index);
    touchStructure();
}

void World::setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform) {
    if (!valid(e)) return;
    if (!parent.isNull() && (!valid(parent) || parent == e || isDescendantOf(parent, e))) return;
    if (m_db.hierarchies[e.index].parent == parent.index) return;

    const EntityHandle oldParent = getParent(e);
    const glm::mat4 world = keepWorldTransform ? getWorldMatrix(e) : glm::mat4(1.0f);
    unlink(e.index);
    link(e.index, parent.index);
    invalidateWorld(e.index);
    markSpatialSubtree(e.index);
    if (keepWorldTransform) {
        const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : getWorldMatrix(parent);
        m_db.transforms[e.index].localMatrix = glm::inverse(parentWorld) * world;
    }
    touchStructure();
    m_events.emit(EntityReparented{e, oldParent, parent});
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
    if (m_worldCache.size() < m_alive.size()) {
        m_worldCache.resize(m_alive.size(), glm::mat4(1.0f));
        m_worldDirty.resize(m_alive.size(), 1);
    }
    m_worldChain.clear();
    Entity cur = e.index;
    while (cur != INVALID_ENTITY && m_worldDirty[cur]) {
        m_worldChain.push_back(cur);
        cur = m_db.hierarchies[cur].parent;
    }
    glm::mat4 base = cur == INVALID_ENTITY ? glm::mat4(1.0f) : m_worldCache[cur];
    for (size_t i = m_worldChain.size(); i-- > 0;) {
        const Entity idx = m_worldChain[i];
        base = base * m_db.transforms[idx].localMatrix;
        m_worldCache[idx] = base;
        m_worldDirty[idx] = 0;
    }
    return m_worldCache[e.index];
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

void World::clearTag(Entity idx) {
    if (m_tags[idx].empty()) return;
    auto it = m_tagIndex.find(m_tags[idx]);
    if (it != m_tagIndex.end()) {
        auto& bucket = it->second;
        bucket.erase(std::remove(bucket.begin(), bucket.end(), idx), bucket.end());
        if (bucket.empty()) m_tagIndex.erase(it);
    }
    m_tags[idx].clear();
}

void World::setTag(EntityHandle e, std::string tag) {
    if (!valid(e)) return;
    clearTag(e.index);
    if (tag.empty()) return;
    m_tagIndex[tag].push_back(e.index);
    m_tags[e.index] = std::move(tag);
}

const std::string& World::getTag(EntityHandle e) const { return valid(e) ? m_tags[e.index] : kEmptyName; }

std::vector<EntityHandle> World::findByTag(const std::string& tag) const {
    std::vector<EntityHandle> out;
    const auto it = m_tagIndex.find(tag);
    if (it == m_tagIndex.end()) return out;
    out.reserve(it->second.size());
    for (Entity idx : it->second) out.push_back({idx, m_generation[idx]});
    return out;
}

void World::setLayer(EntityHandle e, uint32_t mask) {
    if (valid(e)) m_layer[e.index] = mask;
}

uint32_t World::getLayer(EntityHandle e) const { return valid(e) ? m_layer[e.index] : 0; }

void World::forEachInLayer(uint32_t mask, const std::function<void(EntityHandle)>& fn) const {
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && (m_layer[i] & mask)) fn({i, m_generation[i]});
    }
}

void World::setFlagBit(Entity idx, uint32_t bit, bool on) {
    uint32_t& flags = m_db.renderables[idx].flags;
    flags = on ? (flags | bit) : (flags & ~bit);
}

void World::setRenderFlags(EntityHandle e, uint32_t flags, bool enabled) {
    if (!valid(e)) return;
    const uint32_t keepHidden = m_visible[e.index] ? 0u : RENDER_HIDDEN;
    uint32_t& current = m_db.renderables[e.index].flags;
    current = enabled ? (current | flags) : (current & ~flags);
    current |= keepHidden;
    touchData();
}

uint32_t World::getRenderFlags(EntityHandle e) const {
    return valid(e) ? m_db.renderables[e.index].flags : 0;
}

void World::setVisible(EntityHandle e, bool visible) {
    if (!valid(e) || (m_visible[e.index] != 0) == visible) return;
    m_visible[e.index] = visible ? 1 : 0;
    queueSpatial(e.index);
    setFlagBit(e.index, RENDER_HIDDEN, !visible);
    touchData();
}

bool World::isVisible(EntityHandle e) const { return valid(e) && m_visible[e.index]; }

uint32_t World::getMesh(EntityHandle e) const { return valid(e) ? m_mesh[e.index] : 0; }

void World::setMesh(EntityHandle e, uint32_t mesh) {
    if (!valid(e)) return;
    m_mesh[e.index] = mesh;
    m_db.renderables[e.index].mesh_uuid = mesh;
    queueSpatial(e.index);
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
    m_layer.clear();
    m_tags.clear();
    m_tagIndex.clear();
    m_firstChild.clear();
    m_nextSibling.clear();
    m_prevSibling.clear();
    m_names.clear();
    m_freeList.clear();
    m_worldCache.clear();
    m_worldDirty.clear();
    m_spatial.clear();
    m_spatialQueued.clear();
    m_spatialPending.clear();
    m_cells.clear();
    m_largeEntities.clear();
    m_spatialActive = false;
    m_maxSmallRadius = 0.0f;
    m_scripts.clear();
    for (auto& [type, p] : m_pools) p->clear();
    m_pendingDestroy.clear();
    m_pendingRemoveScripts.clear();
    m_firstRoot = INVALID_ENTITY;
    m_aliveCount = 0;
}

bool World::saveScene(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out.precision(9);
    out << "LITSCENE 2\n" << m_aliveCount << "\n";
    if (m_meshNameOf) {
        std::set<uint32_t> usedMeshes;
        for (Entity i = 0; i < m_alive.size(); ++i) {
            if (m_alive[i]) usedMeshes.insert(m_mesh[i]);
        }
        for (uint32_t id : usedMeshes) {
            const std::string name = m_meshNameOf(id);
            if (!name.empty()) out << "mesh " << id << '\t' << name << '\n';
        }
    }
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (!m_alive[i]) continue;
        const auto& r = m_db.renderables[i];
        const Entity parent = m_db.hierarchies[i].parent;
        out << i << ' ' << (parent == INVALID_ENTITY ? -1 : static_cast<long long>(parent)) << ' ' << int(m_visible[i]) << ' ' << m_mesh[i] << ' ' << r.material_uuid << ' ' << r.shaderId << ' ' << r.alpha;
        const float* m = &m_db.transforms[i].localMatrix[0][0];
        for (int k = 0; k < 16; ++k) out << ' ' << m[k];
        out << ' ' << m_layer[i] << ' ' << (r.flags & ~RENDER_HIDDEN);
        out << '\t' << m_names[i] << '\n';
    }
    for (Entity i = 0; i < m_alive.size(); ++i) {
        if (m_alive[i] && !m_tags[i].empty()) out << "tag " << i << '\t' << m_tags[i] << '\n';
    }
    for (const auto& [name, serializer] : m_serializers) {
        const auto pool = m_pools.find(serializer.type);
        if (pool == m_pools.end()) continue;
        serializer.owners(*pool->second, [&](Entity e) {
            if (!m_alive[e]) return;
            out << "component " << name << ' ' << e << '\t';
            serializer.write(*pool->second, out, e);
            out << '\n';
        });
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
    if (!in || magic != "LITSCENE" || (version != 1 && version != 2)) return false;
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
    std::vector<std::string> componentLines;
    std::unordered_map<uint32_t, uint32_t> meshRemap;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.rfind("mesh ", 0) == 0) {
            const size_t meshTab = line.find('\t');
            if (meshTab != std::string::npos && m_meshLoad) {
                meshRemap[static_cast<uint32_t>(std::strtoul(line.c_str() + 5, nullptr, 10))] = m_meshLoad(line.substr(meshTab + 1));
            }
            continue;
        }
        if (line.rfind("tag ", 0) == 0 || line.rfind("component ", 0) == 0) {
            componentLines.push_back(line);
            continue;
        }
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
        uint32_t layer = 1;
        if (!(fields >> layer)) layer = 1;
        uint32_t renderFlags = 0;
        if (!(fields >> renderFlags)) renderFlags = 0;
        if (tab != std::string::npos) desc.name = line.substr(tab + 1);
        if (const auto remap = meshRemap.find(desc.mesh); remap != meshRemap.end()) desc.mesh = remap->second;
        const EntityHandle h = create(desc);
        setLocalMatrix(h, local);
        setLayer(h, layer);
        setRenderFlags(h, renderFlags, true);
        if (!visible) setVisible(h, false);
        handles[id] = h;
        records.push_back({id, parent});
    }

    for (const Record& r : records) {
        if (r.parent < 0) continue;
        const auto it = handles.find(r.parent);
        if (it != handles.end()) setParent(handles[r.id], it->second, false);
    }

    for (const std::string& componentLine : componentLines) {
        const size_t tab = componentLine.find('\t');
        if (tab == std::string::npos) continue;
        std::istringstream head(componentLine.substr(0, tab));
        std::string keyword, name;
        long long id;
        if (componentLine.rfind("tag ", 0) == 0) {
            head >> keyword >> id;
            const auto handle = handles.find(id);
            if (head && handle != handles.end()) setTag(handle->second, componentLine.substr(tab + 1));
            continue;
        }
        head >> keyword >> name >> id;
        const auto serializer = m_serializers.find(name);
        const auto handle = handles.find(id);
        if (!head || serializer == m_serializers.end() || handle == handles.end()) continue;
        std::istringstream payload(componentLine.substr(tab + 1));
        serializer->second.read(*this, handle->second, payload);
    }
    return true;
}

void World::runScriptDestroy(Entity idx) {
    const auto it = m_scripts.find(idx);
    if (it == m_scripts.end()) return;
    const EntityHandle h{idx, m_generation[idx]};
    auto scripts = std::move(it->second);
    m_scripts.erase(it);
    for (auto& s : scripts) {
        if (s->m_started) s->onDestroy(*this, h);
    }
}

Script* World::addScript(EntityHandle e, std::unique_ptr<Script> script) {
    if (!valid(e) || !script) return nullptr;
    Script* raw = script.get();
    m_scripts[e.index].push_back(std::move(script));
    return raw;
}

void World::removeScripts(EntityHandle e) {
    if (!valid(e)) return;
    if (m_updating) {
        m_pendingRemoveScripts.push_back(e);
        return;
    }
    runScriptDestroy(e.index);
}

void World::flushPendingDestroy() {
    auto pending = std::move(m_pendingDestroy);
    m_pendingDestroy.clear();
    for (EntityHandle h : pending) destroy(h);
}

void World::runScripts(const std::function<void(Script&, EntityHandle)>& fn) {
    std::vector<std::pair<EntityHandle, Script*>> snapshot;
    for (auto& [idx, scripts] : m_scripts) {
        for (auto& s : scripts) snapshot.emplace_back(EntityHandle{idx, m_generation[idx]}, s.get());
    }

    m_updating = true;
    for (auto& [h, script] : snapshot) {
        if (!script->m_started) {
            script->m_started = true;
            script->onStart(*this, h);
        }
        fn(*script, h);
    }
    m_updating = false;
    auto removals = std::move(m_pendingRemoveScripts);
    m_pendingRemoveScripts.clear();
    for (EntityHandle h : removals) removeScripts(h);
    flushPendingDestroy();
}

void World::update(float deltaTime) {
    runScripts([&](Script& s, EntityHandle h) { s.onUpdate(*this, h, deltaTime); });
}

void World::fixedUpdate(float fixedDelta) {
    runScripts([&](Script& s, EntityHandle h) { s.onFixedUpdate(*this, h, fixedDelta); });
}

Prefab World::capture(EntityHandle root) const {
    Prefab prefab;
    if (!valid(root)) return prefab;

    std::vector<std::pair<Entity, int>> stack{{root.index, -1}};
    while (!stack.empty()) {
        const auto [idx, parentNode] = stack.back();
        stack.pop_back();

        PrefabNode node;
        node.name = m_names[idx];
        node.mesh = m_mesh[idx];
        node.material = m_db.renderables[idx].material_uuid;
        node.shader = m_db.renderables[idx].shaderId;
        node.alpha = m_db.renderables[idx].alpha;
        node.visible = m_visible[idx] != 0;
        node.layer = m_layer[idx];
        node.renderFlags = m_db.renderables[idx].flags & ~RENDER_HIDDEN;
        node.tag = m_tags[idx];
        node.local = m_db.transforms[idx].localMatrix;
        node.parent = parentNode;
        for (const auto& [name, serializer] : m_serializers) {
            const auto pool = m_pools.find(serializer.type);
            if (pool == m_pools.end()) continue;
            std::ostringstream payload;
            serializer.write(*pool->second, payload, idx);
            if (!payload.str().empty()) node.components.emplace_back(name, payload.str());
        }

        const int nodeIndex = static_cast<int>(prefab.nodes.size());
        prefab.nodes.push_back(std::move(node));
        for (Entity c = m_firstChild[idx]; c != INVALID_ENTITY; c = m_nextSibling[c]) stack.emplace_back(c, nodeIndex);
    }
    return prefab;
}

EntityHandle World::instantiate(const Prefab& prefab, EntityHandle parent) {
    std::vector<EntityHandle> created;
    created.reserve(prefab.nodes.size());
    for (const PrefabNode& node : prefab.nodes) {
        EntityDesc desc;
        desc.name = node.name;
        desc.mesh = node.mesh;
        desc.material = node.material;
        desc.shader = node.shader;
        desc.alpha = node.alpha;
        desc.parent = node.parent < 0 ? parent : created[node.parent];
        const EntityHandle h = create(desc);
        setLocalMatrix(h, node.local);
        if (!node.visible) setVisible(h, false);
        setLayer(h, node.layer);
        setRenderFlags(h, node.renderFlags, true);
        setTag(h, node.tag);
        for (const auto& [name, payload] : node.components) {
            const auto serializer = m_serializers.find(name);
            if (serializer == m_serializers.end()) continue;
            std::istringstream in(payload);
            serializer->second.read(*this, h, in);
        }
        created.push_back(h);
    }
    return created.empty() ? NULL_ENTITY : created.front();
}

void World::syncCamera() {
    if (!valid(m_activeCamera)) return;
    const CameraComponent* cam = get<CameraComponent>(m_activeCamera);
    if (!cam) return;
    const glm::mat4 world = getWorldMatrix(m_activeCamera);
    const glm::vec3 forward = glm::normalize(-glm::vec3(world[2]));
    m_camera.setPos(glm::vec3(world[3]));
    m_camera.setOrientation(glm::degrees(std::atan2(forward.z, forward.x)), glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f))));
    m_camera.setFov(cam->fov);
    m_camera.setNearPlane(cam->nearPlane);
    m_camera.setFarPlane(cam->farPlane);
}

uint64_t World::spatialKey(int x, int y, int z) const {
    constexpr uint64_t mask = 0x1FFFFF;
    return ((static_cast<uint64_t>(x) & mask) << 42) | ((static_cast<uint64_t>(y) & mask) << 21) | (static_cast<uint64_t>(z) & mask);
}

void World::setSpatialCellSize(float size) {
    if (size <= 0.0f) return;
    m_cellSize = size;
    m_cells.clear();
    m_largeEntities.clear();
    m_spatial.clear();
    m_spatialQueued.clear();
    m_spatialPending.clear();
    m_maxSmallRadius = 0.0f;
    m_spatialActive = false;
}

void World::queueSpatial(Entity idx) {
    if (!m_spatialActive) return;
    if (m_spatialQueued.size() <= idx) m_spatialQueued.resize(m_alive.size(), 0);
    if (m_spatialQueued[idx]) return;
    m_spatialQueued[idx] = 1;
    m_spatialPending.push_back(idx);
}

void World::markSpatialSubtree(Entity idx) {
    if (!m_spatialActive) return;
    m_spatialStack.clear();
    m_spatialStack.push_back(idx);
    while (!m_spatialStack.empty()) {
        const Entity e = m_spatialStack.back();
        m_spatialStack.pop_back();
        queueSpatial(e);
        for (Entity c = m_firstChild[e]; c != INVALID_ENTITY; c = m_nextSibling[c]) m_spatialStack.push_back(c);
    }
}

void World::removeSpatialEntry(Entity idx) {
    SpatialEntry& entry = m_spatial[idx];
    if (entry.state == 1) {
        const auto it = m_cells.find(entry.cell);
        if (it != m_cells.end()) {
            auto& bucket = it->second;
            const auto pos = std::find(bucket.begin(), bucket.end(), idx);
            if (pos != bucket.end()) {
                *pos = bucket.back();
                bucket.pop_back();
            }
            if (bucket.empty()) m_cells.erase(it);
        }
    } else if (entry.state == 2) {
        m_largeEntities.erase(std::remove(m_largeEntities.begin(), m_largeEntities.end(), idx), m_largeEntities.end());
    }
    entry.state = 0;
}

void World::rebuildSpatialEntry(Entity idx) {
    if (m_spatial.size() <= idx) m_spatial.resize(m_alive.size());
    removeSpatialEntry(idx);
    if (idx >= m_alive.size() || !m_alive[idx] || !m_visible[idx] || !m_meshBounds) return;

    const glm::vec4 bounds = m_meshBounds(m_mesh[idx]);
    const glm::mat4 world = getWorldMatrix({idx, m_generation[idx]});
    const float maxScale = std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
    SpatialEntry& entry = m_spatial[idx];
    entry.center = glm::vec3(world * glm::vec4(glm::vec3(bounds), 1.0f));
    entry.radius = bounds.w * maxScale;

    if (entry.radius > m_cellSize * 4.0f) {
        entry.state = 2;
        m_largeEntities.push_back(idx);
        return;
    }
    m_maxSmallRadius = std::max(m_maxSmallRadius, entry.radius);
    entry.cell = spatialKey(static_cast<int>(std::floor(entry.center.x / m_cellSize)), static_cast<int>(std::floor(entry.center.y / m_cellSize)), static_cast<int>(std::floor(entry.center.z / m_cellSize)));
    entry.state = 1;
    m_cells[entry.cell].push_back(idx);
}

void World::refreshSpatial() {
    if (!m_spatialActive) {
        m_spatialActive = true;
        m_spatial.assign(m_alive.size(), SpatialEntry{});
        m_spatialQueued.assign(m_alive.size(), 0);
        m_spatialPending.clear();
        for (Entity i = 0; i < m_alive.size(); ++i) {
            if (m_alive[i]) rebuildSpatialEntry(i);
        }
        return;
    }
    auto pending = std::move(m_spatialPending);
    m_spatialPending.clear();
    for (Entity idx : pending) {
        if (idx < m_spatialQueued.size()) m_spatialQueued[idx] = 0;
        rebuildSpatialEntry(idx);
    }
}

std::vector<EntityHandle> World::overlapSphere(const glm::vec3& center, float radius) {
    refreshSpatial();
    std::vector<EntityHandle> out;
    const auto test = [&](Entity e) {
        const SpatialEntry& entry = m_spatial[e];
        const float reach = radius + entry.radius;
        const glm::vec3 d = entry.center - center;
        if (glm::dot(d, d) <= reach * reach) out.push_back({e, m_generation[e]});
    };

    const float reach = radius + m_maxSmallRadius;
    const int x0 = static_cast<int>(std::floor((center.x - reach) / m_cellSize)), x1 = static_cast<int>(std::floor((center.x + reach) / m_cellSize));
    const int y0 = static_cast<int>(std::floor((center.y - reach) / m_cellSize)), y1 = static_cast<int>(std::floor((center.y + reach) / m_cellSize));
    const int z0 = static_cast<int>(std::floor((center.z - reach) / m_cellSize)), z1 = static_cast<int>(std::floor((center.z + reach) / m_cellSize));
    const size_t span = static_cast<size_t>(x1 - x0 + 1) * static_cast<size_t>(y1 - y0 + 1) * static_cast<size_t>(z1 - z0 + 1);

    if (span > m_cells.size()) {
        for (const auto& [key, bucket] : m_cells) {
            for (Entity e : bucket) test(e);
        }
    } else {
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    const auto it = m_cells.find(spatialKey(x, y, z));
                    if (it == m_cells.end()) continue;
                    for (Entity e : it->second) test(e);
                }
            }
        }
    }
    for (Entity e : m_largeEntities) test(e);
    return out;
}

std::optional<RayHit> World::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    refreshSpatial();
    const float len = glm::length(direction);
    if (len < 1.0e-8f) return std::nullopt;
    const glm::vec3 dir = direction / len;

    std::optional<RayHit> best;
    const auto test = [&](Entity e) {
        const SpatialEntry& entry = m_spatial[e];
        const glm::vec3 oc = entry.center - origin;
        const float along = glm::dot(oc, dir);
        const float perp2 = glm::dot(oc, oc) - along * along;
        const float r2 = entry.radius * entry.radius;
        if (perp2 > r2) return;
        const float half = std::sqrt(r2 - perp2);
        float t = along - half;
        if (t < 0.0f) t = along + half;
        if (t < 0.0f || t > maxDistance) return;
        if (!best || t < best->distance) best = RayHit{{e, m_generation[e]}, t};
    };
    for (Entity e : m_largeEntities) test(e);

    const int inflate = static_cast<int>(std::ceil(m_maxSmallRadius / m_cellSize));
    int cell[3] = {static_cast<int>(std::floor(origin.x / m_cellSize)), static_cast<int>(std::floor(origin.y / m_cellSize)), static_cast<int>(std::floor(origin.z / m_cellSize))};
    int step[3];
    float tMax[3], tDelta[3];
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {dir.x, dir.y, dir.z};
    for (int a = 0; a < 3; ++a) {
        if (d[a] > 0.0f) {
            step[a] = 1;
            tMax[a] = ((cell[a] + 1) * m_cellSize - o[a]) / d[a];
            tDelta[a] = m_cellSize / d[a];
        } else if (d[a] < 0.0f) {
            step[a] = -1;
            tMax[a] = (cell[a] * m_cellSize - o[a]) / d[a];
            tDelta[a] = -m_cellSize / d[a];
        } else {
            step[a] = 0;
            tMax[a] = std::numeric_limits<float>::infinity();
            tDelta[a] = std::numeric_limits<float>::infinity();
        }
    }

    const float margin = m_maxSmallRadius + m_cellSize;
    float tCell = 0.0f;
    const size_t maxSteps = m_cells.empty() ? 0 : 4096;
    for (size_t iter = 0; iter < maxSteps; ++iter) {
        if (tCell > maxDistance + margin) break;
        if (best && tCell > best->distance + margin) break;
        for (int x = cell[0] - inflate; x <= cell[0] + inflate; ++x) {
            for (int y = cell[1] - inflate; y <= cell[1] + inflate; ++y) {
                for (int z = cell[2] - inflate; z <= cell[2] + inflate; ++z) {
                    const auto it = m_cells.find(spatialKey(x, y, z));
                    if (it == m_cells.end()) continue;
                    for (Entity e : it->second) test(e);
                }
            }
        }
        const int axis = (tMax[0] < tMax[1]) ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
        if (std::isinf(tMax[axis])) break;
        tCell = tMax[axis];
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
    }
    return best;
}

Ray World::screenRay(float screenX, float screenY, float width, float height) const {
    const float ndcX = 2.0f * screenX / width - 1.0f;
    const float ndcY = 1.0f - 2.0f * screenY / height;
    const glm::mat4 inv = glm::inverse(m_camera.getProjectionMatrix() * m_camera.getViewMatrix());
    glm::vec4 nearPoint = inv * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 n = glm::vec3(nearPoint) / nearPoint.w;
    const glm::vec3 f = glm::vec3(farPoint) / farPoint.w;
    return {n, glm::normalize(f - n)};
}
