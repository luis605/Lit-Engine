module;

#include <array>
#include <atomic>
#include <limits>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

export module Engine.Streaming;

import Engine.glm;
import Engine.Delta;
import Engine.Portal;
import Engine.Render.entity;
import Engine.World;

export class AsyncJobs {
  public:
    explicit AsyncJobs(size_t workers = 2);
    ~AsyncJobs();
    AsyncJobs(const AsyncJobs&) = delete;
    AsyncJobs& operator=(const AsyncJobs&) = delete;

    void submit(int priority, std::function<void()> job);
    // Cancellable job: always called exactly once, with `cancelled` true when `cancel` was set before a worker picked it up (skip the work, still
    // publish completion). A job that already runs is not interrupted; it can poll the token itself.
    void submit(int priority, std::function<void(bool cancelled)> job, std::shared_ptr<std::atomic<bool>> cancel);
    [[nodiscard]] size_t pending() const;
    [[nodiscard]] uint64_t cancelledJobs() const { return m_cancelled.load(); }
    void waitIdle();

  private:
    struct Item {
        int priority = 0;
        uint64_t order = 0;
        std::function<void(bool)> fn;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    void loop();

    std::vector<std::thread> m_threads;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::vector<Item> m_queue;
    uint64_t m_order = 0;
    size_t m_running = 0;
    bool m_stop = false;
    std::atomic<uint64_t> m_cancelled{0};
};

export struct StreamBudget {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double millis = 1.0;
    size_t bytes = 4u << 20;
    size_t spent = 0;
    bool unlimited = false;

    [[nodiscard]] bool exhausted() const {
        if (unlimited) return false;
        return spent >= bytes || std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= millis;
    }
    void spend(size_t amount) { spent += amount; }
};

// ---------------------------------------------------------------- unified residency

// Everything that streams in: portal cells, spatial grid cells (portal-less cells), terrain tiles, GPU mesh assets, merged scatter batches.
export enum class ResidentKind : uint8_t { PortalCell, GridCell, TerrainTile, GpuMesh, ScatterBatch };
export constexpr size_t kResidentKinds = 5;
export [[nodiscard]] const char* residentKindName(ResidentKind kind);

// Tunables of the single priority metric and of the memory budget.
export struct ResidencyConfig {
    // One portal hop counts as this many metres of distance (portal cells have no metric distance of their own).
    float hopMetres = 40.0f;
    // Floor of the closing speed (m/s): at rest the metric is distance / minSpeed, so ordering is by distance.
    float minSpeed = 4.0f;
    // Added to the time-to-needed of items outside the view.
    float outOfViewSeconds = 2.0f;
    // Items already resident or loading keep their place: their time is multiplied by this (<1) so they are not swapped for equal newcomers.
    float residentBias = 0.75f;
    // Seconds added per kind (e.g. make GPU mesh requests slightly less urgent than visible tiles).
    std::array<float, kResidentKinds> kindBias{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    // Resident bytes (CPU payloads in flight excluded) over which the least important unprotected items are evicted, across all kinds. 0: unlimited.
    size_t byteBudget = 0;
    // Ignore camera velocity and wall-clock effects: priority depends on positions only (headless, replays, tests).
    bool deterministic = false;
};

// Inputs of the metric. Lower score = needed sooner. `space` selects the camera velocity (0 world-local, 1 absolute world) because streaming
// positions are floating-origin local while terrain uses absolute coordinates; velocity is frame independent, so a shift does not matter.
export struct ResidencyDemand {
    ResidentKind kind = ResidentKind::PortalCell;
    int space = 0;
    glm::vec3 direction{0.0f};   // unit vector from the camera to the item (zero: unknown)
    float distance = 0.0f;       // metres to the nearest point of the item, 0 when inside
    float radius = 0.0f;         // bounding radius, only used as screen-space importance tie break
    int hops = 0;                // portal graph hops to the camera cell
    bool inView = true;
    bool resident = false;
};

export struct ResidencyKindStats {
    uint32_t resident = 0;
    uint32_t pending = 0;
    uint64_t loaded = 0;
    uint64_t unloaded = 0;
    uint64_t cancelled = 0;
    uint64_t evictedForMemory = 0;
    uint64_t bytesCommitted = 0;
    uint64_t residentBytes = 0;
    uint64_t hitches = 0;         // frames over the hitch threshold in which this kind spent the most commit time
    double commitMillis = 0.0;    // last frame
    double commitMillisMax = 0.0;
};

export struct ResidencyStats {
    std::array<ResidencyKindStats, kResidentKinds> kinds;
    uint64_t residentBytes = 0;
    uint64_t byteBudget = 0;
    uint64_t memoryEvictions = 0;
    uint64_t jobsCancelled = 0;
    uint64_t commitsRun = 0;
    uint64_t commitsDeferred = 0;
};

// One residency manager for all kinds: the worker pool (AsyncJobs, cancellable, priority = metric), the per-frame commit/unload StreamBudget,
// the commit queue ordered by the metric across kinds, the byte budget with eviction in metric order, and per-kind statistics. Domain code
// (portal graph BFS and rings, terrain quadtree, mesh LOD requests) still decides *what* is wanted; this decides *in which order* and *how much*.
export class ResidencyManager {
  public:
    explicit ResidencyManager(size_t workers = 2, ResidencyConfig config = {});
    ~ResidencyManager();
    ResidencyManager(const ResidencyManager&) = delete;
    ResidencyManager& operator=(const ResidencyManager&) = delete;

    [[nodiscard]] AsyncJobs& jobs() { return m_jobs; }
    [[nodiscard]] ResidencyConfig& config() { return m_config; }
    [[nodiscard]] const ResidencyConfig& config() const { return m_config; }

    // Camera velocity per space from successive positions (wall clock). Jumps above 1000 m/s (teleport, origin shift) reset the estimate.
    void observeCamera(int space, const glm::vec3& position);
    void setVelocity(int space, const glm::vec3& velocity);
    [[nodiscard]] glm::vec3 velocity(int space) const;

    // The metric: estimated seconds until the item is needed (distance plus hop distance over the closing speed, out-of-view and kind penalties,
    // resident bias), minus a tiny size term so bigger items win ties. Comparable across kinds.
    [[nodiscard]] float score(const ResidencyDemand& demand) const;
    // Worker pool priority of a score (lower first).
    [[nodiscard]] static int poolPriority(float score);

    // Submits a prepare job to the shared pool with the metric as priority; set *cancel to skip it if it did not start.
    void submit(const ResidencyDemand& demand, std::function<void(bool cancelled)> job, std::shared_ptr<std::atomic<bool>> cancel);

    // Per-frame budget of everything that streams. Reset when `frame` changes (or when older than a frame could be).
    [[nodiscard]] StreamBudget& frameBudget(uint64_t frame, double millis, size_t bytes);

    // Commit queue: producers offer resumable steps, a flush runs them in metric order across kinds until the budget is exhausted (the first
    // always runs, so oversized items progress). Unfinished steps are dropped at flush; producers re-offer every update. `owner` ties a task
    // to its producer so a destroyed producer never leaves a dangling task.
    void offer(const void* owner, ResidentKind kind, float score, std::function<void(StreamBudget&)> run);
    size_t flush(StreamBudget& budget);
    void dropOwner(const void* owner);
    // A producer calls this after offering. The queue flushes when every producer that was active last frame has arrived this frame (or the
    // budget is unlimited, as in prime), so portal cells and terrain tiles compete in one metric-ordered queue whichever system runs first,
    // and a producer that stops ticking never blocks the others. Returns true when it flushed.
    bool arrive(const void* owner, StreamBudget& budget);

    // Resident item registry (memory budget). `protectedItem` items (wanted now) are never chosen; items without `evict` count but are pinned.
    void track(ResidentKind kind, uint64_t id, size_t bytes, float score, bool protectedItem, std::function<bool()> evict);
    void update(ResidentKind kind, uint64_t id, float score, bool protectedItem);
    void addBytes(ResidentKind kind, uint64_t id, size_t bytes);
    void untrack(ResidentKind kind, uint64_t id);
    [[nodiscard]] bool tracked(ResidentKind kind, uint64_t id) const;
    [[nodiscard]] size_t residentBytes() const;
    // Evicts unprotected items, least important (highest score) first, until residentBytes() <= byteBudget. Returns the number requested.
    size_t enforceByteBudget();

    void noteLoaded(ResidentKind kind, size_t bytes);
    void noteUnloaded(ResidentKind kind);
    void noteCancelled(ResidentKind kind);
    void setPending(ResidentKind kind, uint32_t count);
    // Frame time hook: attributes a hitch to the kind with the most commit time in that frame, then clears the frame's commit times.
    void recordFrame(double millis, double hitchMillis);
    [[nodiscard]] ResidencyStats stats() const;
    void resetStats();

  private:
    struct Task {
        const void* owner = nullptr;
        ResidentKind kind = ResidentKind::PortalCell;
        float score = 0.0f;
        uint64_t order = 0;
        std::function<void(StreamBudget&)> run;
    };
    struct Entry {
        size_t bytes = 0;
        float score = 0.0f;
        bool protectedItem = false;
        bool evicting = false;
        std::function<bool()> evict;
    };
    struct Tracker {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        std::chrono::steady_clock::time_point time{};
        bool valid = false;
    };

    ResidencyConfig m_config;
    AsyncJobs m_jobs;
    std::array<Tracker, 2> m_trackers;
    StreamBudget m_budget;
    uint64_t m_budgetFrame = ~0ull;
    std::vector<Task> m_tasks;
    uint64_t m_order = 0;
    std::map<const void*, uint64_t> m_arrivals;
    std::map<std::pair<uint8_t, uint64_t>, Entry> m_entries;
    ResidencyStats m_stats;
    std::array<double, kResidentKinds> m_frameCommit{};
    std::array<uint32_t, kResidentKinds> m_pending{};
};

export struct SpatialStreamingConfig {
    bool enabled = false;
    float loadRadius = 150.0f;
    float keepRadius = 200.0f;
    std::vector<float> lodRadii;
    float lodHysteresis = 10.0f;
    bool reloadOnLodChange = true;
    float moveThreshold = 1.0f;
};

export struct StreamingConfig {
    SpatialStreamingConfig spatial;
    int loadHops = 1;
    int keepHops = 2;
    uint32_t workers = 2;
    uint32_t maxInFlight = 4;
    double commitMillisPerFrame = 1.0;
    size_t uploadBytesPerFrame = 4u << 20;
    double hitchMillis = 33.0;
    double primeTimeoutMillis = 20000.0;
    uint32_t window = 600;
    // Priority metric and memory budget shared by every kind (cells, assets, terrain tiles attached to this system).
    ResidencyConfig residency;
    // Headless/replay mode: update waits for the workers and commits without a time budget until nothing is pending, and the metric ignores
    // camera velocity, so residency depends only on the camera path.
    bool deterministic = false;
};

export struct CellLoader {
    std::function<std::shared_ptr<void>(int cell)> prepare;
    std::function<std::shared_ptr<void>(int cell, int lod)> prepareLod;
    std::function<bool(int cell, void* payload, StreamBudget& budget)> commit;
    std::function<bool(int cell, StreamBudget& budget)> unload;
    std::function<void(int cell)> resident;
    std::function<void(int cell)> evicting;
};

export struct AssetRequest {
    std::string key;
    int priority = 0;
    size_t bytes = 0;
    std::function<std::shared_ptr<void>()> load;
    std::function<void(std::shared_ptr<void>)> finish;
    // Metres to the camera for the unified metric; negative: unknown, `priority` is then read as portal hops.
    float distance = -1.0f;
    ResidentKind kind = ResidentKind::GpuMesh;
};

export enum class CellState : uint8_t { Unloaded, Loading, Ready, Committing, Resident, Unloading };

export struct StreamingStats {
    uint64_t frames = 0;
    uint64_t hitches = 0;
    double frameP50 = 0.0;
    double frameP99 = 0.0;
    double frameMax = 0.0;
    double updateMillis = 0.0;
    double updateMaxMillis = 0.0;
    uint32_t updateWork = 0;
    uint32_t updateWorkMax = 0;
    uint64_t bytesCommitted = 0;
    uint64_t cellsLoaded = 0;
    uint64_t cellsUnloaded = 0;
    uint64_t cellsCancelled = 0;
    uint64_t lodReloads = 0;
    uint64_t assetsLoaded = 0;
    uint32_t pendingCells = 0;
    uint32_t pendingAssets = 0;
    uint32_t residentCells = 0;
    // Unified residency (all kinds, including terrain tiles when the TerrainStreamer is attached to this system).
    std::array<ResidencyKindStats, kResidentKinds> kinds;
    uint64_t residentBytes = 0;
    uint64_t memoryEvictions = 0;
    uint64_t jobsCancelled = 0;
    uint64_t hitchesByKind(ResidentKind kind) const { return kinds[static_cast<size_t>(kind)].hitches; }
};

export class StreamingSystem {
  public:
    StreamingSystem(PortalSystem& portals, StreamingConfig config = {});
    ~StreamingSystem();

    void setLoader(CellLoader loader) { m_loader = std::move(loader); }
    [[nodiscard]] StreamingConfig& config() { return m_config; }
    [[nodiscard]] const StreamingConfig& config() const { return m_config; }

    void update(int cameraCell);
    void update(int cameraCell, const glm::vec3& cameraPosition);
    bool prime(int cameraCell);
    bool prime(int cameraCell, const glm::vec3& cameraPosition);
    [[nodiscard]] int lod(int cell) const;
    [[nodiscard]] float spatialDistance(int cell) const;
    [[nodiscard]] bool wanted(int cell) const;
    void recordFrame(double millis);
    // Per-frame budget shared by everything that streams in one frame (cells, assets, TerrainStreamer). It is reset from the config on the first
    // call after recordFrame (or when older than a frame could be without recordFrame), so a second consumer sees what the first one left.
    [[nodiscard]] StreamBudget& sharedBudget();
    void requestAsset(AssetRequest request);
    [[nodiscard]] bool assetReady(const std::string& key) const;

    [[nodiscard]] CellState state(int cell) const;
    [[nodiscard]] bool resident(int cell) const { return state(cell) == CellState::Resident; }
    [[nodiscard]] int distance(int cell) const;
    [[nodiscard]] std::vector<int> residentCells() const;
    [[nodiscard]] bool settled() const;
    [[nodiscard]] StreamingStats stats() const;
    void resetStats();
    [[nodiscard]] AsyncJobs& jobs() { return m_residency.jobs(); }
    // The unified residency manager (pool, budget, commit queue, byte budget). Attach a TerrainStreamer to it (Engine::enableTerrain does).
    [[nodiscard]] ResidencyManager& residency() { return m_residency; }
    [[nodiscard]] const ResidencyManager& residency() const { return m_residency; }
    [[nodiscard]] std::vector<int> graphDistances(int from) const;

  private:
    struct JobResult {
        std::atomic<bool> done{false};
        std::shared_ptr<void> payload;
    };
    struct Slot {
        CellState state = CellState::Unloaded;
        std::shared_ptr<void> payload;
        std::shared_ptr<JobResult> job;
        bool cancelled = false;
        bool evictNotified = false;
        int lod = 0;
        std::shared_ptr<std::atomic<bool>> cancel;
        size_t bytes = 0;
    };
    struct Asset {
        AssetRequest request;
        std::shared_ptr<JobResult> job;
        bool done = false;
    };

    void ensureSlots();
    void refreshDistances(int cameraCell);
    void startLoad(int cell, Slot& slot, int priority, int lod);
    void updateImpl(int cameraCell, const glm::vec3* position);
    bool primeImpl(int cameraCell, const glm::vec3* position);
    void setCamera(int cameraCell, const glm::vec3* position);
    void refreshSpatial(const glm::vec3& position);
    void rebuildOrder();
    [[nodiscard]] int desiredLod(int cell, int current) const;
    void pollLoading(Slot& slot, bool evict);
    void stepCommitting(int cell, Slot& slot, bool evict, StreamBudget& budget);
    [[nodiscard]] bool shouldEvict(int cell) const;
    [[nodiscard]] ResidentKind kindOf(int cell) const;
    [[nodiscard]] ResidencyDemand demandOf(int cell, bool resident) const;
    void advanceCell(int cell);
    void workCell(int cell, StreamBudget& budget);
    void stepCells(StreamBudget& budget, bool flushNow);
    void trackCell(int cell, Slot& slot);
    void stepAssets(StreamBudget& budget);
    void updatePending();

    PortalSystem& m_portals;
    StreamingConfig m_config;
    CellLoader m_loader;
    ResidencyManager m_residency;
    std::vector<Slot> m_slots;
    std::vector<int> m_dist;
    std::vector<int> m_cellOrder;
    std::vector<float> m_cellScore;
    std::map<std::string, Asset> m_assets;
    mutable std::mutex m_assetMutex;
    std::vector<double> m_frames;
    size_t m_frameHead = 0;
    StreamingStats m_stats;
    std::vector<float> m_spatialDist;
    glm::vec3 m_spatialPos{0.0f};
    bool m_spatialValid = false;
    int m_lastCell = -2;
    size_t m_lastPortals = 0;
    uint32_t m_inFlight = 0;
};

export struct CellEntity {
    std::string key;
    EntityDesc desc;
    std::string tag;
    uint32_t layer = 1;
    bool visible = true;
    size_t bytes = 256;
    bool hasWorldPosition = false;
    glm::dvec3 worldPosition{0.0};
};

export struct CellContent {
    std::vector<CellEntity> entities;
};

export [[nodiscard]] CellLoader makeBatchLoader(World& world, PortalSystem& portals, DeltaStore* deltas, std::function<CellContent(int cell)> build, size_t destroyBatch = 64);

export [[nodiscard]] CellLoader makeBatchLoader(World& world, PortalSystem& portals, DeltaStore* deltas, std::function<CellContent(int cell, int lod)> build, size_t destroyBatch = 64);

// Adds an nx by nz grid of portal cells (no portals) covering world-space [worldMin, worldMin + n * cellSize]. Returns the first cell index; cell (x, z) is first + x * nz + z, named "G<x>_<z>".
export int addGridCells(PortalSystem& portals, const World& world, const glm::dvec3& worldMin, float cellSize, int nx, int nz, float minY = -100.0f, float maxY = 1000.0f);

export struct MeshStreamRequest {
    std::string key;
    int lod = 0;
    int priority = 0;
    size_t bytes = 0;
    std::function<std::shared_ptr<void>(int lod)> load;
    std::function<void(int lod, std::shared_ptr<void> payload)> upload;
    std::function<void(int lod)> evict;
    float distance = -1.0f;  // metres to the camera, for the unified metric (negative: priority is hops)
};

// Residency and eviction budget for GPU mesh uploads. Uploads go through StreamingSystem::requestAsset, so they share the per-frame upload budget.
// Lower lod number = finer. Entries not touched this frame are evicted least recently used first when the resident byte budget is exceeded.
export class MeshResidency {
  public:
    MeshResidency(StreamingSystem& streaming, size_t budgetBytes) : m_streaming(streaming), m_budget(budgetBytes) {}
    ~MeshResidency();
    MeshResidency(const MeshResidency&) = delete;
    MeshResidency& operator=(const MeshResidency&) = delete;

    void setBudget(size_t bytes) { m_budget = bytes; }
    [[nodiscard]] size_t budget() const { return m_budget; }
    bool request(MeshStreamRequest request);
    void touch(const std::string& key);
    void advance();
    void release(const std::string& key);
    [[nodiscard]] int residentLod(const std::string& key) const;
    [[nodiscard]] bool pending(const std::string& key) const { return m_pending.contains(key); }
    [[nodiscard]] size_t residentBytes() const { return m_resident; }
    [[nodiscard]] size_t residentCount() const { return m_entries.size(); }
    [[nodiscard]] uint64_t evictions() const { return m_evictions; }
    [[nodiscard]] uint64_t uploads() const { return m_uploads; }

  private:
    struct Entry {
        int lod = 0;
        size_t bytes = 0;
        uint64_t lastUse = 0;
        std::function<void(int)> evict;
        float base = 0.0f;
    };
    void finish(const MeshStreamRequest& request, std::shared_ptr<void> payload);
    void enforceBudget(const std::string& keep);

    StreamingSystem& m_streaming;
    size_t m_budget;
    size_t m_resident = 0;
    uint64_t m_frame = 1;
    uint64_t m_serial = 0;
    uint64_t m_evictions = 0;
    uint64_t m_uploads = 0;
    std::map<std::string, Entry> m_entries;
    std::map<std::string, int> m_pending;
};
