module;

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

module Engine.Delta;

import Engine.Render.component;

namespace {
Json vec3Json(const glm::vec3& v) {
    Json a = Json::array();
    a.push(v.x);
    a.push(v.y);
    a.push(v.z);
    return a;
}

glm::vec3 vec3From(const Json* j, glm::vec3 fallback) {
    if (!j || j->size() != 3) return fallback;
    return {static_cast<float>(j->at(0)->asNumber()), static_cast<float>(j->at(1)->asNumber()), static_cast<float>(j->at(2)->asNumber())};
}

Json componentsJson(const std::vector<std::pair<std::string, std::string>>& list) {
    Json out = Json::array();
    for (const auto& [name, text] : list) {
        Json pair = Json::array();
        pair.push(name);
        pair.push(text);
        out.push(std::move(pair));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> componentsFrom(const Json* j) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!j) return out;
    for (const Json& p : j->items()) {
        if (p.size() == 2) out.emplace_back(p.at(0)->asString(), p.at(1)->asString());
    }
    return out;
}

Json matrixJson(const glm::mat4& m) {
    Json a = Json::array();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) a.push(m[c][r]);
    }
    return a;
}

glm::mat4 matrixFrom(const Json* j) {
    glm::mat4 m(1.0f);
    if (!j || j->size() != 16) return m;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) m[c][r] = static_cast<float>(j->at(static_cast<size_t>(c * 4 + r))->asNumber());
    }
    return m;
}

Json stateJson(const EntityState& s) {
    Json j = Json::object();
    j.set("name", s.name);
    j.set("tag", s.tag);
    j.set("mesh", s.mesh);
    j.set("material", s.material);
    j.set("shader", s.shader);
    j.set("layer", s.layer);
    j.set("flags", s.renderFlags);
    j.set("alpha", s.alpha);
    j.set("visible", s.visible);
    j.set("local", matrixJson(s.local));
    j.set("components", componentsJson(s.components));
    return j;
}

EntityState stateFrom(const Json& j) {
    EntityState s;
    if (const Json* v = j.get("name")) s.name = v->asString();
    if (const Json* v = j.get("tag")) s.tag = v->asString();
    if (const Json* v = j.get("mesh")) s.mesh = static_cast<uint32_t>(v->asNumber());
    if (const Json* v = j.get("material")) s.material = static_cast<uint32_t>(v->asNumber());
    if (const Json* v = j.get("shader")) s.shader = static_cast<uint32_t>(v->asNumber());
    if (const Json* v = j.get("layer")) s.layer = static_cast<uint32_t>(v->asNumber());
    if (const Json* v = j.get("flags")) s.renderFlags = static_cast<uint32_t>(v->asNumber());
    if (const Json* v = j.get("alpha")) s.alpha = static_cast<float>(v->asNumber(1.0));
    if (const Json* v = j.get("visible")) s.visible = v->asBool(true);
    s.local = matrixFrom(j.get("local"));
    s.components = componentsFrom(j.get("components"));
    return s;
}

Json prefabJson(const Prefab& p) {
    Json nodes = Json::array();
    for (const PrefabNode& n : p.nodes) {
        EntityState s;
        s.name = n.name;
        s.tag = n.tag;
        s.mesh = n.mesh;
        s.material = n.material;
        s.shader = n.shader;
        s.layer = n.layer;
        s.renderFlags = n.renderFlags;
        s.alpha = n.alpha;
        s.visible = n.visible;
        s.local = n.local;
        s.components = n.components;
        Json j = stateJson(s);
        j.set("parent", n.parent);
        j.set("source", n.sourceIndex);
        nodes.push(std::move(j));
    }
    return nodes;
}

Prefab prefabFrom(const Json& nodes) {
    Prefab p;
    for (const Json& j : nodes.items()) {
        const EntityState s = stateFrom(j);
        PrefabNode n;
        n.name = s.name;
        n.tag = s.tag;
        n.mesh = s.mesh;
        n.material = s.material;
        n.shader = s.shader;
        n.layer = s.layer;
        n.renderFlags = s.renderFlags;
        n.alpha = s.alpha;
        n.visible = s.visible;
        n.local = s.local;
        n.components = s.components;
        if (const Json* v = j.get("parent")) n.parent = static_cast<int>(v->asNumber(-1));
        if (const Json* v = j.get("source")) n.sourceIndex = static_cast<uint32_t>(v->asNumber());
        p.nodes.push_back(std::move(n));
    }
    return p;
}

Json paramsJson(const PortalCellParams& p) {
    Json j = Json::object();
    j.set("gravity", vec3Json(p.gravity));
    j.set("tint", vec3Json(p.tint));
    j.set("sun", p.sun);
    j.set("ambient", p.ambient);
    Json sc = Json::object();
    for (const auto& [k, v] : p.scalars) sc.set(k, v);
    j.set("scalars", std::move(sc));
    return j;
}

bool sameParams(const PortalCellParams& a, const PortalCellParams& b) {
    return a.gravity == b.gravity && a.tint == b.tint && a.sun == b.sun && a.ambient == b.ambient && a.scalars == b.scalars;
}

void paramsFrom(const Json& j, PortalCellParams& p) {
    p.gravity = vec3From(j.get("gravity"), p.gravity);
    p.tint = vec3From(j.get("tint"), p.tint);
    if (const Json* v = j.get("sun")) p.sun = static_cast<float>(v->asNumber(1.0));
    if (const Json* v = j.get("ambient")) p.ambient = static_cast<float>(v->asNumber(1.0));
    if (const Json* sc = j.get("scalars")) {
        p.scalars.clear();
        for (const auto& [k, v] : sc->members()) p.scalars[k] = static_cast<float>(v.asNumber());
    }
}
}

EntityState DeltaStore::read(EntityHandle e) const {
    EntityState s;
    s.name = m_world.getName(e);
    s.tag = m_world.getTag(e);
    s.mesh = m_world.getMesh(e);
    const RenderableComponent& r = m_world.getRenderable(e);
    s.material = r.material_uuid;
    s.shader = r.shaderId;
    s.alpha = r.alpha;
    s.renderFlags = r.flags & ~RENDER_HIDDEN;
    s.layer = m_world.getLayer(e);
    s.visible = m_world.isVisible(e);
    s.local = m_world.getLocalMatrix(e);
    const Prefab p = m_world.capture(e);
    if (!p.nodes.empty()) s.components = p.nodes.front().components;
    return s;
}

void DeltaStore::write(EntityHandle e, const EntityState& s) {
    m_world.setName(e, s.name);
    m_world.setTag(e, s.tag);
    m_world.setMesh(e, s.mesh);
    m_world.setMaterial(e, s.material);
    m_world.setShader(e, s.shader);
    m_world.setAlpha(e, s.alpha);
    m_world.setLayer(e, s.layer);
    m_world.setVisible(e, s.visible);
    const uint32_t current = m_world.getRenderFlags(e) & ~RENDER_HIDDEN;
    m_world.setRenderFlags(e, current & ~s.renderFlags, false);
    m_world.setRenderFlags(e, s.renderFlags, true);
    m_world.setLocalMatrix(e, s.local);
    for (const auto& [name, text] : s.components) {
        if (!m_world.componentText(e, name)) m_world.addComponentByName(e, name);
        m_world.setComponentFromText(e, name, text);
    }
}

void DeltaStore::registerBase(const std::string& cell, std::string key, EntityHandle entity) {
    Live& live = m_live[cell];
    live.bases.push_back({std::move(key), entity, read(entity)});
}

EntityHandle DeltaStore::trackAdded(const std::string& cell, EntityHandle root) {
    Live& live = m_live[cell];
    live.added.push_back({"added." + std::to_string(m_addedSerial++), root});
    return root;
}

EntityHandle DeltaStore::find(const std::string& cell, const std::string& key) const {
    const auto it = m_live.find(cell);
    if (it == m_live.end()) return NULL_ENTITY;
    for (const Base& b : it->second.bases) {
        if (b.key == key) return b.handle;
    }
    for (const Added& a : it->second.added) {
        if (a.key == key) return a.handle;
    }
    return NULL_ENTITY;
}

std::vector<EntityHandle> DeltaStore::liveEntities(const std::string& cell) const {
    std::vector<EntityHandle> out;
    const auto it = m_live.find(cell);
    if (it == m_live.end()) return out;
    for (const Base& b : it->second.bases) {
        if (m_world.isAlive(b.handle)) out.push_back(b.handle);
    }
    for (const Added& a : it->second.added) {
        if (m_world.isAlive(a.handle)) out.push_back(a.handle);
    }
    return out;
}

void DeltaStore::snapshotParams() {
    m_baseParams.clear();
    if (!m_portals) return;
    for (const PortalCell& c : m_portals->cells()) m_baseParams[c.name] = c.params;
}

void DeltaStore::captureParams(const std::string& cell) {
    if (!m_portals) return;
    const int index = m_portals->findCell(cell);
    const auto base = m_baseParams.find(cell);
    if (index < 0 || base == m_baseParams.end()) return;
    Stored& stored = m_stored[cell];
    const PortalCellParams& now = m_portals->cellParams(index);
    stored.hasParams = !sameParams(now, base->second);
    stored.paramsJson = stored.hasParams ? paramsJson(now) : Json();
}

void DeltaStore::applyParams(const std::string& cell) {
    if (!m_portals) return;
    const auto it = m_stored.find(cell);
    const int index = m_portals->findCell(cell);
    if (it == m_stored.end() || index < 0 || !it->second.hasParams) return;
    paramsFrom(it->second.paramsJson, m_portals->cellParams(index));
}

void DeltaStore::captureCell(const std::string& cell) {
    captureParams(cell);
    const auto liveIt = m_live.find(cell);
    if (liveIt == m_live.end() || !liveIt->second.resident) return;
    Stored& stored = m_stored[cell];
    for (const Base& b : liveIt->second.bases) {
        if (!m_world.isAlive(b.handle)) {
            stored.changed.erase(b.key);
            stored.removed.insert(b.key);
            continue;
        }
        stored.removed.erase(b.key);
        EntityState now = read(b.handle);
        if (now == b.baseline) stored.changed.erase(b.key);
        else stored.changed[b.key] = std::move(now);
    }
    for (const Added& a : liveIt->second.added) {
        if (m_world.isAlive(a.handle)) stored.added[a.key] = m_world.capture(a.handle);
        else stored.added.erase(a.key);
    }
}

void DeltaStore::captureAll() {
    std::set<std::string> cells;
    for (const auto& [name, live] : m_live) cells.insert(name);
    if (m_portals) {
        for (const PortalCell& c : m_portals->cells()) cells.insert(c.name);
    }
    for (const std::string& c : cells) captureCell(c);
}

void DeltaStore::applyCell(const std::string& cell) {
    applyParams(cell);
    const auto storedIt = m_stored.find(cell);
    Live& live = m_live[cell];
    live.resident = true;
    if (storedIt == m_stored.end()) return;
    const Stored& stored = storedIt->second;
    for (const Base& b : live.bases) {
        if (!m_world.isAlive(b.handle)) continue;
        if (stored.removed.contains(b.key)) {
            m_world.destroy(b.handle);
            continue;
        }
        if (const auto it = stored.changed.find(b.key); it != stored.changed.end()) write(b.handle, it->second);
    }
    for (const auto& [key, prefab] : stored.added) {
        const bool present = std::any_of(live.added.begin(), live.added.end(), [&](const Added& a) { return a.key == key && m_world.isAlive(a.handle); });
        if (present || prefab.empty()) continue;
        const EntityHandle root = m_world.instantiate(prefab);
        live.added.push_back({key, root});
    }
}

void DeltaStore::releaseCell(const std::string& cell) { m_live.erase(cell); }

void DeltaStore::clear() {
    m_live.clear();
    m_stored.clear();
}

size_t DeltaStore::deltaCount() const {
    size_t n = 0;
    for (const auto& [cell, s] : m_stored) n += s.changed.size() + s.removed.size() + s.added.size() + (s.hasParams ? 1 : 0);
    return n;
}

Json DeltaStore::toJson() {
    captureAll();
    Json cells = Json::object();
    for (const auto& [name, s] : m_stored) {
        if (s.empty()) continue;
        Json c = Json::object();
        Json changed = Json::object();
        for (const auto& [key, state] : s.changed) changed.set(key, stateJson(state));
        c.set("changed", std::move(changed));
        Json removed = Json::array();
        for (const std::string& key : s.removed) removed.push(key);
        c.set("removed", std::move(removed));
        Json added = Json::object();
        for (const auto& [key, prefab] : s.added) added.set(key, prefabJson(prefab));
        c.set("added", std::move(added));
        if (s.hasParams) c.set("params", s.paramsJson);
        cells.set(name, std::move(c));
    }
    Json doc = Json::object();
    doc.set("version", 1);
    doc.set("serial", m_addedSerial);
    doc.set("cells", std::move(cells));
    return doc;
}

bool DeltaStore::fromJson(const Json& doc, std::string* error) {
    const Json* cells = doc.get("cells");
    if (!doc.isObject() || !cells || !cells->isObject()) {
        if (error) *error = "delta: missing cells";
        return false;
    }
    std::map<std::string, Stored> parsed;
    for (const auto& [name, c] : cells->members()) {
        Stored s;
        if (const Json* changed = c.get("changed")) {
            for (const auto& [key, v] : changed->members()) s.changed[key] = stateFrom(v);
        }
        if (const Json* removed = c.get("removed")) {
            for (const Json& k : removed->items()) s.removed.insert(k.asString());
        }
        if (const Json* added = c.get("added")) {
            for (const auto& [key, v] : added->members()) s.added[key] = prefabFrom(v);
        }
        if (const Json* p = c.get("params")) {
            s.hasParams = true;
            s.paramsJson = *p;
        }
        parsed[name] = std::move(s);
    }
    for (auto& [name, live] : m_live) {
        for (const Added& a : live.added) {
            if (m_world.isAlive(a.handle)) m_world.destroy(a.handle);
        }
        live.added.clear();
        for (const Base& b : live.bases) {
            if (m_world.isAlive(b.handle)) write(b.handle, b.baseline);
        }
    }
    if (m_portals) {
        for (const auto& [name, base] : m_baseParams) {
            const int index = m_portals->findCell(name);
            if (index >= 0) m_portals->cellParams(index) = base;
        }
    }
    m_stored = std::move(parsed);
    if (const Json* serial = doc.get("serial")) m_addedSerial = std::max<uint64_t>(m_addedSerial, static_cast<uint64_t>(serial->asNumber()));
    for (const auto& [name, live] : m_live) {
        if (live.resident) applyCell(name);
    }
    if (m_portals) {
        for (const auto& [name, s] : m_stored) applyParams(name);
    }
    return true;
}
