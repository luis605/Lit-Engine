module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

module Engine.Streaming;

import Engine.Delta;
import Engine.Portal;
import Engine.Render.entity;
import Engine.World;

namespace {
constexpr std::chrono::microseconds kPrimeSleep{200};
constexpr double kP99Fraction = 0.99;
}

AsyncJobs::AsyncJobs(size_t workers) {
    const size_t workerCount = std::max<size_t>(workers, 1);
    m_threads.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker) m_threads.emplace_back([this] { loop(); });
}

AsyncJobs::~AsyncJobs() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& thread : m_threads) thread.join();
}

void AsyncJobs::submit(int priority, std::function<void()> job) {
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back({priority, m_order++, std::move(job)});
    }
    m_wake.notify_one();
}

size_t AsyncJobs::pending() const {
    std::lock_guard lock(m_mutex);
    return m_queue.size() + m_running;
}

void AsyncJobs::waitIdle() {
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_queue.empty() && m_running == 0; });
}

void AsyncJobs::loop() {
    for (;;) {
        Item item;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty()) return;
            const auto best = std::min_element(m_queue.begin(), m_queue.end(), [](const Item& left, const Item& right) { return left.priority != right.priority ? left.priority < right.priority : left.order < right.order; });
            item = std::move(*best);
            if (best != m_queue.end() - 1) *best = std::move(m_queue.back());
            m_queue.pop_back();
            ++m_running;
        }
        try {
            item.fn();
        } catch (...) {
        }
        {
            std::lock_guard lock(m_mutex);
            --m_running;
            if (m_queue.empty() && m_running == 0) m_idle.notify_all();
        }
    }
}

StreamingSystem::StreamingSystem(PortalSystem& portals, StreamingConfig config) : m_portals(portals), m_config(config), m_jobs(config.workers) {}

StreamingSystem::~StreamingSystem() { m_jobs.waitIdle(); }

void StreamingSystem::ensureSlots() {
    if (m_slots.size() < m_portals.cells().size()) m_slots.resize(m_portals.cells().size());
}

std::vector<int> StreamingSystem::graphDistances(int from) const {
    constexpr int kUnreached = std::numeric_limits<int>::max();
    const size_t cellCount = m_portals.cells().size();
    std::vector<int> distances(cellCount, kUnreached);
    if (from < 0 || static_cast<size_t>(from) >= cellCount) return distances;
    const std::vector<Portal>& portals = m_portals.portals();
    std::vector<int> queue{from};
    distances[static_cast<size_t>(from)] = 0;
    for (size_t head = 0; head < queue.size(); ++head) {
        const int cell = queue[head];
        for (int portalIndex : m_portals.portalsOf(cell)) {
            const int partner = portals[static_cast<size_t>(portalIndex)].partner;
            if (partner < 0) continue;
            const int next = portals[static_cast<size_t>(partner)].cell;
            if (next < 0 || distances[static_cast<size_t>(next)] != kUnreached) continue;
            distances[static_cast<size_t>(next)] = distances[static_cast<size_t>(cell)] + 1;
            queue.push_back(next);
        }
    }
    return distances;
}

CellState StreamingSystem::state(int cell) const { return cell >= 0 && static_cast<size_t>(cell) < m_slots.size() ? m_slots[static_cast<size_t>(cell)].state : CellState::Unloaded; }

int StreamingSystem::distance(int cell) const { return cell >= 0 && static_cast<size_t>(cell) < m_dist.size() ? m_dist[static_cast<size_t>(cell)] : std::numeric_limits<int>::max(); }

std::vector<int> StreamingSystem::residentCells() const {
    std::vector<int> out;
    for (size_t cell = 0; cell < m_slots.size(); ++cell) {
        if (m_slots[cell].state == CellState::Resident) out.push_back(static_cast<int>(cell));
    }
    return out;
}

bool StreamingSystem::settled() const {
    for (const Slot& slot : m_slots) {
        if (slot.state == CellState::Loading || slot.state == CellState::Ready || slot.state == CellState::Committing || slot.state == CellState::Unloading) return false;
    }
    std::lock_guard lock(m_assetMutex);
    for (const auto& [key, asset] : m_assets) {
        if (!asset.done) return false;
    }
    return true;
}

void StreamingSystem::refreshDistances(int cameraCell) {
    m_dist = graphDistances(cameraCell);
    m_lastCell = cameraCell;
    m_lastPortals = m_portals.portals().size();
    m_cellOrder.resize(m_slots.size());
    std::iota(m_cellOrder.begin(), m_cellOrder.end(), 0);
    std::stable_sort(m_cellOrder.begin(), m_cellOrder.end(), [this](int left, int right) { return m_dist[static_cast<size_t>(left)] < m_dist[static_cast<size_t>(right)]; });
}

void StreamingSystem::startLoad(int cell, Slot& slot, int distance) {
    auto job = std::make_shared<JobResult>();
    slot.job = job;
    slot.cancelled = false;
    slot.state = CellState::Loading;
    ++m_inFlight;
    m_jobs.submit(distance, [this, cell, job] {
        try {
            job->payload = m_loader.prepare(cell);
        } catch (...) {
            job->payload.reset();
        }
        job->done.store(true, std::memory_order_release);
    });
}

void StreamingSystem::pollLoading(Slot& slot, bool evict) {
    if (evict) slot.cancelled = true;
    if (!slot.job || !slot.job->done.load(std::memory_order_acquire)) return;
    if (slot.cancelled) {
        slot.payload.reset();
        slot.job.reset();
        slot.state = CellState::Unloaded;
        ++m_stats.cellsCancelled;
        return;
    }
    slot.payload = std::move(slot.job->payload);
    slot.job.reset();
    slot.state = CellState::Ready;
}

void StreamingSystem::stepCommitting(int cell, Slot& slot, bool evict, StreamBudget& budget, bool& touched) {
    if (evict) {
        slot.state = CellState::Unloading;
        slot.payload.reset();
        return;
    }
    if (touched && budget.exhausted()) return;
    touched = true;
    ++m_stats.updateWork;
    const size_t before = budget.spent;
    const bool done = !m_loader.commit || m_loader.commit(cell, slot.payload.get(), budget);
    m_stats.bytesCommitted += budget.spent - before;
    if (!done) return;
    slot.payload.reset();
    slot.state = CellState::Resident;
    ++m_stats.cellsLoaded;
    if (m_loader.resident) m_loader.resident(cell);
}

void StreamingSystem::stepCell(int cell, StreamBudget& budget, bool& touched) {
    Slot& slot = m_slots[static_cast<size_t>(cell)];
    const int distance = m_dist[static_cast<size_t>(cell)];
    const bool wanted = distance <= m_config.loadHops;
    const bool evict = distance > m_config.keepHops;
    if (slot.state == CellState::Unloaded && wanted && m_inFlight < m_config.maxInFlight && m_loader.prepare) startLoad(cell, slot, distance);
    else if (slot.state == CellState::Loading) pollLoading(slot, evict);
    if (slot.state == CellState::Ready) {
        if (evict) {
            slot.payload.reset();
            slot.state = CellState::Unloaded;
            ++m_stats.cellsCancelled;
        } else if ((!touched || !budget.exhausted()) && m_loader.commit) {
            touched = true;
            slot.state = CellState::Committing;
        }
    }
    if (slot.state == CellState::Committing) stepCommitting(cell, slot, evict, budget, touched);
    if (slot.state == CellState::Resident && evict) {
        if (m_loader.evicting && !slot.evictNotified) m_loader.evicting(cell);
        slot.evictNotified = true;
        slot.state = CellState::Unloading;
    }
    if (slot.state == CellState::Unloading && ((!touched || !budget.exhausted()) || !m_loader.unload)) {
        touched = true;
        ++m_stats.updateWork;
        const bool done = !m_loader.unload || m_loader.unload(cell, budget);
        if (!done) return;
        slot.state = CellState::Unloaded;
        slot.evictNotified = false;
        ++m_stats.cellsUnloaded;
    }
}

void StreamingSystem::stepCells(StreamBudget& budget) {
    m_inFlight = 0;
    for (const Slot& slot : m_slots) m_inFlight += slot.state == CellState::Loading ? 1 : 0;
    bool touched = false;
    for (int cell : m_cellOrder) stepCell(cell, budget, touched);
}

void StreamingSystem::stepAssets(StreamBudget& budget) {
    struct ReadyAsset {
        int priority;
        Asset* asset;
    };
    std::vector<ReadyAsset> ready;
    {
        std::lock_guard lock(m_assetMutex);
        for (auto& [key, asset] : m_assets) {
            if (!asset.done && asset.job && asset.job->done.load(std::memory_order_acquire)) ready.push_back({asset.request.priority, &asset});
        }
    }
    std::stable_sort(ready.begin(), ready.end(), [](const ReadyAsset& left, const ReadyAsset& right) { return left.priority < right.priority; });
    bool touched = false;
    for (const ReadyAsset& entry : ready) {
        if (touched && budget.exhausted()) break;
        Asset& asset = *entry.asset;
        touched = true;
        ++m_stats.updateWork;
        if (asset.request.finish) asset.request.finish(asset.job->payload);
        budget.spend(asset.request.bytes);
        m_stats.bytesCommitted += asset.request.bytes;
        ++m_stats.assetsLoaded;
        std::lock_guard lock(m_assetMutex);
        asset.done = true;
        asset.job->payload.reset();
    }
}

void StreamingSystem::requestAsset(AssetRequest request) {
    std::lock_guard lock(m_assetMutex);
    if (m_assets.contains(request.key)) return;
    Asset asset;
    asset.job = std::make_shared<JobResult>();
    asset.request = std::move(request);
    auto job = asset.job;
    auto load = asset.request.load;
    const int priority = asset.request.priority;
    m_assets.emplace(asset.request.key, std::move(asset));
    m_jobs.submit(priority, [job, load] {
        try {
            if (load) job->payload = load();
        } catch (...) {
            job->payload.reset();
        }
        job->done.store(true, std::memory_order_release);
    });
}

bool StreamingSystem::assetReady(const std::string& key) const {
    std::lock_guard lock(m_assetMutex);
    const auto found = m_assets.find(key);
    return found != m_assets.end() && found->second.done;
}

void StreamingSystem::update(int cameraCell) {
    const auto start = std::chrono::steady_clock::now();
    ensureSlots();
    if (cameraCell != m_lastCell || m_portals.portals().size() != m_lastPortals || m_dist.size() != m_slots.size()) refreshDistances(cameraCell);
    StreamBudget budget;
    budget.start = start;
    budget.millis = m_config.commitMillisPerFrame;
    budget.bytes = m_config.uploadBytesPerFrame;
    m_stats.updateWork = 0;
    stepAssets(budget);
    stepCells(budget);
    m_stats.updateWorkMax = std::max(m_stats.updateWorkMax, m_stats.updateWork);
    const double elapsedMillis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.updateMillis = elapsedMillis;
    m_stats.updateMaxMillis = std::max(m_stats.updateMaxMillis, elapsedMillis);
}

bool StreamingSystem::prime(int cameraCell) {
    ensureSlots();
    refreshDistances(cameraCell);
    const double timeoutMillis = std::isfinite(m_config.primeTimeoutMillis) ? std::max(0.0, m_config.primeTimeoutMillis) : 0.0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::milli>(timeoutMillis));
    while (true) {
        StreamBudget budget;
        budget.unlimited = true;
        stepAssets(budget);
        stepCells(budget);
        if (settled()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(kPrimeSleep);
    }
}

void StreamingSystem::recordFrame(double millis) {
    if (!std::isfinite(millis)) return;
    ++m_stats.frames;
    m_stats.frameMax = std::max(m_stats.frameMax, millis);
    if (millis > m_config.hitchMillis) ++m_stats.hitches;
    if (m_config.window == 0) return;
    if (m_frames.size() < m_config.window) {
        m_frames.push_back(millis);
        return;
    }
    m_frameHead %= m_frames.size();
    m_frames[m_frameHead] = millis;
    m_frameHead = (m_frameHead + 1) % m_frames.size();
}

StreamingStats StreamingSystem::stats() const {
    StreamingStats out = m_stats;
    if (!m_frames.empty()) {
        std::vector<double> sorted = m_frames;
        const size_t medianIndex = sorted.size() / 2;
        const size_t p99Index = std::min(sorted.size() - 1, static_cast<size_t>(static_cast<double>(sorted.size()) * kP99Fraction));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(medianIndex), sorted.end());
        out.frameP50 = sorted[medianIndex];
        if (p99Index > medianIndex) std::nth_element(sorted.begin() + static_cast<std::ptrdiff_t>(medianIndex) + 1, sorted.begin() + static_cast<std::ptrdiff_t>(p99Index), sorted.end());
        out.frameP99 = sorted[p99Index];
    }
    for (const Slot& slot : m_slots) {
        if (slot.state == CellState::Resident) ++out.residentCells;
        if (slot.state == CellState::Loading || slot.state == CellState::Ready || slot.state == CellState::Committing) ++out.pendingCells;
    }
    std::lock_guard lock(m_assetMutex);
    for (const auto& [key, asset] : m_assets) out.pendingAssets += asset.done ? 0 : 1;
    return out;
}

void StreamingSystem::resetStats() {
    m_stats = {};
    m_frames.clear();
    m_frameHead = 0;
}
