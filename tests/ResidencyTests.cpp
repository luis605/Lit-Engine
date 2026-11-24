#include "TestSupport.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

import Engine.World;
import Engine.Portal;
import Engine.Streaming;
import Engine.Terrain;
import Engine.mesh;
import Engine.glm;

namespace {
constexpr float kSpacing = 24.0f;
constexpr float kHalfWidth = 1.4f;
constexpr float kHalfHeight = 1.8f;
constexpr size_t kCellBytes = 1000;

void buildLine(PortalSystem& p, int count) {
    for (int i = 0; i < count; ++i) {
        PortalCell c;
        c.name = "C" + std::to_string(i);
        c.min = glm::vec3(kSpacing * static_cast<float>(i) - 5.4f, -0.4f, -5.4f);
        c.max = glm::vec3(kSpacing * static_cast<float>(i) + 5.4f, 4.4f, 5.4f);
        p.addCell(c);
    }
    auto end = [](const std::string& cell, float x, float yaw) {
        PortalEnd e;
        e.cell = cell;
        e.position = glm::vec3(x, kHalfHeight, 0.0f);
        e.eulerDegrees = glm::vec3(yaw, 0.0f, 0.0f);
        e.halfSize = glm::vec2(kHalfWidth, kHalfHeight);
        return e;
    };
    for (int i = 0; i + 1 < count; ++i) {
        const float x = kSpacing * static_cast<float>(i);
        p.addPair("P" + std::to_string(i), end("C" + std::to_string(i), x + 5.0f, -90.0f), end("C" + std::to_string(i + 1), x + kSpacing - 5.0f, 90.0f));
    }
}

struct Log {
    std::mutex mutex;
    std::vector<int> commits;
};

CellLoader makeLoader(const std::shared_ptr<Log>& log, std::chrono::milliseconds prepareDelay = std::chrono::milliseconds(0)) {
    CellLoader loader;
    loader.prepare = [prepareDelay](int cell) -> std::shared_ptr<void> {
        if (prepareDelay.count() > 0) std::this_thread::sleep_for(prepareDelay);
        return std::make_shared<int>(cell);
    };
    loader.commit = [log](int cell, void*, StreamBudget& budget) {
        budget.spend(kCellBytes);
        std::lock_guard lock(log->mutex);
        log->commits.push_back(cell);
        return true;
    };
    loader.unload = [](int, StreamBudget& budget) {
        budget.spend(16);
        return true;
    };
    return loader;
}

void setupWorld(World& w) { w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); }); }

TerrainConfig terrainConfig() {
    NoiseParams noise;
    noise.seed = 11;
    TerrainConfig config;
    config.height = makeNoiseHeight(noise);
    config.tileSize = 64.0f;
    config.tileCells = 16;
    config.minHeight = -200.0f;
    config.maxHeight = 200.0f;
    return config;
}

TerrainLoaderConfig terrainLoader(const std::shared_ptr<std::vector<int>>& order, const std::shared_ptr<std::atomic<int>>& uploads) {
    TerrainLoaderConfig loader;
    loader.terrain = terrainConfig();
    loader.uploadMesh = [order, uploads](const Mesh&) {
        const int id = ++*uploads;
        if (order) order->push_back(-id);  // negative: terrain upload, in commit order
        return static_cast<uint32_t>(id);
    };
    return loader;
}

TerrainStreamConfig terrainStream(bool deterministic) {
    TerrainStreamConfig stream;
    stream.lod.maxLevel = 2;
    stream.lod.viewDistance = 300.0f;
    stream.reselectInterval = 1;
    stream.evictDelay = 4;
    stream.deterministic = deterministic;
    return stream;
}

void testPriorityAcrossKinds() {
    ResidencyManager manager(1);
    manager.config().minSpeed = 4.0f;
    ResidencyDemand tile;
    tile.kind = ResidentKind::TerrainTile;
    tile.distance = 30.0f;
    ResidencyDemand cell;
    cell.kind = ResidentKind::PortalCell;
    cell.hops = 1;  // 40 m
    CHECK(manager.score(tile) < manager.score(cell));
    tile.distance = 100.0f;
    CHECK(manager.score(cell) < manager.score(tile));
    // Distance and hops are one currency: 40 m of tile distance equals one hop.
    tile.distance = 40.0f;
    CHECK(std::abs(manager.score(tile) - manager.score(cell)) < 0.01f);
    // Moving toward an item shortens its time-to-needed; an item behind does not benefit.
    ResidencyDemand ahead;
    ahead.kind = ResidentKind::TerrainTile;
    ahead.distance = 120.0f;
    ahead.direction = glm::vec3(1.0f, 0.0f, 0.0f);
    ResidencyDemand behind = ahead;
    behind.distance = 60.0f;
    behind.direction = glm::vec3(-1.0f, 0.0f, 0.0f);
    CHECK(manager.score(behind) < manager.score(ahead));
    manager.setVelocity(1, glm::vec3(30.0f, 0.0f, 0.0f));
    ahead.space = 1;
    behind.space = 1;
    CHECK(manager.score(ahead) < manager.score(behind));
    // Deterministic mode ignores velocity.
    manager.config().deterministic = true;
    CHECK(manager.score(behind) < manager.score(ahead));
    manager.config().deterministic = false;
    // Out of view is later; resident items keep their place (hysteresis); bigger breaks ties.
    ResidencyDemand seen = behind;
    ResidencyDemand hidden = behind;
    hidden.inView = false;
    CHECK(manager.score(seen) < manager.score(hidden));
    ResidencyDemand loaded = behind;
    loaded.resident = true;
    CHECK(manager.score(loaded) < manager.score(behind));

    // The commit queue runs across kinds in metric order, whoever offered first.
    std::vector<int> order;
    manager.offer(&order, ResidentKind::TerrainTile, 5.0f, [&](StreamBudget&) { order.push_back(5); });
    manager.offer(&order, ResidentKind::PortalCell, 1.0f, [&](StreamBudget&) { order.push_back(1); });
    manager.offer(&order, ResidentKind::GpuMesh, 3.0f, [&](StreamBudget&) { order.push_back(3); });
    manager.offer(&order, ResidentKind::GridCell, 2.0f, [&](StreamBudget&) { order.push_back(2); });
    StreamBudget budget;
    budget.unlimited = true;
    manager.flush(budget);
    CHECK((order == std::vector<int>{1, 2, 3, 5}));

    // The shared pool orders jobs by the same metric: one worker, blocked, then released.
    std::atomic<bool> release{false};
    std::mutex mutex;
    std::vector<int> ran;
    manager.jobs().submit(-1000000, [&] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto submit = [&](int id, ResidentKind kind, float distance, int hops) {
        ResidencyDemand demand;
        demand.kind = kind;
        demand.distance = distance;
        demand.hops = hops;
        manager.submit(demand, [&, id](bool) {
            std::lock_guard lock(mutex);
            ran.push_back(id);
        }, nullptr);
    };
    submit(1, ResidentKind::TerrainTile, 200.0f, 0);
    submit(2, ResidentKind::PortalCell, 0.0f, 1);   // 40 m
    submit(3, ResidentKind::TerrainTile, 10.0f, 0);
    submit(4, ResidentKind::GridCell, 0.0f, 3);     // 120 m
    release = true;
    manager.jobs().waitIdle();
    CHECK((ran == std::vector<int>{3, 2, 4, 1}));
}

void testCancellation() {
    ResidencyManager manager(1);
    std::atomic<bool> release{false};
    manager.jobs().submit(-1000000, [&] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto token = std::make_shared<std::atomic<bool>>(false);
    std::atomic<int> worked{0};
    std::atomic<int> reported{0};
    manager.jobs().submit(0, [&](bool cancelled) {
        if (!cancelled) ++worked;
        ++reported;
    }, token);
    auto kept = std::make_shared<std::atomic<bool>>(false);
    manager.jobs().submit(1, [&](bool cancelled) {
        if (!cancelled) ++worked;
        ++reported;
    }, kept);
    token->store(true);
    release = true;
    manager.jobs().waitIdle();
    CHECK(reported == 2);  // completion is always reported
    CHECK(worked == 1);    // the cancelled job did no work
    CHECK(manager.jobs().cancelledJobs() == 1);

    // Streaming: a cell evicted while still preparing is cancelled, never committed, and counted.
    PortalSystem portals;
    buildLine(portals, 8);
    StreamingConfig config;
    config.workers = 1;
    config.loadHops = 1;
    config.keepHops = 1;
    config.maxInFlight = 8;
    auto log = std::make_shared<Log>();
    StreamingSystem streaming(portals, config);
    streaming.setLoader(makeLoader(log, std::chrono::milliseconds(40)));
    streaming.update(0);
    streaming.update(0);
    streaming.update(7);  // far away before the slow prepares finished
    for (int i = 0; i < 400 && !streaming.settled(); ++i) {
        streaming.update(7);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(streaming.stats().cellsCancelled > 0);
    CHECK(streaming.stats().kinds[static_cast<size_t>(ResidentKind::PortalCell)].cancelled > 0);
    bool farCommitted = false;
    for (int cell : log->commits) farCommitted = farCommitted || cell < 5;
    CHECK(!farCommitted);
    CHECK(streaming.resident(7));

    // Terrain: tiles unwanted before their prepare started are cancelled and never become resident.
    World w;
    setupWorld(w);
    auto uploads = std::make_shared<std::atomic<int>>(0);
    TerrainStreamConfig stream = terrainStream(false);
    stream.workers = 1;
    stream.maxInFlight = 64;
    stream.evictDelay = 0;
    TerrainStreamer streamer(w, terrainLoader(nullptr, uploads), stream);
    streamer.update(glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
    for (int i = 1; i < 12; ++i) streamer.update(glm::vec3(4000.0f * static_cast<float>(i), 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
    streamer.residency().jobs().waitIdle();
    for (int i = 0; i < 50; ++i) streamer.update(glm::vec3(4000.0f * 11.0f, 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
    CHECK(streamer.stats().cancelled > 0 || streamer.residency().jobs().cancelledJobs() > 0);
}

void testMixedBudget() {
    PortalSystem portals;
    buildLine(portals, 6);
    StreamingConfig config;
    config.loadHops = 1;
    config.keepHops = 2;
    config.commitMillisPerFrame = 1000.0;
    config.uploadBytesPerFrame = 1500;  // one cell (1000) plus a bit: forces the queue to span frames
    auto log = std::make_shared<Log>();
    StreamingSystem streaming(portals, config);
    streaming.setLoader(makeLoader(log));
    World w;
    setupWorld(w);
    auto order = std::make_shared<std::vector<int>>();
    auto uploads = std::make_shared<std::atomic<int>>(0);
    TerrainStreamConfig stream = terrainStream(false);
    stream.lod.viewDistance = 200.0f;
    TerrainStreamer streamer(w, terrainLoader(order, uploads), stream);
    streamer.setSharedBudget([&streaming]() -> StreamBudget* { return &streaming.sharedBudget(); });
    streamer.attachResidency(streaming.residency());
    CHECK(&streamer.residency() == &streaming.residency());

    size_t maxSpentPerFrame = 0;
    for (int frame = 0; frame < 3000; ++frame) {
        streaming.update(0, glm::vec3(0.0f));
        streamer.update(glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
        maxSpentPerFrame = std::max(maxSpentPerFrame, streaming.sharedBudget().spent);
        streaming.recordFrame(16.0);
        if (streaming.settled() && streamer.settled()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(streaming.settled());
    CHECK(streamer.settled());
    CHECK(streamer.stats().sharedPool);
    CHECK(streamer.stats().sharedBudget);
    const StreamingStats stats = streaming.stats();
    const auto& cells = stats.kinds[static_cast<size_t>(ResidentKind::PortalCell)];
    const auto& tiles = stats.kinds[static_cast<size_t>(ResidentKind::TerrainTile)];
    CHECK(cells.resident >= 2);  // camera cell and its neighbour
    CHECK(tiles.resident > 0);
    CHECK(tiles.resident == streamer.stats().resident);
    CHECK(cells.loaded >= 2 && tiles.loaded > 0);
    CHECK(stats.residentBytes >= cells.residentBytes + tiles.residentBytes);
    // One budget: the per-frame spend stays near the shared limit (the first step of a flush may exceed it by one step), not cells plus tiles each.
    CHECK(maxSpentPerFrame < static_cast<size_t>(config.uploadBytesPerFrame) + 6 * (1u << 20));
    // Terrain and cells ran in one frame sequence: every kind that committed has a commit time recorded.
    CHECK(tiles.commitMillisMax >= 0.0);
}

void testDeterministic() {
    struct Run {
        std::vector<int> commits;
        std::vector<int> terrain;
        std::vector<std::vector<int>> cells;
        std::vector<std::vector<TileKey>> tiles;
    };
    const auto run = [](uint32_t workers) {
        Run result;
        PortalSystem portals;
        buildLine(portals, 10);
        StreamingConfig config;
        config.workers = workers;
        config.loadHops = 1;
        config.keepHops = 2;
        config.deterministic = true;
        auto log = std::make_shared<Log>();
        StreamingSystem streaming(portals, config);
        streaming.setLoader(makeLoader(log, workers > 1 ? std::chrono::milliseconds(1) : std::chrono::milliseconds(0)));
        World w;
        setupWorld(w);
        auto order = std::make_shared<std::vector<int>>();
        auto uploads = std::make_shared<std::atomic<int>>(0);
        TerrainStreamer streamer(w, terrainLoader(order, uploads), terrainStream(true));
        streamer.setSharedBudget([&streaming]() -> StreamBudget* { return &streaming.sharedBudget(); });
        streamer.attachResidency(streaming.residency());
        for (int step = 0; step < 40; ++step) {
            const float x = static_cast<float>(step) * 6.0f;
            const int cell = std::min(9, static_cast<int>((x + kSpacing * 0.5f) / kSpacing));
            streaming.update(cell, glm::vec3(x, 1.0f, 0.0f));
            streamer.update(glm::vec3(x, 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
            streaming.recordFrame(16.0);
            result.cells.push_back(streaming.residentCells());
            result.tiles.push_back(streamer.residentTiles());
        }
        result.commits = log->commits;
        result.terrain = *order;
        return result;
    };
    const Run a = run(1);
    const Run b = run(4);
    CHECK(!a.commits.empty() && !a.terrain.empty());
    CHECK(a.commits == b.commits);
    CHECK(a.terrain == b.terrain);
    CHECK(a.cells == b.cells);
    bool sameTiles = a.tiles.size() == b.tiles.size();
    for (size_t i = 0; sameTiles && i < a.tiles.size(); ++i) sameTiles = a.tiles[i] == b.tiles[i];
    CHECK(sameTiles);
    CHECK(a.cells.back().size() >= 2);
}

void testMemoryBudget() {
    PortalSystem portals;
    buildLine(portals, 8);
    StreamingConfig config;
    config.loadHops = 0;   // only the camera cell is wanted
    config.keepHops = 3;   // neighbours stay unless memory forces them out
    config.residency.byteBudget = 2 * kCellBytes + 500;
    auto log = std::make_shared<Log>();
    StreamingSystem streaming(portals, config);
    streaming.setLoader(makeLoader(log));
    const auto settle = [&](int cell) {
        for (int i = 0; i < 2000; ++i) {
            streaming.update(cell);
            if (streaming.settled() && streaming.stats().pendingCells == 0) {
                streaming.update(cell);
                if (streaming.settled()) return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    for (int cell = 0; cell < 6; ++cell) {
        settle(cell);
        CHECK(streaming.resident(cell));
        if (cell > 0) CHECK(streaming.resident(cell - 1));   // nearest non-wanted stays
        if (cell > 1) CHECK(!streaming.resident(cell - 2));  // farthest goes first
        CHECK(streaming.residency().residentBytes() <= config.residency.byteBudget);
    }
    CHECK(streaming.stats().memoryEvictions > 0);
    CHECK(streaming.stats().residentBytes <= config.residency.byteBudget);

    // Cross-kind eviction order at the registry level: highest score (least needed) first, protected never, any kind.
    ResidencyManager manager(1);
    manager.config().byteBudget = 250;
    std::vector<std::string> evicted;
    const auto add = [&](ResidentKind kind, uint64_t id, float score, bool protectedItem, const std::string& name) {
        manager.track(kind, id, 100, score, protectedItem, [&evicted, name] {
            evicted.push_back(name);
            return true;
        });
    };
    add(ResidentKind::TerrainTile, 1, 9.0f, false, "tile-far");
    add(ResidentKind::GpuMesh, 1, 7.0f, false, "mesh");
    add(ResidentKind::PortalCell, 1, 8.0f, false, "cell");
    add(ResidentKind::GridCell, 1, 20.0f, true, "grid-protected");
    manager.track(ResidentKind::ScatterBatch, 1, 100, 99.0f, false, nullptr);  // pinned: counts, never evicted
    CHECK(manager.residentBytes() == 500);
    CHECK(manager.enforceByteBudget() == 3);
    CHECK((evicted == std::vector<std::string>{"tile-far", "cell", "mesh"}));
    CHECK(manager.residentBytes() == 200);
    const ResidencyStats stats = manager.stats();
    CHECK(stats.memoryEvictions == 3);
    CHECK(stats.kinds[static_cast<size_t>(ResidentKind::TerrainTile)].evictedForMemory == 1);

    // Terrain tiles under a byte budget: tiles kept only by the eviction delay are unloaded early, wanted ones stay.
    World w;
    setupWorld(w);
    auto uploads = std::make_shared<std::atomic<int>>(0);
    TerrainStreamConfig stream = terrainStream(true);
    stream.evictDelay = 100000;
    TerrainStreamer streamer(w, terrainLoader(nullptr, uploads), stream);
    streamer.update(glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
    const uint32_t before = streamer.stats().resident;
    CHECK(before > 0);
    streamer.residency().config().byteBudget = static_cast<size_t>(streamer.stats().residentBytes) + 1;
    for (int i = 1; i <= 12; ++i) streamer.update(glm::vec3(100.0f * static_cast<float>(i), 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
    CHECK(streamer.stats().memoryEvicted > 0);
    for (int i = 0; i < 20; ++i) streamer.update(glm::vec3(1200.0f, 30.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), nullptr);
    CHECK(streamer.settled());
}

void testHitchAttribution() {
    ResidencyManager manager(1);
    StreamBudget budget;
    budget.unlimited = true;
    manager.offer(&manager, ResidentKind::TerrainTile, 1.0f, [](StreamBudget&) { std::this_thread::sleep_for(std::chrono::milliseconds(8)); });
    manager.offer(&manager, ResidentKind::PortalCell, 2.0f, [](StreamBudget&) {});
    manager.flush(budget);
    manager.recordFrame(50.0, 33.0);
    ResidencyStats stats = manager.stats();
    CHECK(stats.kinds[static_cast<size_t>(ResidentKind::TerrainTile)].hitches == 1);
    CHECK(stats.kinds[static_cast<size_t>(ResidentKind::PortalCell)].hitches == 0);
    CHECK(stats.kinds[static_cast<size_t>(ResidentKind::TerrainTile)].commitMillisMax >= 5.0);
    manager.recordFrame(50.0, 33.0);  // no commit work this frame: nobody blamed
    CHECK(manager.stats().kinds[static_cast<size_t>(ResidentKind::TerrainTile)].hitches == 1);
}
}

int main() {
    testPriorityAcrossKinds();
    testCancellation();
    testMixedBudget();
    testDeterministic();
    testMemoryBudget();
    testHitchAttribution();
    return testsupport::finish();
}
