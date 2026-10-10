module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
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

AsyncJobs::AsyncJobs(size_t workers) {
    for (size_t i = 0; i < std::max<size_t>(workers, 1); ++i) m_threads.emplace_back([this] { loop(); });
}

AsyncJobs::~AsyncJobs() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& t : m_threads) t.join();
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
            auto best = std::min_element(m_queue.begin(), m_queue.end(), [](const Item& a, const Item& b) { return a.priority != b.priority ? a.priority < b.priority : a.order < b.order; });
            item = std::move(*best);
            m_queue.erase(best);
            ++m_running;
        }
        item.fn();
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
    const size_t n = m_portals.cells().size();
    std::vector<int> dist(n, std::numeric_limits<int>::max());
    if (from < 0 || static_cast<size_t>(from) >= n) return dist;
    std::vector<int> queue{from};
    dist[static_cast<size_t>(from)] = 0;
    for (size_t head = 0; head < queue.size(); ++head) {
        const int c = queue[head];
        for (int p : m_portals.portalsOf(c)) {
            const int partner = m_portals.portals()[static_cast<size_t>(p)].partner;
            if (partner < 0) continue;
            const int next = m_portals.portals()[static_cast<size_t>(partner)].cell;
            if (next < 0 || dist[static_cast<size_t>(next)] != std::numeric_limits<int>::max()) continue;
            dist[static_cast<size_t>(next)] = dist[static_cast<size_t>(c)] + 1;
            queue.push_back(next);
        }
    }
    return dist;
}

CellState StreamingSystem::state(int cell) const { return cell >= 0 && static_cast<size_t>(cell) < m_slots.size() ? m_slots[static_cast<size_t>(cell)].state : CellState::Unloaded; }

int StreamingSystem::distance(int cell) const { return cell >= 0 && static_cast<size_t>(cell) < m_dist.size() ? m_dist[static_cast<size_t>(cell)] : std::numeric_limits<int>::max(); }

std::vector<int> StreamingSystem::residentCells() const {
    std::vector<int> out;
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].state == CellState::Resident) out.push_back(static_cast<int>(i));
    }
    return out;
}

bool StreamingSystem::settled() const {
    for (const Slot& s : m_slots) {
        if (s.state == CellState::Loading || s.state == CellState::Ready || s.state == CellState::Committing || s.state == CellState::Unloading) return false;
    }
    std::lock_guard lock(m_assetMutex);
    for (const auto& [key, a] : m_assets) {
        if (!a.done) return false;
    }
    return true;
}

void StreamingSystem::stepCells(StreamBudget& budget) {
    const int keep = m_config.keepHops;
    const int load = m_config.loadHops;
    std::vector<int> order;
    for (size_t i = 0; i < m_slots.size(); ++i) order.push_back(static_cast<int>(i));
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return m_dist[static_cast<size_t>(a)] < m_dist[static_cast<size_t>(b)]; });

    m_inFlight = 0;
    for (const Slot& s : m_slots) m_inFlight += s.state == CellState::Loading ? 1 : 0;
    bool touched = false;
    auto spendAllowed = [&] { return !touched || !budget.exhausted(); };

    for (int cell : order) {
        Slot& s = m_slots[static_cast<size_t>(cell)];
        const int d = m_dist[static_cast<size_t>(cell)];
        const bool wanted = d <= load;
        const bool evict = d > keep;
        switch (s.state) {
        case CellState::Unloaded:
            if (wanted && m_inFlight < m_config.maxInFlight && m_loader.prepare) {
                auto job = std::make_shared<JobResult>();
                s.job = job;
                s.cancelled = false;
                s.state = CellState::Loading;
                ++m_inFlight;
                m_jobs.submit(d, [this, cell, job] {
                    job->payload = m_loader.prepare(cell);
                    job->done.store(true, std::memory_order_release);
                });
            }
            break;
        case CellState::Loading:
            if (evict) s.cancelled = true;
            if (s.job && s.job->done.load(std::memory_order_acquire)) {
                if (s.cancelled) {
                    s.payload.reset();
                    s.job.reset();
                    s.state = CellState::Unloaded;
                    ++m_stats.cellsCancelled;
                } else {
                    s.payload = std::move(s.job->payload);
                    s.job.reset();
                    s.state = CellState::Ready;
                }
            }
            break;
        default:
            break;
        }
        if (s.state == CellState::Ready) {
            if (evict) {
                s.payload.reset();
                s.state = CellState::Unloaded;
                ++m_stats.cellsCancelled;
            } else if (spendAllowed() && m_loader.commit) {
                touched = true;
                s.state = CellState::Committing;
            }
        }
        if (s.state == CellState::Committing) {
            if (evict) {
                s.state = CellState::Unloading;
                s.payload.reset();
            } else if (spendAllowed()) {
                touched = true;
                const size_t before = budget.spent;
                const bool done = !m_loader.commit || m_loader.commit(cell, s.payload.get(), budget);
                m_stats.bytesCommitted += budget.spent - before;
                if (done) {
                    s.payload.reset();
                    s.state = CellState::Resident;
                    ++m_stats.cellsLoaded;
                    if (m_loader.resident) m_loader.resident(cell);
                }
            }
        }
        if (s.state == CellState::Resident && evict) {
            if (m_loader.evicting && !s.evictNotified) m_loader.evicting(cell);
            s.evictNotified = true;
            s.state = CellState::Unloading;
        }
        if (s.state == CellState::Unloading && (spendAllowed() || !m_loader.unload)) {
            touched = true;
            const bool done = !m_loader.unload || m_loader.unload(cell, budget);
            if (done) {
                s.state = CellState::Unloaded;
                s.evictNotified = false;
                ++m_stats.cellsUnloaded;
            }
        }
    }
}

void StreamingSystem::stepAssets(StreamBudget& budget) {
    std::vector<std::string> ready;
    {
        std::lock_guard lock(m_assetMutex);
        for (auto& [key, a] : m_assets) {
            if (!a.done && a.job && a.job->done.load(std::memory_order_acquire)) ready.push_back(key);
        }
    }
    std::stable_sort(ready.begin(), ready.end(), [&](const std::string& x, const std::string& y) { return m_assets[x].request.priority < m_assets[y].request.priority; });
    bool touched = false;
    for (const std::string& key : ready) {
        if (touched && budget.exhausted()) break;
        Asset& a = m_assets[key];
        touched = true;
        if (a.request.finish) a.request.finish(a.job->payload);
        budget.spend(a.request.bytes);
        m_stats.bytesCommitted += a.request.bytes;
        ++m_stats.assetsLoaded;
        std::lock_guard lock(m_assetMutex);
        a.done = true;
        a.job->payload.reset();
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
        if (load) job->payload = load();
        job->done.store(true, std::memory_order_release);
    });
}

bool StreamingSystem::assetReady(const std::string& key) const {
    std::lock_guard lock(m_assetMutex);
    const auto it = m_assets.find(key);
    return it != m_assets.end() && it->second.done;
}

void StreamingSystem::update(int cameraCell) {
    const auto start = std::chrono::steady_clock::now();
    ensureSlots();
    if (cameraCell != m_lastCell || m_portals.portals().size() != m_lastPortals || m_dist.size() != m_slots.size()) {
        m_dist = graphDistances(cameraCell);
        m_lastCell = cameraCell;
        m_lastPortals = m_portals.portals().size();
    }
    StreamBudget budget;
    budget.start = start;
    budget.millis = m_config.commitMillisPerFrame;
    budget.bytes = m_config.uploadBytesPerFrame;
    stepAssets(budget);
    stepCells(budget);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.updateMillis = ms;
    m_stats.updateMaxMillis = std::max(m_stats.updateMaxMillis, ms);
}

void StreamingSystem::prime(int cameraCell) {
    ensureSlots();
    m_dist = graphDistances(cameraCell);
    m_lastCell = cameraCell;
    m_lastPortals = m_portals.portals().size();
    for (int guard = 0; guard < 100000; ++guard) {
        StreamBudget budget;
        budget.unlimited = true;
        stepAssets(budget);
        stepCells(budget);
        if (settled()) return;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void StreamingSystem::recordFrame(double millis) {
    ++m_stats.frames;
    m_stats.frameMax = std::max(m_stats.frameMax, millis);
    if (millis > m_config.hitchMillis) ++m_stats.hitches;
    if (m_frames.size() < m_config.window) {
        m_frames.push_back(millis);
    } else {
        m_frames[m_frameHead] = millis;
        m_frameHead = (m_frameHead + 1) % m_frames.size();
    }
}

StreamingStats StreamingSystem::stats() const {
    StreamingStats out = m_stats;
    if (!m_frames.empty()) {
        std::vector<double> sorted = m_frames;
        std::sort(sorted.begin(), sorted.end());
        out.frameP50 = sorted[sorted.size() / 2];
        out.frameP99 = sorted[std::min(sorted.size() - 1, static_cast<size_t>(static_cast<double>(sorted.size()) * 0.99))];
    }
    for (const Slot& s : m_slots) {
        if (s.state == CellState::Resident) ++out.residentCells;
        if (s.state == CellState::Loading || s.state == CellState::Ready || s.state == CellState::Committing) ++out.pendingCells;
    }
    std::lock_guard lock(m_assetMutex);
    for (const auto& [key, a] : m_assets) out.pendingAssets += a.done ? 0 : 1;
    return out;
}

void StreamingSystem::resetStats() {
    m_stats = {};
    m_frames.clear();
    m_frameHead = 0;
}

CellLoader makeBatchLoader(World& world, PortalSystem& portals, DeltaStore* deltas, std::function<CellContent(int cell)> build, size_t destroyBatch) {
    struct Batch {
        CellContent content;
        size_t next = 0;
    };
    struct Shared {
        std::map<int, std::vector<EntityHandle>> doomed;
        std::map<int, std::vector<EntityHandle>> created;
        std::map<int, bool> collected;
    };
    auto shared = std::make_shared<Shared>();
    CellLoader loader;
    loader.prepare = [build](int cell) -> std::shared_ptr<void> {
        auto batch = std::make_shared<Batch>();
        batch->content = build(cell);
        return batch;
    };
    loader.commit = [&world, &portals, deltas, shared](int cell, void* payload, StreamBudget& budget) {
        auto* batch = static_cast<Batch*>(payload);
        const std::string& name = portals.cells()[static_cast<size_t>(cell)].name;
        while (batch->next < batch->content.entities.size()) {
            const CellEntity& e = batch->content.entities[batch->next++];
            const EntityHandle h = world.create(e.desc);
            if (!e.tag.empty()) world.setTag(h, e.tag);
            world.setLayer(h, e.layer);
            if (!e.visible) world.setVisible(h, false);
            shared->created[cell].push_back(h);
            if (deltas) deltas->registerBase(name, e.key, h);
            budget.spend(e.bytes);
            if (budget.exhausted()) break;
        }
        if (batch->next < batch->content.entities.size()) return false;
        if (deltas) deltas->applyCell(name);
        return true;
    };
    loader.evicting = [&portals, deltas](int cell) {
        if (deltas) deltas->captureCell(portals.cells()[static_cast<size_t>(cell)].name);
    };
    loader.unload = [&world, &portals, deltas, shared, destroyBatch](int cell, StreamBudget& budget) {
        const std::string& name = portals.cells()[static_cast<size_t>(cell)].name;
        if (!shared->collected[cell]) {
            std::vector<EntityHandle>& list = shared->doomed[cell];
            list = shared->created[cell];
            if (deltas) {
                for (EntityHandle h : deltas->liveEntities(name)) {
                    if (std::find(list.begin(), list.end(), h) == list.end()) list.push_back(h);
                }
            }
            shared->created.erase(cell);
            shared->collected[cell] = true;
        }
        std::vector<EntityHandle>& doomed = shared->doomed[cell];
        while (!doomed.empty()) {
            const size_t n = std::min(destroyBatch, doomed.size());
            std::vector<EntityHandle> chunk(doomed.end() - static_cast<std::ptrdiff_t>(n), doomed.end());
            doomed.resize(doomed.size() - n);
            std::vector<EntityHandle> alive;
            for (EntityHandle h : chunk) {
                if (world.isAlive(h)) alive.push_back(h);
            }
            world.destroyBatch(alive);
            budget.spend(n * 64);
            if (budget.exhausted() && !doomed.empty()) return false;
        }
        if (deltas) deltas->releaseCell(name);
        shared->doomed.erase(cell);
        shared->collected.erase(cell);
        return true;
    };
    return loader;
}
