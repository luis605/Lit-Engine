module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module Engine.Streaming;

import Engine.Delta;
import Engine.Portal;
import Engine.Render.entity;
import Engine.World;

namespace {
constexpr size_t kBytesPerDestroyedEntity = 64;

struct Batch {
    CellContent content;
    size_t next = 0;
};

struct BatchState {
    std::map<int, std::vector<EntityHandle>> doomed;
    std::map<int, std::vector<EntityHandle>> created;
    std::map<int, bool> collected;
};

void collectDoomed(BatchState& state, DeltaStore* deltas, const std::string& name, int cell) {
    std::vector<EntityHandle>& doomed = state.doomed[cell];
    doomed = std::move(state.created[cell]);
    state.created.erase(cell);
    state.collected[cell] = true;
    if (!deltas) return;
    for (EntityHandle handle : deltas->liveEntities(name)) {
        if (std::find(doomed.begin(), doomed.end(), handle) == doomed.end()) doomed.push_back(handle);
    }
}

bool commitBatch(World& world, DeltaStore* deltas, BatchState& state, const std::string& name, int cell, Batch& batch, StreamBudget& budget) {
    while (batch.next < batch.content.entities.size()) {
        const CellEntity& entity = batch.content.entities[batch.next++];
        const EntityHandle handle = world.create(entity.desc);
        if (!entity.tag.empty()) world.setTag(handle, entity.tag);
        world.setLayer(handle, entity.layer);
        if (!entity.visible) world.setVisible(handle, false);
        state.created[cell].push_back(handle);
        if (deltas) deltas->registerBase(name, entity.key, handle);
        budget.spend(entity.bytes);
        if (budget.exhausted()) break;
    }
    if (batch.next < batch.content.entities.size()) return false;
    if (deltas) deltas->applyCell(name);
    return true;
}

bool unloadBatch(World& world, DeltaStore* deltas, BatchState& state, const std::string& name, int cell, size_t destroyBatch, StreamBudget& budget) {
    if (!state.collected[cell]) collectDoomed(state, deltas, name, cell);
    std::vector<EntityHandle>& doomed = state.doomed[cell];
    const size_t batchSize = std::max<size_t>(destroyBatch, 1);
    while (!doomed.empty()) {
        const size_t count = std::min(batchSize, doomed.size());
        std::vector<EntityHandle> alive;
        alive.reserve(count);
        for (auto handle = doomed.end() - static_cast<std::ptrdiff_t>(count); handle != doomed.end(); ++handle) {
            if (world.isAlive(*handle)) alive.push_back(*handle);
        }
        doomed.resize(doomed.size() - count);
        world.destroyBatch(alive);
        budget.spend(count * kBytesPerDestroyedEntity);
        if (budget.exhausted() && !doomed.empty()) return false;
    }
    if (deltas) deltas->releaseCell(name);
    state.doomed.erase(cell);
    state.collected.erase(cell);
    return true;
}
}

CellLoader makeBatchLoader(World& world, PortalSystem& portals, DeltaStore* deltas, std::function<CellContent(int cell)> build, size_t destroyBatch) {
    auto state = std::make_shared<BatchState>();
    CellLoader loader;
    loader.prepare = [build](int cell) -> std::shared_ptr<void> {
        auto batch = std::make_shared<Batch>();
        batch->content = build(cell);
        return batch;
    };
    loader.commit = [&world, &portals, deltas, state](int cell, void* payload, StreamBudget& budget) {
        if (!payload) return true;
        const std::string& name = portals.cells()[static_cast<size_t>(cell)].name;
        return commitBatch(world, deltas, *state, name, cell, *static_cast<Batch*>(payload), budget);
    };
    loader.evicting = [&portals, deltas](int cell) {
        if (deltas) deltas->captureCell(portals.cells()[static_cast<size_t>(cell)].name);
    };
    loader.unload = [&world, &portals, deltas, state, destroyBatch](int cell, StreamBudget& budget) {
        const std::string& name = portals.cells()[static_cast<size_t>(cell)].name;
        return unloadBatch(world, deltas, *state, name, cell, destroyBatch, budget);
    };
    return loader;
}
