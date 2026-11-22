module;

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module Engine.Terrain;

import Engine.Render.component;
import Engine.Render.entity;

namespace {
constexpr size_t kBytesPerDestroyedEntity = 64;
constexpr size_t kUnloadBatch = 64;
constexpr size_t kTerrainClusterMinTriangles = 4096;  // as kClusterMinTriangles of the asset pipeline

struct TilePayload {
    TileKey key;
    std::shared_ptr<HeightfieldData> field;
    std::unique_ptr<TerrainTileMesh> mesh;
    std::vector<CellEntity> scatter;                 // one entity per instance (species without geometry)
    std::vector<ScatterBatch> batches;               // merged per species
    std::vector<PreparedMesh> prepared;              // LOD chain and clusters of each batch, built on the worker
    size_t next = 0;
    size_t nextBatch = 0;
    bool tileCreated = false;
};

PreparedMesh prepareMesh(const Mesh& merged, bool clusters) {
    PreparedMesh out;
    out.base = merged.optimized();
    out.lods = out.base.generateLODChain();
    if (clusters && out.base.indices.size() / 3 >= kTerrainClusterMinTriangles) out.clusters = buildClusterMesh(out.base.vertices, kVertexFloats, out.base.indices);
    return out;
}
}

struct TerrainTileLoader::State {
    TerrainLoaderConfig config;
    World* world = nullptr;
    std::map<int, std::vector<EntityHandle>> created;
    std::map<int, std::vector<uint32_t>> meshes;
    std::map<int, TileKey> keys;
    std::map<int, bool> visible;
};

TerrainTileLoader::TerrainTileLoader(World& world, TerrainLoaderConfig config) : m_state(std::make_shared<State>()) {
    m_state->config = std::move(config);
    m_state->world = &world;
}

TerrainTileLoader::~TerrainTileLoader() = default;

TerrainLoaderConfig& TerrainTileLoader::config() { return m_state->config; }

size_t TerrainTileLoader::entityCount(int cell) const {
    const auto it = m_state->created.find(cell);
    return it == m_state->created.end() ? 0 : it->second.size();
}

size_t TerrainTileLoader::meshCount() const {
    size_t total = 0;
    for (const auto& [cell, ids] : m_state->meshes) total += ids.size();
    return total;
}

void TerrainTileLoader::setVisible(int cell, bool visible) {
    State& s = *m_state;
    const auto it = s.created.find(cell);
    if (it == s.created.end()) return;
    const auto known = s.visible.find(cell);
    if (known != s.visible.end() && known->second == visible) return;
    s.visible[cell] = visible;
    for (const EntityHandle handle : it->second) {
        if (s.world->isAlive(handle)) s.world->setVisible(handle, visible);
    }
}

std::shared_ptr<void> TerrainTileLoader::prepare(int cell) {
    const State& s = *m_state;
    if (!s.config.tileOfCell) return nullptr;
    const auto key = s.config.tileOfCell(cell);
    if (!key) return nullptr;
    auto payload = std::make_shared<TilePayload>();
    payload->key = *key;
    payload->mesh = std::make_unique<TerrainTileMesh>(buildTileMesh(s.config.terrain, *key, s.config.meshOptions));
    if (key->level <= s.config.colliderMaxLevel) payload->field = buildTileHeightfield(s.config.terrain, *key);
    if (key->level <= s.config.scatterMaxLevel && !s.config.scatter.species.empty()) {
        const std::vector<ScatterInstance> instances = scatterTile(s.config.terrain, s.config.scatter, *key);
        if (s.config.uploadScatterMesh) {
            payload->batches = scatterBatches(s.config.scatter, *key, instances, s.config.terrain);
            for (const ScatterBatch& batch : payload->batches) payload->prepared.push_back(prepareMesh(batch.mesh, s.config.scatterClusters));
        }
        // Species without CPU geometry keep the one entity per instance path.
        std::vector<ScatterInstance> plain;
        for (const ScatterInstance& instance : instances) {
            const ScatterSpecies& species = s.config.scatter.species[instance.species];
            if (!s.config.uploadScatterMesh || !species.geometry) plain.push_back(instance);
        }
        payload->scatter = scatterEntities(s.config.scatter, *key, plain);
    }
    return payload;
}

bool TerrainTileLoader::commit(int cell, void* raw, StreamBudget& budget) {
    if (!raw) return true;
    State& s = *m_state;
    TilePayload& payload = *static_cast<TilePayload*>(raw);
    World& w = *s.world;
    const bool hidden = s.config.startHidden;
    if (!payload.tileCreated) {
        EntityDesc desc;
        desc.name = tileName(payload.key);
        desc.mesh = s.config.uploadMesh ? s.config.uploadMesh(payload.mesh->mesh) : NO_MESH;
        if (desc.mesh != NO_MESH) s.meshes[cell].push_back(desc.mesh);
        desc.material = s.config.materialOfLevel ? s.config.materialOfLevel(payload.key.level) : s.config.material;
        desc.shader = s.config.shader;
        desc.position = w.toLocal(glm::dvec3(tileOrigin(s.config.terrain, payload.key)));
        const EntityHandle tile = w.create(desc);
        w.setLayer(tile, s.config.layer);
        w.setTag(tile, "terrain");
        if (hidden) w.setVisible(tile, false);
        if (payload.field) {
            HeightfieldCollider collider;
            collider.data = payload.field;
            w.add<HeightfieldCollider>(tile, collider);
        }
        s.created[cell].push_back(tile);
        s.keys[cell] = payload.key;
        s.visible[cell] = !hidden;
        payload.tileCreated = true;
        budget.spend(payload.mesh->mesh.vertices.size() * sizeof(float) + payload.mesh->mesh.indices.size() * sizeof(unsigned int));
        if (budget.exhausted() && (!payload.scatter.empty() || !payload.batches.empty())) return false;
    }
    while (payload.nextBatch < payload.batches.size()) {
        const size_t index = payload.nextBatch++;
        const ScatterBatch& batch = payload.batches[index];
        const ScatterSpecies& species = s.config.scatter.species[batch.species];
        EntityDesc desc;
        desc.name = species.name + " " + tileName(payload.key);
        desc.mesh = s.config.uploadScatterMesh(payload.prepared[index]);
        if (desc.mesh != NO_MESH) s.meshes[cell].push_back(desc.mesh);
        desc.material = species.material;
        desc.shader = species.shader;
        desc.position = w.toLocal(glm::dvec3(tileOrigin(s.config.terrain, payload.key)));
        const EntityHandle handle = w.create(desc);
        if (!species.tag.empty()) w.setTag(handle, species.tag);
        w.setLayer(handle, s.config.layer);
        if (hidden) w.setVisible(handle, false);
        s.created[cell].push_back(handle);
        budget.spend(payload.batches[index].mesh.vertices.size() * sizeof(float) * 2);
        std::vector<float>().swap(payload.batches[index].mesh.vertices);
        std::vector<unsigned int>().swap(payload.batches[index].mesh.indices);
        if (budget.exhausted()) return false;
    }
    while (payload.next < payload.scatter.size()) {
        const CellEntity& entity = payload.scatter[payload.next++];
        EntityDesc desc = entity.desc;
        desc.position = w.toLocal(glm::dvec3(desc.position));
        const EntityHandle handle = w.create(desc);
        if (!entity.tag.empty()) w.setTag(handle, entity.tag);
        w.setLayer(handle, entity.layer);
        if (hidden) w.setVisible(handle, false);
        s.created[cell].push_back(handle);
        budget.spend(entity.bytes);
        if (budget.exhausted()) break;
    }
    return payload.next >= payload.scatter.size() && payload.nextBatch >= payload.batches.size();
}

bool TerrainTileLoader::unload(int cell, StreamBudget& budget) {
    State& s = *m_state;
    std::vector<EntityHandle>& doomed = s.created[cell];
    while (!doomed.empty()) {
        const size_t count = std::min(kUnloadBatch, doomed.size());
        std::vector<EntityHandle> alive;
        for (auto handle = doomed.end() - static_cast<std::ptrdiff_t>(count); handle != doomed.end(); ++handle) {
            if (s.world->isAlive(*handle)) alive.push_back(*handle);
        }
        doomed.resize(doomed.size() - count);
        s.world->destroyBatch(alive);
        budget.spend(count * kBytesPerDestroyedEntity);
        if (budget.exhausted() && !doomed.empty()) return false;
    }
    // Entities are gone: the GPU geometry (tile mesh and merged scatter meshes) can be reused.
    if (s.config.releaseMesh) {
        for (const uint32_t mesh : s.meshes[cell]) s.config.releaseMesh(mesh);
    }
    s.meshes.erase(cell);
    s.created.erase(cell);
    s.keys.erase(cell);
    s.visible.erase(cell);
    return true;
}

CellLoader makeTerrainLoader(World& world, TerrainLoaderConfig config) {
    auto loader = std::make_shared<TerrainTileLoader>(world, std::move(config));
    CellLoader out;
    out.prepare = [loader](int cell) { return loader->prepare(cell); };
    out.commit = [loader](int cell, void* payload, StreamBudget& budget) { return loader->commit(cell, payload, budget); };
    out.unload = [loader](int cell, StreamBudget& budget) { return loader->unload(cell, budget); };
    out.evicting = [](int) {};
    out.resident = [](int) {};
    return out;
}
