module;

#include <algorithm>
#include <cmath>
#include <span>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

module Engine.Terrain;

import Engine.Delta;
import Engine.Render.component;
import Engine.Render.entity;
import Engine.Instancing;
import Engine.glm;

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
    struct InstancedSpecies {
        uint32_t species = 0;
        std::vector<InstanceData> instances;
        std::vector<uint64_t> keys;
    };
    std::vector<InstancedSpecies> instanced;         // instancedScatter: one InstanceBatch per species
    size_t next = 0;
    size_t nextBatch = 0;
    size_t nextInstanced = 0;
    bool tileCreated = false;
    std::string deltaName;                           // DeltaStore cell, empty when this tile does not take part
    std::set<std::string> mask;                      // removed instance keys the worker built against
};

constexpr uint32_t kCellBits = 28;
constexpr uint64_t kCellMask = (1ull << kCellBits) - 1;

int32_t signExtendCell(uint64_t bits) { return static_cast<int32_t>(static_cast<uint32_t>(bits << (32 - kCellBits))) >> (32 - kCellBits); }

std::string batchDeltaKey(const ScatterConfig& scatter, uint32_t species) { return scatterInstanceKey(scatter, species, 0, 0) + "/batch"; }

PreparedMesh prepareMesh(const Mesh& merged, bool clusters) {
    PreparedMesh out;
    out.base = merged.optimized();
    out.lods = out.base.generateLODChain();
    if (clusters && out.base.indices.size() / 3 >= kTerrainClusterMinTriangles) out.clusters = buildClusterMesh(out.base.vertices, kVertexFloats, out.base.indices);
    return out;
}
}

uint64_t scatterInstanceId(uint32_t species, int32_t cellX, int32_t cellZ) {
    return (static_cast<uint64_t>(species) << (2 * kCellBits)) | ((static_cast<uint64_t>(static_cast<uint32_t>(cellX)) & kCellMask) << kCellBits) | (static_cast<uint64_t>(static_cast<uint32_t>(cellZ)) & kCellMask);
}

InstanceData scatterInstanceData(const ScatterSpecies& species, const ScatterInstance& instance, const glm::vec3& origin) {
    InstanceData data;
    glm::quat rotation = glm::angleAxis(instance.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    if (species.alignToSlope) {
        const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), instance.normal);
        const float axisLength = glm::length(axis);
        if (axisLength > 1.0e-5f) rotation = glm::angleAxis(std::asin(std::min(axisLength, 1.0f)), axis / axisLength) * rotation;
    }
    data.position = instance.position - origin;
    data.rotation = rotation;
    data.scale = instance.scale;
    const uint64_t id = scatterInstanceId(instance.species, static_cast<int32_t>(instance.cellX), static_cast<int32_t>(instance.cellZ));
    data.seed = static_cast<uint32_t>((id * 0x9E3779B97F4A7C15ull) >> 40);
    // Subtle per-instance tint from the seed (surface shaders multiply instanceColor in).
    const float tint = 0.82f + 0.18f * instanceHash01(data.seed ^ 0x5bd1e995u);
    data.color = packInstanceColor(glm::vec4(tint, tint, tint, 1.0f));
    return data;
}

struct BatchRef {
    uint32_t species = 0;
    EntityHandle entity;
    uint32_t mesh = NO_MESH;
};

struct TerrainTileLoader::State {
    TerrainLoaderConfig config;
    World* world = nullptr;
    std::map<int, std::string> deltaCells;           // resident cells registered with the DeltaStore
    std::map<int, std::vector<EntityHandle>> registered;  // per-instance scatter entities registered as delta bases
    std::map<int, std::vector<BatchRef>> batchRefs;
    std::map<int, std::set<std::string>> appliedMask;
    std::map<int, bool> captured;
    std::map<int, std::vector<EntityHandle>> created;
    std::map<int, std::vector<uint32_t>> meshes;
    std::map<int, TileKey> keys;
    std::map<int, bool> visible;
    std::map<int, size_t> batchBytes;
    std::vector<uint32_t> speciesMeshes;  // instancedScatter: GPU mesh per species (resolved on first use)
};

namespace {
bool takesPart(const TerrainLoaderConfig& config, const TileKey& key) { return config.deltas && key.level <= config.scatterMaxLevel && !config.scatter.species.empty(); }

std::vector<ScatterInstance> withoutRemoved(const TerrainLoaderConfig& config, std::vector<ScatterInstance> instances, const std::set<std::string>& removed) {
    if (removed.empty()) return instances;
    std::erase_if(instances, [&](const ScatterInstance& instance) { return removed.contains(scatterInstanceKey(config.scatter, instance)); });
    return instances;
}
}

std::string terrainCellName(const TileKey& key) { return "terrain/" + std::to_string(key.x) + "_" + std::to_string(key.z) + "_" + std::to_string(key.level); }

// Rebuilds the merged batch meshes of a resident tile against the current removed set (main thread).
void TerrainTileLoader::rebuild(int cell) {
    State& s = *m_state;
    const auto keyIt = s.keys.find(cell);
    const auto nameIt = s.deltaCells.find(cell);
    if (keyIt == s.keys.end() || nameIt == s.deltaCells.end()) return;
    const std::set<std::string> removed = s.config.deltas->removedSnapshot(nameIt->second);
    if (s.config.instancedScatter) {
        // Dead flags follow the removed set: no mesh is rebuilt, only the touched cells of the instance buffer are uploaded.
        for (const BatchRef& ref : s.batchRefs[cell]) {
            InstanceBatch* batch = s.world->isAlive(ref.entity) ? s.world->get<InstanceBatch>(ref.entity) : nullptr;
            if (!batch) continue;
            for (uint32_t index = 0; index < batch->keys.size() && index < batch->instances.size(); ++index) {
                const uint64_t id = batch->keys[index];
                const std::string key = scatterInstanceKey(s.config.scatter, ref.species, signExtendCell(id >> kCellBits), signExtendCell(id));
                const bool dead = (batch->instances[index].flags & InstanceFlags::Dead) != 0;
                if (removed.contains(key) && !dead) batch->remove(index);
                else if (!removed.contains(key) && dead) batch->revive(index);
            }
        }
        s.appliedMask[cell] = removed;
        return;
    }
    if (!s.config.uploadScatterMesh) return;
    const std::vector<ScatterInstance> instances = withoutRemoved(s.config, scatterTile(s.config.terrain, s.config.scatter, keyIt->second), removed);
    std::vector<ScatterBatch> batches = scatterBatches(s.config.scatter, keyIt->second, instances, s.config.terrain);
    std::vector<BatchRef>& refs = s.batchRefs[cell];
    std::vector<uint32_t>& meshes = s.meshes[cell];
    for (auto ref = refs.begin(); ref != refs.end();) {
        const auto batch = std::find_if(batches.begin(), batches.end(), [&](const ScatterBatch& b) { return b.species == ref->species; });
        const uint32_t old = ref->mesh;
        if (batch == batches.end()) {
            // Every instance of this species is gone.
            if (s.world->isAlive(ref->entity)) s.world->destroy(ref->entity);
            std::erase(s.created[cell], ref->entity);
            ref = refs.erase(ref);
        } else {
            PreparedMesh prepared = prepareMesh(batch->mesh, s.config.scatterClusters);
            ref->mesh = s.config.uploadScatterMesh(prepared);
            if (ref->mesh != NO_MESH) meshes.push_back(ref->mesh);
            if (s.world->isAlive(ref->entity)) s.world->setMesh(ref->entity, ref->mesh);
            ++ref;
        }
        if (old != NO_MESH) {
            std::erase(meshes, old);
            if (s.config.releaseMesh) s.config.releaseMesh(old);
        }
    }
    s.appliedMask[cell] = removed;
}

TerrainTileLoader::TerrainTileLoader(World& world, TerrainLoaderConfig config) : m_state(std::make_shared<State>()) {
    m_state->config = std::move(config);
    m_state->world = &world;
    if (m_state->config.instancedScatter) registerInstanceBatchComponent(world);
}

TerrainTileLoader::~TerrainTileLoader() = default;

TerrainLoaderConfig& TerrainTileLoader::config() { return m_state->config; }

size_t TerrainTileLoader::entityCount(int cell) const {
    const auto it = m_state->created.find(cell);
    return it == m_state->created.end() ? 0 : it->second.size();
}

size_t TerrainTileLoader::scatterBatchBytes(int cell) const {
    const auto it = m_state->batchBytes.find(cell);
    return it == m_state->batchBytes.end() ? 0 : it->second;
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
        if (takesPart(s.config, *key)) {
            payload->deltaName = terrainCellName(*key);
            payload->mask = s.config.deltas->removedSnapshot(payload->deltaName);
        }
        if (s.config.instancedScatter) {
            // Removed instances stay in the batch as dead slots, so every instance keeps its index (and key) for later edits.
            const glm::vec3 origin = tileOrigin(s.config.terrain, *key);
            for (const ScatterInstance& instance : scatterTile(s.config.terrain, s.config.scatter, *key)) {
                auto group = std::find_if(payload->instanced.begin(), payload->instanced.end(), [&](const TilePayload::InstancedSpecies& g) { return g.species == instance.species; });
                if (group == payload->instanced.end()) {
                    payload->instanced.push_back({instance.species, {}, {}});
                    group = payload->instanced.end() - 1;
                }
                InstanceData data = scatterInstanceData(s.config.scatter.species[instance.species], instance, origin);
                if (!payload->mask.empty() && payload->mask.contains(scatterInstanceKey(s.config.scatter, instance))) data.flags |= InstanceFlags::Dead;
                group->instances.push_back(data);
                group->keys.push_back(scatterInstanceId(instance.species, static_cast<int32_t>(instance.cellX), static_cast<int32_t>(instance.cellZ)));
            }
            return payload;
        }
        const std::vector<ScatterInstance> instances = withoutRemoved(s.config, scatterTile(s.config.terrain, s.config.scatter, *key), payload->mask);
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
        if (!payload.deltaName.empty()) s.config.deltas->ignoreVisibility(payload.deltaName);
        s.keys[cell] = payload.key;
        s.visible[cell] = !hidden;
        payload.tileCreated = true;
        budget.spend(payload.mesh->mesh.vertices.size() * sizeof(float) + payload.mesh->mesh.indices.size() * sizeof(unsigned int));
        if (budget.exhausted() && (!payload.scatter.empty() || !payload.batches.empty() || !payload.instanced.empty())) return false;
    }
    while (payload.nextInstanced < payload.instanced.size()) {
        TilePayload::InstancedSpecies& group = payload.instanced[payload.nextInstanced++];
        const ScatterSpecies& species = s.config.scatter.species[group.species];
        if (s.speciesMeshes.size() < s.config.scatter.species.size()) s.speciesMeshes.assign(s.config.scatter.species.size(), NO_MESH - 1);
        uint32_t& mesh = s.speciesMeshes[group.species];
        if (mesh == NO_MESH - 1) {
            mesh = s.config.speciesMesh ? s.config.speciesMesh(group.species) : NO_MESH;
            if (mesh == NO_MESH) mesh = species.mesh;
        }
        EntityDesc desc;
        desc.name = species.name + " " + tileName(payload.key);
        desc.position = w.toLocal(glm::dvec3(tileOrigin(s.config.terrain, payload.key)));
        const EntityHandle handle = w.create(desc);
        InstanceBatch batch;
        batch.mesh = mesh;
        batch.material = species.material;
        batch.shader = species.shader;
        batch.settings.drawDistance = species.drawDistance;
        batch.settings.fadeStart = species.fadeStart;
        batch.settings.castShadows = species.castShadows;
        const size_t bytes = group.instances.size() * sizeof(InstanceGpu);
        batch.instances = std::move(group.instances);
        batch.keys = std::move(group.keys);
        batch.markLayout();
        w.add<InstanceBatch>(handle, std::move(batch));
        if (!species.tag.empty()) w.setTag(handle, species.tag);
        w.setLayer(handle, s.config.layer);
        if (hidden) w.setVisible(handle, false);
        s.created[cell].push_back(handle);
        s.batchRefs[cell].push_back({group.species, handle, NO_MESH});
        s.batchBytes[cell] += bytes;
        if (!payload.deltaName.empty()) {
            s.config.deltas->registerBase(payload.deltaName, batchDeltaKey(s.config.scatter, group.species), handle);
            s.registered[cell].push_back(handle);
        }
        budget.spend(bytes);
        if (budget.exhausted()) return false;
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
        s.batchRefs[cell].push_back({batch.species, handle, desc.mesh});
        const size_t batchBytes = payload.batches[index].mesh.vertices.size() * sizeof(float) * 2;
        s.batchBytes[cell] += batchBytes;
        budget.spend(batchBytes);
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
        if (!payload.deltaName.empty()) {
            s.config.deltas->registerBase(payload.deltaName, entity.key, handle);
            s.registered[cell].push_back(handle);
        }
        budget.spend(entity.bytes);
        if (budget.exhausted()) break;
    }
    const bool done = payload.next >= payload.scatter.size() && payload.nextBatch >= payload.batches.size() && payload.nextInstanced >= payload.instanced.size();
    if (done) finish(cell, &payload);
    return done;
}

// All of the tile is created: register the cell, apply stored deltas (changed/added instances), re-hide what the streamer hides, and catch up when the
// removed set changed after the worker built the batches.
void TerrainTileLoader::finish(int cell, void* raw) {
    State& s = *m_state;
    const TilePayload& payload = *static_cast<TilePayload*>(raw);
    if (payload.deltaName.empty()) return;
    s.config.deltas->applyCell(payload.deltaName);
    // Stored InstanceBatch diffs carry the mesh, material and shader ids of the session that saved them: this session's win.
    for (const BatchRef& ref : s.batchRefs[cell]) {
        InstanceBatch* batch = s.config.instancedScatter ? s.world->get<InstanceBatch>(ref.entity) : nullptr;
        if (!batch || ref.species >= s.speciesMeshes.size()) continue;
        const ScatterSpecies& species = s.config.scatter.species[ref.species];
        if (batch->mesh != s.speciesMeshes[ref.species] || batch->material != species.material || batch->shader != species.shader) {
            batch->mesh = s.speciesMeshes[ref.species];
            batch->material = species.material;
            batch->shader = species.shader;
            batch->markLayout();
        }
    }
    s.deltaCells[cell] = payload.deltaName;
    s.appliedMask[cell] = payload.mask;
    const bool shown = s.visible[cell];
    for (const EntityHandle handle : s.registered[cell]) {
        if (s.world->isAlive(handle)) s.world->setVisible(handle, shown);
    }
    if (s.config.deltas->removedSnapshot(payload.deltaName) != payload.mask) rebuild(cell);
}

std::string TerrainTileLoader::cellName(int cell) const {
    const auto it = m_state->deltaCells.find(cell);
    return it == m_state->deltaCells.end() ? std::string() : it->second;
}

std::optional<int> TerrainTileLoader::residentCellOf(const TileKey& key) const {
    for (const auto& [cell, name] : m_state->deltaCells) {
        const auto it = m_state->keys.find(cell);
        if (it != m_state->keys.end() && it->second == key) return cell;
    }
    return std::nullopt;
}

bool TerrainTileLoader::removeScatter(int cell, uint32_t species, int32_t cellX, int32_t cellZ) {
    State& s = *m_state;
    const auto it = s.deltaCells.find(cell);
    if (it == s.deltaCells.end() || species >= s.config.scatter.species.size()) return false;
    const std::string key = scatterInstanceKey(s.config.scatter, species, cellX, cellZ);
    if (s.config.instancedScatter) {
        // Free mask: the instance is marked dead in its batch (one cell re-uploaded), indices of the others are unchanged.
        if (InstanceBatch* batch = s.world->get<InstanceBatch>(scatterBatchEntity(cell, species))) {
            if (const auto index = batch->indexOfKey(scatterInstanceId(species, cellX, cellZ))) batch->remove(*index);
        }
        s.config.deltas->markRemoved(it->second, key);
        s.appliedMask[cell] = s.config.deltas->removedSnapshot(it->second);
        return true;
    }
    const EntityHandle handle = s.config.deltas->find(it->second, key);
    if (handle != NULL_ENTITY && s.world->isAlive(handle)) s.world->destroy(handle);
    s.config.deltas->markRemoved(it->second, key);
    const ScatterSpecies& sp = s.config.scatter.species[species];
    if (sp.geometry && s.config.uploadScatterMesh) rebuild(cell);
    else s.appliedMask[cell] = s.config.deltas->removedSnapshot(it->second);
    return true;
}

EntityHandle TerrainTileLoader::scatterBatchEntity(int cell, uint32_t species) const {
    const auto it = m_state->batchRefs.find(cell);
    if (it == m_state->batchRefs.end()) return NULL_ENTITY;
    for (const BatchRef& ref : it->second) {
        if (ref.species == species && m_state->world->isAlive(ref.entity)) return ref.entity;
    }
    return NULL_ENTITY;
}

bool TerrainTileLoader::moveScatter(int cell, uint32_t species, int32_t cellX, int32_t cellZ, const glm::vec3& worldPosition, const glm::quat& rotation, float scale) {
    State& s = *m_state;
    const auto keyIt = s.keys.find(cell);
    if (!s.config.instancedScatter || keyIt == s.keys.end()) return false;
    InstanceBatch* batch = s.world->get<InstanceBatch>(scatterBatchEntity(cell, species));
    if (!batch) return false;
    const auto index = batch->indexOfKey(scatterInstanceId(species, cellX, cellZ));
    if (!index) return false;
    InstanceData data = batch->instances[*index];
    data.position = worldPosition - tileOrigin(s.config.terrain, keyIt->second);
    data.rotation = rotation;
    data.scale = scale;
    batch->update(*index, std::span<const InstanceData>(&data, 1));
    return true;
}

bool TerrainTileLoader::trackAdded(int cell, EntityHandle root) {
    State& s = *m_state;
    const auto it = s.deltaCells.find(cell);
    if (it == s.deltaCells.end()) return false;
    s.config.deltas->trackAdded(it->second, root);
    return true;
}

void TerrainTileLoader::syncDeltas() {
    State& s = *m_state;
    if (!s.config.deltas) return;
    for (const auto& [cell, name] : std::map<int, std::string>(s.deltaCells)) {
        if (s.config.deltas->removedSnapshot(name) != s.appliedMask[cell]) rebuild(cell);
    }
}

bool TerrainTileLoader::unload(int cell, StreamBudget& budget) {
    State& s = *m_state;
    std::vector<EntityHandle>& doomed = s.created[cell];
    const auto deltaIt = s.deltaCells.find(cell);
    if (deltaIt != s.deltaCells.end() && !s.captured[cell]) {
        // Player changes are captured while the entities are still alive; entities the player added die with the tile and come back from the delta.
        s.captured[cell] = true;
        s.config.deltas->captureCell(deltaIt->second);
        for (const EntityHandle handle : s.config.deltas->liveEntities(deltaIt->second)) {
            if (std::find(doomed.begin(), doomed.end(), handle) == doomed.end()) doomed.push_back(handle);
        }
    }
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
    if (deltaIt != s.deltaCells.end()) s.config.deltas->releaseCell(deltaIt->second);
    s.deltaCells.erase(cell);
    s.registered.erase(cell);
    s.batchRefs.erase(cell);
    s.batchBytes.erase(cell);
    s.appliedMask.erase(cell);
    s.captured.erase(cell);
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
