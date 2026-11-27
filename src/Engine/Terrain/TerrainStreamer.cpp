module;

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

module Engine.Terrain;

import Engine.glm;
import Engine.Streaming;
import Engine.World;

namespace {
struct JobResult {
    std::atomic<bool> done{false};
    std::shared_ptr<void> payload;
};

enum class TileState : uint8_t { Loading, Ready, Committing, Resident, Unloading };

struct Tile {
    int cell = 0;
    TileKey key;
    TileState state = TileState::Loading;
    std::shared_ptr<JobResult> job;
    std::shared_ptr<void> payload;
    uint64_t lastWanted = 0;
    float distance = 0.0f;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    bool visible = false;
    bool cancelled = false;
    std::shared_ptr<std::atomic<bool>> cancel;
    size_t bytes = 0;
    bool tracked = false;
};

TileKey ancestorOf(const TileKey& key, int levels) { return TileKey{key.x >> levels, key.z >> levels, key.level + levels}; }

bool isAncestor(const TileKey& ancestor, const TileKey& key) { return ancestor.level > key.level && ancestorOf(key, ancestor.level - key.level) == ancestor; }
}

struct TerrainStreamer::Impl {
    World& world;
    TerrainStreamConfig config;
    std::mutex mutex;
    std::map<int, TileKey> byCell;
    std::unique_ptr<TerrainTileLoader> loader;
    std::map<TileKey, Tile> tiles;
    std::vector<SelectedTile> wanted;
    TerrainStats stats;
    uint64_t frame = 0;
    uint64_t lastSelect = 0;
    bool selected = false;
    glm::vec3 lastCamera{0.0f};
    glm::vec3 lastForward{0.0f, 0.0f, -1.0f};
    std::vector<TerrainViewpoint> views;
    std::vector<TerrainViewpoint> lastViews;
    int nextCell = 1;
    std::unique_ptr<ResidencyManager> ownResidency;
    ResidencyManager* res = nullptr;
    bool attached = false;
    std::function<StreamBudget*()> sharedProvider;

    Impl(World& w, TerrainLoaderConfig loaderConfig, TerrainStreamConfig streamConfig) : world(w), config(std::move(streamConfig)) {
        loaderConfig.startHidden = true;
        loaderConfig.tileOfCell = [this](int cell) -> std::optional<TileKey> {
            std::lock_guard lock(mutex);
            const auto it = byCell.find(cell);
            if (it == byCell.end()) return std::nullopt;
            return it->second;
        };
        loader = std::make_unique<TerrainTileLoader>(w, std::move(loaderConfig));
        ownResidency = std::make_unique<ResidencyManager>(std::max(config.workers, 1u));
        res = ownResidency.get();
    }

    ~Impl() {
        res->dropOwner(this);
        res->jobs().waitIdle();
        ownResidency->jobs().waitIdle();
        untrackAll();
    }

    [[nodiscard]] static uint64_t scatterId(int cell) { return static_cast<uint64_t>(cell); }

    void untrackAll() {
        for (auto& [key, tile] : tiles) untrackTile(tile);
    }

    void untrackTile(Tile& tile) {
        if (!tile.tracked) return;
        res->untrack(ResidentKind::TerrainTile, static_cast<uint64_t>(tile.cell));
        res->untrack(ResidentKind::ScatterBatch, scatterId(tile.cell));
        tile.tracked = false;
    }

    [[nodiscard]] float scoreOf(const Tile& tile) const {
        ResidencyDemand demand;
        demand.kind = ResidentKind::TerrainTile;
        demand.space = 1;
        demand.distance = tile.distance;
        demand.radius = tile.radius;
        demand.resident = true;
        const glm::vec3 to = tile.center - lastCamera;
        const float length = glm::length(to);
        if (length > 1.0e-3f) demand.direction = to / length;
        return res->score(demand);
    }

    void trackTile(Tile& tile) {
        const TileKey key = tile.key;
        const size_t scatter = loader->scatterBatchBytes(tile.cell);
        res->track(ResidentKind::TerrainTile, static_cast<uint64_t>(tile.cell), tile.bytes > scatter ? tile.bytes - scatter : 0, scoreOf(tile), true, [this, key]() -> bool {
            const auto it = tiles.find(key);
            if (it == tiles.end() || it->second.state != TileState::Resident) return false;
            it->second.state = TileState::Unloading;
            ++stats.memoryEvicted;
            return true;
        });
        if (scatter > 0) res->track(ResidentKind::ScatterBatch, scatterId(tile.cell), scatter, scoreOf(tile), true, nullptr);
        tile.tracked = true;
    }

    void rebind(ResidencyManager* target, bool isAttached) {
        res->dropOwner(this);
        untrackAll();
        res = target;
        attached = isAttached;
        for (auto& [key, tile] : tiles) {
            if (tile.state == TileState::Resident) trackTile(tile);
        }
    }

    [[nodiscard]] uint32_t countInFlight() const {
        uint32_t n = 0;
        for (const auto& [key, tile] : tiles) n += (tile.state == TileState::Loading || tile.state == TileState::Ready || tile.state == TileState::Committing) ? 1 : 0;
        return n;
    }

    void select(const glm::vec3& camera, const glm::vec3& forward, const std::array<glm::vec4, 6>* planes) {
        const float moved = glm::length(camera - lastCamera);
        const bool turned = glm::dot(forward, lastForward) < 0.9995f;
        const bool viewsChanged = !std::ranges::equal(views, lastViews, [&](const TerrainViewpoint& a, const TerrainViewpoint& b) { return glm::length(a.position - b.position) < config.moveThreshold; });
        if (selected && moved < config.moveThreshold && !turned && !viewsChanged && frame - lastSelect < config.reselectInterval) return;
        LodParams lod = config.lod;
        lod.extraViews = views;
        lastViews = views;
        const size_t before = selectTerrainTiles(loader->config().terrain, camera, [&] {
            LodParams unculled = lod;
            unculled.frustumCull = false;
            return unculled;
        }()).size();
        if (planes) {
            lod.frustumCull = true;
            lod.planes = *planes;
        }
        wanted = selectTerrainTiles(loader->config().terrain, camera, lod);
        if (ownResidency.get() == res) res->config().deterministic = config.deterministic;
        std::vector<std::pair<float, SelectedTile>> ranked;
        ranked.reserve(wanted.size());
        for (const SelectedTile& tile : wanted) ranked.emplace_back(selectedScore(tile), tile);
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t index = 0; index < wanted.size(); ++index) wanted[index] = ranked[index].second;
        stats.wanted = static_cast<uint32_t>(wanted.size());
        stats.culledByFrustum = static_cast<uint32_t>(before > wanted.size() ? before - wanted.size() : 0);
        lastCamera = camera;
        lastForward = forward;
        lastSelect = frame;
        selected = true;
        for (const SelectedTile& tile : wanted) {
            const auto it = tiles.find(tile.key);
            if (it != tiles.end()) {
                it->second.lastWanted = frame;
                it->second.distance = tile.distance;
                it->second.center = (tile.boundsMin + tile.boundsMax) * 0.5f;
                it->second.radius = 0.5f * glm::length(tile.boundsMax - tile.boundsMin);
            }
        }
    }

    [[nodiscard]] float selectedScore(const SelectedTile& tile) const {
        ResidencyDemand demand;
        demand.kind = ResidentKind::TerrainTile;
        demand.space = 1;
        demand.distance = tile.distance;
        demand.radius = 0.5f * glm::length(tile.boundsMax - tile.boundsMin);
        demand.resident = tiles.contains(tile.key);
        const glm::vec3 to = (tile.boundsMin + tile.boundsMax) * 0.5f - lastCamera;
        const float length = glm::length(to);
        if (length > 1.0e-3f) demand.direction = to / length;
        return res->score(demand);
    }

    void startLoads() {
        uint32_t inFlight = countInFlight();
        for (const SelectedTile& selectedTile : wanted) {
            if (inFlight >= config.maxInFlight) break;
            auto it = tiles.find(selectedTile.key);
            if (it != tiles.end()) {
                it->second.lastWanted = frame;
                continue;
            }
            Tile tile;
            tile.cell = nextCell++;
            tile.key = selectedTile.key;
            tile.lastWanted = frame;
            tile.distance = selectedTile.distance;
            tile.center = (selectedTile.boundsMin + selectedTile.boundsMax) * 0.5f;
            tile.radius = 0.5f * glm::length(selectedTile.boundsMax - selectedTile.boundsMin);
            tile.job = std::make_shared<JobResult>();
            tile.cancel = std::make_shared<std::atomic<bool>>(false);
            {
                std::lock_guard lock(mutex);
                byCell[tile.cell] = tile.key;
            }
            auto job = tile.job;
            TerrainTileLoader* loaderPtr = loader.get();
            const int cell = tile.cell;
            res->jobs().submit(ResidencyManager::poolPriority(selectedScore(selectedTile)), [job, loaderPtr, cell](bool cancelled) {
                if (!cancelled) job->payload = loaderPtr->prepare(cell);
                job->done.store(true, std::memory_order_release);
            }, tile.cancel);
            tiles.emplace(tile.key, std::move(tile));
            ++inFlight;
        }
    }

    void pollLoading() {
        for (auto it = tiles.begin(); it != tiles.end();) {
            Tile& tile = it->second;
            if (tile.state != TileState::Loading || !tile.job->done.load(std::memory_order_acquire)) {
                ++it;
                continue;
            }
            if (tile.cancelled) {
                {
                    std::lock_guard lock(mutex);
                    byCell.erase(tile.cell);
                }
                res->noteCancelled(ResidentKind::TerrainTile);
                ++stats.cancelled;
                it = tiles.erase(it);
                continue;
            }
            tile.payload = std::move(tile.job->payload);
            tile.job.reset();
            tile.state = TileState::Ready;
            ++it;
        }
    }

    void commitTile(const TileKey& key, StreamBudget& budget) {
        const auto it = tiles.find(key);
        if (it == tiles.end()) return;
        Tile& tile = it->second;
        if (tile.state != TileState::Ready && tile.state != TileState::Committing) return;
        tile.state = TileState::Committing;
        const size_t before = budget.spent;
        const bool done = loader->commit(tile.cell, tile.payload.get(), budget);
        tile.bytes += budget.spent - before;
        if (!done) return;
        tile.payload.reset();
        tile.state = TileState::Resident;
        ++stats.loaded;
        res->noteLoaded(ResidentKind::TerrainTile, tile.bytes);
        trackTile(tile);
    }

    void offerCommits() {
        for (auto& [key, tile] : tiles) {
            if (tile.state != TileState::Ready && tile.state != TileState::Committing) continue;
            const TileKey tileKey = key;
            res->offer(this, ResidentKind::TerrainTile, scoreOf(tile), [this, tileKey](StreamBudget& budget) { commitTile(tileKey, budget); });
        }
    }

    std::set<TileKey> visibleSet(std::set<TileKey>* protectedTiles) {
        std::set<TileKey> vis;
        const auto resident = [&](const TileKey& key) {
            const auto it = tiles.find(key);
            return it != tiles.end() && it->second.state == TileState::Resident;
        };
        for (const SelectedTile& leaf : wanted) {
            if (resident(leaf.key)) {
                vis.insert(leaf.key);
                continue;
            }
            bool found = false;
            for (int up = 1; up <= config.lod.maxLevel - leaf.key.level; ++up) {
                const TileKey ancestor = ancestorOf(leaf.key, up);
                if (resident(ancestor)) {
                    vis.insert(ancestor);
                    found = true;
                    break;
                }
            }
            if (found) continue;
            for (const auto& [key, tile] : tiles) {
                if (tile.state == TileState::Resident && isAncestor(leaf.key, key)) vis.insert(key);
            }
        }
        if (protectedTiles) *protectedTiles = vis;
        std::set<TileKey> result;
        for (const TileKey& key : vis) {
            bool covered = false;
            for (const TileKey& other : vis) {
                if (isAncestor(other, key)) {
                    covered = true;
                    break;
                }
            }
            if (!covered) result.insert(key);
        }
        return result;
    }

    void evictAndUnload(StreamBudget& budget, const std::set<TileKey>& protectedTiles) {
        std::set<TileKey> wantedKeys;
        for (const SelectedTile& tile : wanted) wantedKeys.insert(tile.key);
        size_t residentCount = 0;
        for (const auto& [key, tile] : tiles) residentCount += tile.state == TileState::Resident ? 1 : 0;
        std::vector<Tile*> candidates;
        for (auto& [key, tile] : tiles) {
            if (tile.state != TileState::Resident && tile.state != TileState::Loading && tile.state != TileState::Ready) continue;
            if (wantedKeys.contains(key) || protectedTiles.contains(key)) continue;
            candidates.push_back(&tile);
        }
        std::sort(candidates.begin(), candidates.end(), [](const Tile* a, const Tile* b) { return a->lastWanted < b->lastWanted; });
        for (auto& [key, tile] : tiles) {
            if (tile.state == TileState::Resident && tile.tracked) res->update(ResidentKind::TerrainTile, static_cast<uint64_t>(tile.cell), scoreOf(tile), wantedKeys.contains(key) || protectedTiles.contains(key));
        }
        res->enforceByteBudget();
        for (Tile* tile : candidates) {
            const bool aged = frame - tile->lastWanted > config.evictDelay;
            const bool overCap = residentCount > config.maxResidentTiles || loader->meshCount() > config.maxGpuMeshes;
            if (!aged && !overCap) continue;
            if (tile->state == TileState::Loading) {
                if (!tile->cancelled) {
                    tile->cancelled = true;
                    if (tile->cancel) tile->cancel->store(true, std::memory_order_release);
                }
                continue;
            }
            if (tile->state == TileState::Ready) {
                tile->payload.reset();
                {
                    std::lock_guard lock(mutex);
                    byCell.erase(tile->cell);
                }
                res->noteCancelled(ResidentKind::TerrainTile);
                ++stats.cancelled;
                tiles.erase(tile->key);
                ++stats.evicted;
                continue;
            }
            tile->state = TileState::Unloading;
            if (overCap) --residentCount;
        }
        for (auto it = tiles.begin(); it != tiles.end();) {
            Tile& tile = it->second;
            if (tile.state != TileState::Unloading) {
                ++it;
                continue;
            }
            if (budget.exhausted() && !budget.unlimited) {
                ++it;
                continue;
            }
            if (loader->unload(tile.cell, budget)) {
                untrackTile(tile);
                res->noteUnloaded(ResidentKind::TerrainTile);
                {
                    std::lock_guard lock(mutex);
                    byCell.erase(tile.cell);
                }
                ++stats.evicted;
                it = tiles.erase(it);
            } else {
                ++it;
            }
        }
    }

    void update(const glm::vec3& camera, const glm::vec3& forward, const std::array<glm::vec4, 6>* planes, bool unlimited) {
        const auto t0 = std::chrono::steady_clock::now();
        ++frame;
        res->observeCamera(1, camera);
        select(camera, forward, planes);
        startLoads();
        if (config.deterministic) {
            res->jobs().waitIdle();
            unlimited = true;
        }
        pollLoading();
        loader->syncDeltas();
        StreamBudget ownCommit;
        ownCommit.millis = config.commitMillisPerFrame;
        ownCommit.bytes = config.uploadBytesPerFrame;
        ownCommit.unlimited = unlimited;
        StreamBudget ownUnload = ownCommit;
        StreamBudget* shared = !unlimited && sharedProvider ? sharedProvider() : nullptr;
        stats.sharedBudget = shared != nullptr;
        stats.sharedPool = attached;
        StreamBudget& budget = shared ? *shared : ownCommit;
        StreamBudget& unloadBudget = shared ? *shared : ownUnload;
        res->dropOwner(this);
        if (loader->meshCount() < config.maxGpuMeshes + 40) offerCommits();
        res->arrive(this, budget);
        std::set<TileKey> protectedTiles;
        const std::set<TileKey> visible = visibleSet(&protectedTiles);
        evictAndUnload(unloadBudget, protectedTiles);
        uint32_t resident = 0;
        uint32_t loading = 0;
        uint32_t shown = 0;
        for (auto& [key, tile] : tiles) {
            if (tile.state == TileState::Resident) {
                ++resident;
                const bool show = visible.contains(key);
                tile.visible = show;
                loader->setVisible(tile.cell, show);
                shown += show ? 1 : 0;
            } else if (tile.state != TileState::Unloading) {
                ++loading;
            }
        }
        stats.resident = resident;
        stats.visible = shown;
        stats.loading = loading;
        stats.gpuMeshes = static_cast<uint32_t>(loader->meshCount());
        stats.residentBytes = res->residentBytes();
        res->setPending(ResidentKind::TerrainTile, loading);
        uint32_t entities = 0;
        for (const auto& [key, tile] : tiles) entities += static_cast<uint32_t>(loader->entityCount(tile.cell));
        stats.entities = entities;
        stats.updateMillis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    [[nodiscard]] bool isSettled() const {
        for (const SelectedTile& leaf : wanted) {
            const auto it = tiles.find(leaf.key);
            if (it == tiles.end() || it->second.state != TileState::Resident) return false;
        }
        return selected;
    }
};

TerrainStreamer::TerrainStreamer(World& world, TerrainLoaderConfig loader, TerrainStreamConfig config) : m_impl(std::make_unique<Impl>(world, std::move(loader), std::move(config))) {}

TerrainStreamer::~TerrainStreamer() = default;

void TerrainStreamer::update(const glm::vec3& cameraWorld, const glm::vec3& cameraForward, const std::array<glm::vec4, 6>* planes, std::vector<TerrainViewpoint> extraViews) {
    m_impl->views = std::move(extraViews);
    m_impl->update(cameraWorld, cameraForward, planes, false);
}

void TerrainStreamer::prime(const glm::vec3& cameraWorld, const glm::vec3& cameraForward, const std::array<glm::vec4, 6>* planes, std::vector<TerrainViewpoint> extraViews) {
    m_impl->views = std::move(extraViews);
    const auto start = std::chrono::steady_clock::now();
    m_impl->selected = false;
    do {
        m_impl->update(cameraWorld, cameraForward, planes, true);
        if (m_impl->isSettled()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() - start < std::chrono::seconds(60));
}

const TerrainStats& TerrainStreamer::stats() const { return m_impl->stats; }

std::vector<TileKey> TerrainStreamer::residentTiles() const {
    std::vector<TileKey> out;
    for (const auto& [key, tile] : m_impl->tiles) {
        if (tile.state == TileState::Resident) out.push_back(key);
    }
    return out;
}

std::vector<TileKey> TerrainStreamer::visibleTiles() const {
    std::vector<TileKey> out;
    for (const auto& [key, tile] : m_impl->tiles) {
        if (tile.state == TileState::Resident && tile.visible) out.push_back(key);
    }
    return out;
}

std::vector<SelectedTile> TerrainStreamer::wantedTiles() const { return m_impl->wanted; }

bool TerrainStreamer::settled() const { return m_impl->isSettled(); }

TerrainConfig& TerrainStreamer::terrain() { return m_impl->loader->config().terrain; }

TerrainStreamConfig& TerrainStreamer::config() { return m_impl->config; }

TerrainTileLoader& TerrainStreamer::loader() { return *m_impl->loader; }

void TerrainStreamer::setSharedBudget(std::function<StreamBudget*()> provider) { m_impl->sharedProvider = std::move(provider); }

void TerrainStreamer::attachResidency(ResidencyManager& manager) { m_impl->rebind(&manager, true); }

void TerrainStreamer::detachResidency() { m_impl->rebind(m_impl->ownResidency.get(), false); }

ResidencyManager& TerrainStreamer::residency() { return *m_impl->res; }
