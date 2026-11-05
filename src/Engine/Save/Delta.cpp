module;

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

module Engine.Delta;

import Engine.Render.component;

#include "DeltaCodec.hpp"

using namespace delta_codec;

EntityState DeltaStore::read(EntityHandle entity) const {
    EntityState state;
    readInto(entity, state, m_world.componentNames());
    return state;
}

void DeltaStore::readInto(EntityHandle entity, EntityState& state, const std::vector<std::string>& componentNames) const {
    state.name = m_world.getName(entity);
    state.tag = m_world.getTag(entity);
    state.mesh = m_world.getMesh(entity);
    const RenderableComponent& renderable = m_world.getRenderable(entity);
    state.material = renderable.material_uuid;
    state.shader = renderable.shaderId;
    state.alpha = renderable.alpha;
    state.renderFlags = renderable.flags & ~RENDER_HIDDEN;
    state.layer = m_world.getLayer(entity);
    state.visible = m_world.isVisible(entity);
    state.local = m_world.getLocalMatrix(entity);
    state.components.clear();
    for (const std::string& name : componentNames) {
        std::optional<std::string> text = m_world.componentText(entity, name);
        if (text && !text->empty()) state.components.emplace_back(name, std::move(*text));
    }
}

bool DeltaStore::matches(EntityHandle entity, const EntityState& state, const std::vector<std::string>& componentNames) const {
    const RenderableComponent& renderable = m_world.getRenderable(entity);
    if (m_world.getMesh(entity) != state.mesh || renderable.material_uuid != state.material || renderable.shaderId != state.shader) return false;
    if (renderable.alpha != state.alpha || (renderable.flags & ~RENDER_HIDDEN) != state.renderFlags) return false;
    if (m_world.getLayer(entity) != state.layer || m_world.isVisible(entity) != state.visible) return false;
    if (m_world.getLocalMatrix(entity) != state.local || m_world.getName(entity) != state.name || m_world.getTag(entity) != state.tag) return false;
    size_t next = 0;
    for (const std::string& name : componentNames) {
        std::optional<std::string> text = m_world.componentText(entity, name);
        if (!text || text->empty()) continue;
        if (next >= state.components.size() || state.components[next].first != name || state.components[next].second != *text) return false;
        ++next;
    }
    return next == state.components.size();
}

void DeltaStore::writeChanges(EntityHandle entity, const EntityState& state) {
    const RenderableComponent& renderable = m_world.getRenderable(entity);
    if (m_world.getName(entity) != state.name) m_world.setName(entity, state.name);
    if (m_world.getTag(entity) != state.tag) m_world.setTag(entity, state.tag);
    if (m_world.getMesh(entity) != state.mesh) m_world.setMesh(entity, state.mesh);
    if (renderable.material_uuid != state.material) m_world.setMaterial(entity, state.material);
    if (renderable.shaderId != state.shader) m_world.setShader(entity, state.shader);
    if (renderable.alpha != state.alpha) m_world.setAlpha(entity, state.alpha);
    if (m_world.getLayer(entity) != state.layer) m_world.setLayer(entity, state.layer);
    if (m_world.isVisible(entity) != state.visible) m_world.setVisible(entity, state.visible);
    const uint32_t current = m_world.getRenderFlags(entity) & ~RENDER_HIDDEN;
    if (current != state.renderFlags) {
        m_world.setRenderFlags(entity, current & ~state.renderFlags, false);
        m_world.setRenderFlags(entity, state.renderFlags, true);
    }
    if (m_world.getLocalMatrix(entity) != state.local) m_world.setLocalMatrix(entity, state.local);
    for (const auto& [name, text] : state.components) {
        const std::optional<std::string> existing = m_world.componentText(entity, name);
        if (existing && *existing == text) continue;
        if (!existing) m_world.addComponentByName(entity, name);
        m_world.setComponentFromText(entity, name, text);
    }
}

void DeltaStore::buildIndex(Live& live) {
    if (live.indexed == live.bases.size()) return;
    live.index.clear();
    live.index.reserve(live.bases.size());
    live.duplicateKeys = false;
    for (size_t position = 0; position < live.bases.size(); ++position) {
        if (!live.index.emplace(live.bases[position].key, static_cast<uint32_t>(position)).second) live.duplicateKeys = true;
    }
    live.indexed = live.bases.size();
}

void DeltaStore::registerBase(const std::string& cell, std::string key, EntityHandle entity) {
    m_live[cell].bases.push_back({std::move(key), entity, read(entity)});
}

EntityHandle DeltaStore::trackAdded(const std::string& cell, EntityHandle root) {
    m_live[cell].added.push_back({"added." + std::to_string(m_addedSerial++), root});
    return root;
}

EntityHandle DeltaStore::find(const std::string& cell, const std::string& key) const {
    const auto liveIt = m_live.find(cell);
    if (liveIt == m_live.end()) return NULL_ENTITY;
    for (const Base& base : liveIt->second.bases) {
        if (base.key == key) return base.handle;
    }
    for (const Added& added : liveIt->second.added) {
        if (added.key == key) return added.handle;
    }
    return NULL_ENTITY;
}

std::vector<EntityHandle> DeltaStore::liveEntities(const std::string& cell) const {
    std::vector<EntityHandle> out;
    const auto liveIt = m_live.find(cell);
    if (liveIt == m_live.end()) return out;
    out.reserve(liveIt->second.bases.size() + liveIt->second.added.size());
    for (const Base& base : liveIt->second.bases) {
        if (m_world.isAlive(base.handle)) out.push_back(base.handle);
    }
    for (const Added& added : liveIt->second.added) {
        if (m_world.isAlive(added.handle)) out.push_back(added.handle);
    }
    return out;
}

void DeltaStore::snapshotParams() {
    m_baseParams.clear();
    if (!m_portals) return;
    for (const PortalCell& portalCell : m_portals->cells()) m_baseParams[portalCell.name] = portalCell.params;
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
    const auto storedIt = m_stored.find(cell);
    const int index = m_portals->findCell(cell);
    if (storedIt == m_stored.end() || index < 0 || !storedIt->second.hasParams) return;
    paramsFrom(storedIt->second.paramsJson, m_portals->cellParams(index));
}

void DeltaStore::captureCell(const std::string& cell) {
    captureParams(cell);
    const auto liveIt = m_live.find(cell);
    if (liveIt == m_live.end() || !liveIt->second.resident) return;
    Stored& stored = m_stored[cell];
    const std::vector<std::string> componentNames = m_world.componentNames();
    for (const Base& base : liveIt->second.bases) {
        if (!m_world.isAlive(base.handle)) {
            if (!stored.changed.empty()) stored.changed.erase(base.key);
            stored.removed.insert(base.key);
            continue;
        }
        if (!stored.removed.empty()) stored.removed.erase(base.key);
        if (matches(base.handle, base.baseline, componentNames)) {
            if (!stored.changed.empty()) stored.changed.erase(base.key);
            continue;
        }
        auto changedIt = stored.changed.find(base.key);
        if (changedIt == stored.changed.end()) changedIt = stored.changed.try_emplace(base.key).first;
        readInto(base.handle, changedIt->second, componentNames);
    }
    for (const Added& added : liveIt->second.added) {
        if (m_world.isAlive(added.handle)) stored.added[added.key] = m_world.capture(added.handle);
        else stored.added.erase(added.key);
    }
}

void DeltaStore::captureAll() {
    std::set<std::string> cells;
    for (const auto& [name, live] : m_live) cells.insert(name);
    if (m_portals) {
        for (const PortalCell& portalCell : m_portals->cells()) cells.insert(portalCell.name);
    }
    for (const std::string& cell : cells) captureCell(cell);
}

void DeltaStore::applyStored(Live& live, const Stored& stored) {
    buildIndex(live);
    if (live.duplicateKeys) {
        for (const Base& base : live.bases) {
            if (!m_world.isAlive(base.handle)) continue;
            if (stored.removed.contains(base.key)) {
                m_world.destroy(base.handle);
                continue;
            }
            if (const auto changedIt = stored.changed.find(base.key); changedIt != stored.changed.end()) writeChanges(base.handle, changedIt->second);
        }
        return;
    }
    if (!stored.removed.empty()) {
        std::vector<uint32_t> doomed;
        doomed.reserve(stored.removed.size());
        for (const std::string& key : stored.removed) {
            if (const auto found = live.index.find(key); found != live.index.end()) doomed.push_back(found->second);
        }
        std::sort(doomed.begin(), doomed.end());
        for (const uint32_t position : doomed) {
            if (m_world.isAlive(live.bases[position].handle)) m_world.destroy(live.bases[position].handle);
        }
    }
    for (const auto& [key, state] : stored.changed) {
        const auto found = live.index.find(key);
        if (found == live.index.end()) continue;
        const EntityHandle handle = live.bases[found->second].handle;
        if (m_world.isAlive(handle)) writeChanges(handle, state);
    }
}

void DeltaStore::applyCell(const std::string& cell) {
    applyParams(cell);
    const auto storedIt = m_stored.find(cell);
    Live& live = m_live[cell];
    live.resident = true;
    if (storedIt == m_stored.end()) return;
    const Stored& stored = storedIt->second;
    applyStored(live, stored);
    std::set<std::string> presentKeys;
    for (const Added& added : live.added) {
        if (m_world.isAlive(added.handle)) presentKeys.insert(added.key);
    }
    for (const auto& [key, prefab] : stored.added) {
        if (presentKeys.contains(key) || prefab.empty()) continue;
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
    size_t count = 0;
    for (const auto& [cell, stored] : m_stored) count += stored.changed.size() + stored.removed.size() + stored.added.size() + (stored.hasParams ? 1 : 0);
    return count;
}

Json DeltaStore::toJson() {
    captureAll();
    Json cells = Json::object();
    for (const auto& [name, stored] : m_stored) {
        if (stored.empty()) continue;
        cells.set(name, storedJson(stored));
    }
    Json document = Json::object();
    document.set("version", 1);
    document.set("serial", m_addedSerial);
    document.set("cells", std::move(cells));
    return document;
}

Json DeltaStore::storedJson(const Stored& stored) {
    Json cell = Json::object();
    Json changed = Json::object();
    for (const auto& [key, state] : stored.changed) changed.set(key, stateJson(state));
    cell.set("changed", std::move(changed));
    Json removed = Json::array();
    for (const std::string& key : stored.removed) removed.push(key);
    cell.set("removed", std::move(removed));
    Json added = Json::object();
    for (const auto& [key, prefab] : stored.added) added.set(key, prefabJson(prefab));
    cell.set("added", std::move(added));
    if (stored.hasParams) cell.set("params", stored.paramsJson);
    return cell;
}

DeltaStore::Stored DeltaStore::storedFrom(const Json& cell) {
    Stored stored;
    if (const Json* changed = cell.get("changed")) {
        for (const auto& [key, value] : changed->members()) stored.changed[key] = stateFrom(value);
    }
    if (const Json* removed = cell.get("removed")) {
        for (const Json& key : removed->items()) stored.removed.insert(key.asString());
    }
    if (const Json* added = cell.get("added")) {
        for (const auto& [key, value] : added->members()) stored.added[key] = prefabFrom(value);
    }
    if (const Json* params = cell.get("params")) {
        stored.hasParams = true;
        stored.paramsJson = *params;
    }
    return stored;
}

void DeltaStore::revertToBaseline() {
    for (auto& [name, live] : m_live) {
        for (const Added& added : live.added) {
            if (m_world.isAlive(added.handle)) m_world.destroy(added.handle);
        }
        live.added.clear();
        for (const Base& base : live.bases) {
            if (m_world.isAlive(base.handle)) writeChanges(base.handle, base.baseline);
        }
    }
    if (!m_portals) return;
    for (const auto& [name, base] : m_baseParams) {
        const int index = m_portals->findCell(name);
        if (index >= 0) m_portals->cellParams(index) = base;
    }
}

bool DeltaStore::fromJson(const Json& document, std::string* error) {
    const Json* cells = document.get("cells");
    if (!document.isObject() || !cells || !cells->isObject()) {
        if (error) *error = "delta: missing cells";
        return false;
    }
    std::map<std::string, Stored> parsed;
    for (const auto& [name, cell] : cells->members()) parsed[name] = storedFrom(cell);
    revertToBaseline();
    m_stored = std::move(parsed);
    if (const Json* serial = document.get("serial")) m_addedSerial = std::max<uint64_t>(m_addedSerial, safeSerial(serial->asNumber()));
    for (const auto& [name, live] : m_live) {
        if (live.resident) applyCell(name);
    }
    if (m_portals) {
        for (const auto& [name, stored] : m_stored) applyParams(name);
    }
    return true;
}
