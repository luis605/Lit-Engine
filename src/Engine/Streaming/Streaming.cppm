module;

#include <atomic>
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
    [[nodiscard]] size_t pending() const;
    void waitIdle();

  private:
    struct Item {
        int priority = 0;
        uint64_t order = 0;
        std::function<void()> fn;
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

export struct StreamingConfig {
    int loadHops = 1;
    int keepHops = 2;
    uint32_t workers = 2;
    uint32_t maxInFlight = 4;
    double commitMillisPerFrame = 1.0;
    size_t uploadBytesPerFrame = 4u << 20;
    double hitchMillis = 33.0;
    uint32_t window = 600;
};

export struct CellLoader {
    std::function<std::shared_ptr<void>(int cell)> prepare;
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
    uint64_t bytesCommitted = 0;
    uint64_t cellsLoaded = 0;
    uint64_t cellsUnloaded = 0;
    uint64_t cellsCancelled = 0;
    uint64_t assetsLoaded = 0;
    uint32_t pendingCells = 0;
    uint32_t pendingAssets = 0;
    uint32_t residentCells = 0;
};

export class StreamingSystem {
  public:
    StreamingSystem(PortalSystem& portals, StreamingConfig config = {});
    ~StreamingSystem();

    void setLoader(CellLoader loader) { m_loader = std::move(loader); }
    [[nodiscard]] StreamingConfig& config() { return m_config; }
    [[nodiscard]] const StreamingConfig& config() const { return m_config; }

    void update(int cameraCell);
    void prime(int cameraCell);
    void recordFrame(double millis);
    void requestAsset(AssetRequest request);
    [[nodiscard]] bool assetReady(const std::string& key) const;

    [[nodiscard]] CellState state(int cell) const;
    [[nodiscard]] bool resident(int cell) const { return state(cell) == CellState::Resident; }
    [[nodiscard]] int distance(int cell) const;
    [[nodiscard]] std::vector<int> residentCells() const;
    [[nodiscard]] bool settled() const;
    [[nodiscard]] StreamingStats stats() const;
    void resetStats();
    [[nodiscard]] AsyncJobs& jobs() { return m_jobs; }
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
    };
    struct Asset {
        AssetRequest request;
        std::shared_ptr<JobResult> job;
        bool done = false;
    };

    void ensureSlots();
    void stepCells(StreamBudget& budget);
    void stepAssets(StreamBudget& budget);

    PortalSystem& m_portals;
    StreamingConfig m_config;
    CellLoader m_loader;
    AsyncJobs m_jobs;
    std::vector<Slot> m_slots;
    std::vector<int> m_dist;
    std::map<std::string, Asset> m_assets;
    mutable std::mutex m_assetMutex;
    std::vector<double> m_frames;
    size_t m_frameHead = 0;
    StreamingStats m_stats;
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
};

export struct CellContent {
    std::vector<CellEntity> entities;
};

export [[nodiscard]] CellLoader makeBatchLoader(World& world, PortalSystem& portals, DeltaStore* deltas, std::function<CellContent(int cell)> build, size_t destroyBatch = 64);
