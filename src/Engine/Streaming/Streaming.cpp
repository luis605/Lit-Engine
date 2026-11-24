module;

#include <algorithm>
#include <array>
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

import Engine.glm;
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
    submit(priority, [job = std::move(job)](bool) { job(); }, nullptr);
}

void AsyncJobs::submit(int priority, std::function<void(bool)> job, std::shared_ptr<std::atomic<bool>> cancel) {
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back({priority, m_order++, std::move(job), std::move(cancel)});
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
        const bool cancelled = item.cancel && item.cancel->load(std::memory_order_acquire);
        if (cancelled) m_cancelled.fetch_add(1);
        try {
            item.fn(cancelled);
        } catch (...) {
        }
        {
            std::lock_guard lock(m_mutex);
            --m_running;
            if (m_queue.empty() && m_running == 0) m_idle.notify_all();
        }
    }
}

StreamingSystem::StreamingSystem(PortalSystem& portals, StreamingConfig config) : m_portals(portals), m_config(config), m_residency(config.workers, config.residency) {
    m_residency.config().deterministic = config.deterministic || config.residency.deterministic;
}

StreamingSystem::~StreamingSystem() {
    m_residency.dropOwner(this);
    m_residency.jobs().waitIdle();
    for (size_t cell = 0; cell < m_slots.size(); ++cell) {
        m_residency.untrack(ResidentKind::PortalCell, cell);
        m_residency.untrack(ResidentKind::GridCell, cell);
    }
}

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
    rebuildOrder();
}

ResidentKind StreamingSystem::kindOf(int cell) const { return m_portals.portalsOf(cell).empty() ? ResidentKind::GridCell : ResidentKind::PortalCell; }

ResidencyDemand StreamingSystem::demandOf(int cell, bool resident) const {
    constexpr float kUnbounded = 1.0e8f;
    constexpr int kFarHops = 1000;
    ResidencyDemand demand;
    demand.kind = kindOf(cell);
    demand.resident = resident;
    const int hops = m_dist[static_cast<size_t>(cell)];
    demand.hops = hops >= kFarHops ? kFarHops : hops;
    const PortalCell& portalCell = m_portals.cells()[static_cast<size_t>(cell)];
    const bool bounded = std::abs(portalCell.min.x) < kUnbounded && std::abs(portalCell.max.x) < kUnbounded && std::abs(portalCell.min.z) < kUnbounded && std::abs(portalCell.max.z) < kUnbounded;
    if (m_config.spatial.enabled && m_spatialValid && bounded && static_cast<size_t>(cell) < m_spatialDist.size() && std::isfinite(m_spatialDist[static_cast<size_t>(cell)])) {
        // Metric distance subsumes the hop count unless the portal route is the closer reading (taken as the minimum in rebuildOrder).
        demand.distance = m_spatialDist[static_cast<size_t>(cell)];
        const glm::vec3 center = (portalCell.min + portalCell.max) * 0.5f;
        const glm::vec3 to = center - m_spatialPos;
        const float length = glm::length(to);
        if (length > 1.0e-3f) demand.direction = to / length;
        demand.radius = 0.5f * glm::length(glm::vec2(portalCell.max.x - portalCell.min.x, portalCell.max.z - portalCell.min.z));
    }
    return demand;
}

void StreamingSystem::rebuildOrder() {
    m_cellOrder.resize(m_slots.size());
    std::iota(m_cellOrder.begin(), m_cellOrder.end(), 0);
    m_cellScore.assign(m_slots.size(), std::numeric_limits<float>::max());
    if (m_dist.size() != m_slots.size()) return;
    for (size_t cell = 0; cell < m_slots.size(); ++cell) {
        const int index = static_cast<int>(cell);
        const bool resident = m_slots[cell].state != CellState::Unloaded;
        const ResidencyDemand metric = demandOf(index, resident);
        float best = m_residency.score(metric);
        if (metric.distance > 0.0f || metric.direction != glm::vec3(0.0f)) {
            ResidencyDemand viaPortals = metric;
            viaPortals.distance = 0.0f;
            viaPortals.direction = glm::vec3(0.0f);
            best = std::min(best, m_residency.score(viaPortals));
        }
        m_cellScore[cell] = best;
    }
    std::stable_sort(m_cellOrder.begin(), m_cellOrder.end(), [this](int left, int right) { return m_cellScore[static_cast<size_t>(left)] < m_cellScore[static_cast<size_t>(right)]; });
}

void StreamingSystem::refreshSpatial(const glm::vec3& position) {
    constexpr float kUnbounded = 1.0e8f;
    const std::vector<PortalCell>& cells = m_portals.cells();
    m_spatialDist.assign(cells.size(), std::numeric_limits<float>::infinity());
    for (size_t index = 0; index < cells.size(); ++index) {
        const PortalCell& cell = cells[index];
        if (std::abs(cell.min.x) >= kUnbounded || std::abs(cell.max.x) >= kUnbounded || std::abs(cell.min.z) >= kUnbounded || std::abs(cell.max.z) >= kUnbounded) continue;
        const float dx = std::max({cell.min.x - position.x, 0.0f, position.x - cell.max.x});
        const float dz = std::max({cell.min.z - position.z, 0.0f, position.z - cell.max.z});
        m_spatialDist[index] = std::sqrt(dx * dx + dz * dz);
    }
    m_spatialPos = position;
    m_spatialValid = true;
    rebuildOrder();
}

int StreamingSystem::desiredLod(int cell, int current) const {
    const std::vector<float>& radii = m_config.spatial.lodRadii;
    if (!m_config.spatial.enabled || !m_spatialValid || radii.empty()) return 0;
    if (m_dist[static_cast<size_t>(cell)] <= m_config.loadHops) return 0;
    const float distance = static_cast<size_t>(cell) < m_spatialDist.size() ? m_spatialDist[static_cast<size_t>(cell)] : 0.0f;
    if (!std::isfinite(distance)) return 0;
    const int count = static_cast<int>(radii.size());
    const int raw = static_cast<int>(std::count_if(radii.begin(), radii.end(), [distance](float radius) { return radius < distance; }));
    if (current < 0 || current > count) return raw;
    const float low = current > 0 ? radii[static_cast<size_t>(current - 1)] - m_config.spatial.lodHysteresis : -std::numeric_limits<float>::infinity();
    const float high = current < count ? radii[static_cast<size_t>(current)] + m_config.spatial.lodHysteresis : std::numeric_limits<float>::infinity();
    return distance >= low && distance <= high ? current : raw;
}

int StreamingSystem::lod(int cell) const { return cell >= 0 && static_cast<size_t>(cell) < m_slots.size() ? m_slots[static_cast<size_t>(cell)].lod : 0; }

float StreamingSystem::spatialDistance(int cell) const { return cell >= 0 && static_cast<size_t>(cell) < m_spatialDist.size() ? m_spatialDist[static_cast<size_t>(cell)] : std::numeric_limits<float>::infinity(); }

bool StreamingSystem::wanted(int cell) const {
    if (cell < 0 || static_cast<size_t>(cell) >= m_dist.size()) return false;
    if (m_dist[static_cast<size_t>(cell)] <= m_config.loadHops) return true;
    return m_config.spatial.enabled && m_spatialValid && static_cast<size_t>(cell) < m_spatialDist.size() && m_spatialDist[static_cast<size_t>(cell)] <= m_config.spatial.loadRadius;
}

void StreamingSystem::startLoad(int cell, Slot& slot, int priority, int lod) {
    auto job = std::make_shared<JobResult>();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    slot.job = job;
    slot.cancel = cancel;
    slot.lod = lod;
    slot.cancelled = false;
    slot.bytes = 0;
    slot.state = CellState::Loading;
    ++m_inFlight;
    m_residency.jobs().submit(priority, [this, cell, lod, job](bool cancelled) {
        if (!cancelled) {
            try {
                job->payload = m_loader.prepareLod ? m_loader.prepareLod(cell, lod) : m_loader.prepare(cell);
            } catch (...) {
                job->payload.reset();
            }
        }
        job->done.store(true, std::memory_order_release);
    }, cancel);
}

void StreamingSystem::pollLoading(Slot& slot, bool evict) {
    if (evict && !slot.cancelled) {
        slot.cancelled = true;
        if (slot.cancel) slot.cancel->store(true, std::memory_order_release);
    }
    if (!slot.job || !slot.job->done.load(std::memory_order_acquire)) return;
    if (slot.cancelled) {
        slot.payload.reset();
        slot.job.reset();
        slot.state = CellState::Unloaded;
        ++m_stats.cellsCancelled;
        m_residency.noteCancelled(ResidentKind::PortalCell);
        return;
    }
    slot.payload = std::move(slot.job->payload);
    slot.job.reset();
    slot.state = CellState::Ready;
}

void StreamingSystem::stepCommitting(int cell, Slot& slot, bool evict, StreamBudget& budget) {
    if (evict) {
        slot.state = CellState::Unloading;
        slot.payload.reset();
        return;
    }
    ++m_stats.updateWork;
    const size_t before = budget.spent;
    const bool done = !m_loader.commit || m_loader.commit(cell, slot.payload.get(), budget);
    const size_t spent = budget.spent - before;
    m_stats.bytesCommitted += spent;
    slot.bytes += spent;
    if (!done) return;
    slot.payload.reset();
    slot.state = CellState::Resident;
    ++m_stats.cellsLoaded;
    m_residency.noteLoaded(kindOf(cell), slot.bytes);
    trackCell(cell, slot);
    if (m_loader.resident) m_loader.resident(cell);
}

void StreamingSystem::trackCell(int cell, Slot& slot) {
    const float score = static_cast<size_t>(cell) < m_cellScore.size() ? m_cellScore[static_cast<size_t>(cell)] : 0.0f;
    m_residency.track(kindOf(cell), static_cast<uint64_t>(cell), slot.bytes, score, wanted(cell), [this, cell]() -> bool {
        if (cell < 0 || static_cast<size_t>(cell) >= m_slots.size()) return false;
        Slot& victim = m_slots[static_cast<size_t>(cell)];
        if (victim.state != CellState::Resident) return false;
        if (m_loader.evicting && !victim.evictNotified) m_loader.evicting(cell);
        victim.evictNotified = true;
        victim.state = CellState::Unloading;
        return true;
    });
}

bool StreamingSystem::shouldEvict(int cell) const {
    bool evict = m_dist[static_cast<size_t>(cell)] > m_config.keepHops;
    if (m_config.spatial.enabled && m_spatialValid && static_cast<size_t>(cell) < m_spatialDist.size()) evict = evict && m_spatialDist[static_cast<size_t>(cell)] > m_config.spatial.keepRadius;
    return evict;
}

// State transitions that need no budget: start and poll worker jobs, cancel, evict decisions. Budgeted steps are offered to the shared queue.
void StreamingSystem::advanceCell(int cell) {
    Slot& slot = m_slots[static_cast<size_t>(cell)];
    const bool wantsLoad = wanted(cell);
    const bool evict = shouldEvict(cell);
    if (slot.state == CellState::Resident && !evict && wantsLoad && m_config.spatial.reloadOnLodChange && (m_loader.prepareLod != nullptr) && desiredLod(cell, slot.lod) != slot.lod) {
        if (m_loader.evicting && !slot.evictNotified) m_loader.evicting(cell);
        slot.evictNotified = true;
        slot.state = CellState::Unloading;
        ++m_stats.lodReloads;
    }
    if (slot.state == CellState::Unloaded && wantsLoad && m_inFlight < m_config.maxInFlight && (m_loader.prepare || m_loader.prepareLod)) {
        startLoad(cell, slot, ResidencyManager::poolPriority(m_cellScore[static_cast<size_t>(cell)]), desiredLod(cell, -1));
    } else if (slot.state == CellState::Loading) {
        pollLoading(slot, evict);
    }
    if (slot.state == CellState::Resident && evict) {
        if (m_loader.evicting && !slot.evictNotified) m_loader.evicting(cell);
        slot.evictNotified = true;
        slot.state = CellState::Unloading;
    }
    if (slot.state == CellState::Resident) m_residency.update(kindOf(cell), static_cast<uint64_t>(cell), m_cellScore[static_cast<size_t>(cell)], wantsLoad);
    if (slot.state == CellState::Unloading) m_residency.untrack(kindOf(cell), static_cast<uint64_t>(cell));
    if (slot.state == CellState::Ready || slot.state == CellState::Committing || slot.state == CellState::Unloading) {
        m_residency.offer(this, kindOf(cell), m_cellScore[static_cast<size_t>(cell)], [this, cell](StreamBudget& budget) { workCell(cell, budget); });
    }
}

// Budgeted work of one cell (runs from the commit queue): commit steps and unload steps.
void StreamingSystem::workCell(int cell, StreamBudget& budget) {
    Slot& slot = m_slots[static_cast<size_t>(cell)];
    const bool evict = shouldEvict(cell);
    if (slot.state == CellState::Ready) {
        if (evict) {
            slot.payload.reset();
            slot.state = CellState::Unloaded;
            ++m_stats.cellsCancelled;
            m_residency.noteCancelled(kindOf(cell));
            return;
        }
        slot.state = CellState::Committing;
    }
    if (slot.state == CellState::Committing) {
        stepCommitting(cell, slot, evict, budget);
        return;
    }
    if (slot.state == CellState::Unloading) {
        ++m_stats.updateWork;
        const bool done = !m_loader.unload || m_loader.unload(cell, budget);
        if (!done) return;
        slot.state = CellState::Unloaded;
        slot.evictNotified = false;
        slot.bytes = 0;
        ++m_stats.cellsUnloaded;
        m_residency.noteUnloaded(kindOf(cell));
    }
}

void StreamingSystem::stepCells(StreamBudget& budget, bool flushNow) {
    m_inFlight = 0;
    for (const Slot& slot : m_slots) m_inFlight += slot.state == CellState::Loading ? 1 : 0;
    for (int cell : m_cellOrder) advanceCell(cell);
    if (flushNow) m_residency.arrive(this, budget);
}

void StreamingSystem::stepAssets(StreamBudget&) {
    std::lock_guard lock(m_assetMutex);
    for (auto& [key, asset] : m_assets) {
        if (asset.done || !asset.job || !asset.job->done.load(std::memory_order_acquire)) continue;
        ResidencyDemand demand;
        demand.kind = asset.request.kind;
        demand.distance = std::max(asset.request.distance, 0.0f);
        demand.hops = asset.request.distance < 0.0f ? std::max(asset.request.priority, 0) : 0;
        Asset* entry = &asset;
        m_residency.offer(this, asset.request.kind, m_residency.score(demand), [this, entry](StreamBudget& budget) {
            {
                std::lock_guard guard(m_assetMutex);
                if (entry->done) return;
            }
            ++m_stats.updateWork;
            if (entry->request.finish) entry->request.finish(entry->job->payload);
            budget.spend(entry->request.bytes);
            m_stats.bytesCommitted += entry->request.bytes;
            ++m_stats.assetsLoaded;
            m_residency.noteLoaded(entry->request.kind, entry->request.bytes);
            std::lock_guard guard(m_assetMutex);
            entry->done = true;
            entry->job->payload.reset();
        });
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
    ResidencyDemand demand;
    demand.kind = asset.request.kind;
    demand.distance = std::max(asset.request.distance, 0.0f);
    demand.hops = asset.request.distance < 0.0f ? std::max(asset.request.priority, 0) : 0;
    const int priority = ResidencyManager::poolPriority(m_residency.score(demand));
    m_assets.emplace(asset.request.key, std::move(asset));
    m_residency.jobs().submit(priority, [job, load] {
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

void StreamingSystem::setCamera(int cameraCell, const glm::vec3* position) {
    const bool cellChanged = cameraCell != m_lastCell || m_portals.portals().size() != m_lastPortals || m_dist.size() != m_slots.size();
    if (cellChanged) refreshDistances(cameraCell);
    if (!m_config.spatial.enabled || !position) return;
    const bool moved = !m_spatialValid || glm::distance(*position, m_spatialPos) >= m_config.spatial.moveThreshold || m_spatialDist.size() != m_slots.size();
    if (moved) refreshSpatial(*position);
}

StreamBudget& StreamingSystem::sharedBudget() { return m_residency.frameBudget(m_stats.frames, m_config.commitMillisPerFrame, m_config.uploadBytesPerFrame); }

void StreamingSystem::update(int cameraCell) { updateImpl(cameraCell, nullptr); }

void StreamingSystem::update(int cameraCell, const glm::vec3& cameraPosition) { updateImpl(cameraCell, &cameraPosition); }

void StreamingSystem::updateImpl(int cameraCell, const glm::vec3* position) {
    const auto start = std::chrono::steady_clock::now();
    ensureSlots();
    ResidencyConfig& residency = m_residency.config();
    const bool deterministic = m_config.deterministic || m_config.residency.deterministic;
    residency = m_config.residency;
    residency.deterministic = deterministic;
    if (position) m_residency.observeCamera(0, *position);
    setCamera(cameraCell, position);
    m_stats.updateWork = 0;
    if (deterministic) {
        // Timing independent: wait for workers, commit everything, repeat until nothing is pending.
        StreamBudget budget;
        budget.unlimited = true;
        m_residency.dropOwner(this);
        for (int pass = 0; pass < 100000; ++pass) {
            m_residency.enforceByteBudget();
            m_residency.dropOwner(this);
            stepAssets(budget);
            stepCells(budget, true);
            if (settled()) break;
            m_residency.jobs().waitIdle();
        }
    } else {
        StreamBudget& budget = sharedBudget();
        m_residency.dropOwner(this);
        m_residency.enforceByteBudget();
        stepAssets(budget);
        stepCells(budget, true);
    }
    updatePending();
    m_stats.updateWorkMax = std::max(m_stats.updateWorkMax, m_stats.updateWork);
    const double elapsedMillis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.updateMillis = elapsedMillis;
    m_stats.updateMaxMillis = std::max(m_stats.updateMaxMillis, elapsedMillis);
}

void StreamingSystem::updatePending() {
    std::array<uint32_t, kResidentKinds> pending{};
    for (size_t cell = 0; cell < m_slots.size(); ++cell) {
        const CellState state = m_slots[cell].state;
        if (state == CellState::Loading || state == CellState::Ready || state == CellState::Committing) ++pending[static_cast<size_t>(kindOf(static_cast<int>(cell)))];
    }
    {
        std::lock_guard lock(m_assetMutex);
        for (const auto& [key, asset] : m_assets) pending[static_cast<size_t>(asset.request.kind)] += asset.done ? 0 : 1;
    }
    for (ResidentKind kind : {ResidentKind::PortalCell, ResidentKind::GridCell, ResidentKind::GpuMesh}) m_residency.setPending(kind, pending[static_cast<size_t>(kind)]);
}

bool StreamingSystem::prime(int cameraCell) { return primeImpl(cameraCell, nullptr); }

bool StreamingSystem::prime(int cameraCell, const glm::vec3& cameraPosition) { return primeImpl(cameraCell, &cameraPosition); }

bool StreamingSystem::primeImpl(int cameraCell, const glm::vec3* position) {
    ensureSlots();
    refreshDistances(cameraCell);
    if (position && m_config.spatial.enabled) refreshSpatial(*position);
    const double timeoutMillis = std::isfinite(m_config.primeTimeoutMillis) ? std::max(0.0, m_config.primeTimeoutMillis) : 0.0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::milli>(timeoutMillis));
    while (true) {
        StreamBudget budget;
        budget.unlimited = true;
        m_residency.dropOwner(this);
        m_residency.enforceByteBudget();
        stepAssets(budget);
        stepCells(budget, true);
        if (settled()) {
            updatePending();
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(kPrimeSleep);
    }
}

void StreamingSystem::recordFrame(double millis) {
    if (!std::isfinite(millis)) return;
    ++m_stats.frames;
    m_residency.recordFrame(millis, m_config.hitchMillis);
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
    {
        std::lock_guard lock(m_assetMutex);
        for (const auto& [key, asset] : m_assets) out.pendingAssets += asset.done ? 0 : 1;
    }
    const ResidencyStats unified = m_residency.stats();
    out.kinds = unified.kinds;
    out.residentBytes = unified.residentBytes;
    out.memoryEvictions = unified.memoryEvictions;
    out.jobsCancelled = unified.jobsCancelled;
    return out;
}

void StreamingSystem::resetStats() {
    m_stats = {};
    m_residency.resetStats();
    m_frames.clear();
    m_frameHead = 0;
}

int addGridCells(PortalSystem& portals, const World& world, const glm::dvec3& worldMin, float cellSize, int nx, int nz, float minY, float maxY) {
    int first = -1;
    for (int x = 0; x < nx; ++x) {
        for (int z = 0; z < nz; ++z) {
            PortalCell cell;
            cell.name = "G" + std::to_string(x) + "_" + std::to_string(z);
            const glm::dvec3 low = worldMin + glm::dvec3(static_cast<double>(x) * cellSize, 0.0, static_cast<double>(z) * cellSize);
            cell.min = glm::vec3(world.toLocal(low).x, minY, world.toLocal(low).z);
            cell.max = glm::vec3(world.toLocal(low + glm::dvec3(cellSize, 0.0, cellSize)).x, maxY, world.toLocal(low + glm::dvec3(cellSize, 0.0, cellSize)).z);
            const int index = portals.addCell(cell);
            if (first < 0) first = index;
        }
    }
    return first;
}

bool MeshResidency::request(MeshStreamRequest request) {
    if (const auto found = m_entries.find(request.key); found != m_entries.end()) {
        found->second.lastUse = m_frame;
        if (found->second.lod <= request.lod) return false;
    }
    if (const auto found = m_pending.find(request.key); found != m_pending.end() && found->second <= request.lod) return false;
    m_pending[request.key] = request.lod;
    AssetRequest asset;
    asset.key = "mesh:" + request.key + ":" + std::to_string(request.lod) + ":" + std::to_string(m_serial++);
    asset.priority = request.priority;
    asset.bytes = request.bytes;
    asset.distance = request.distance;
    asset.kind = ResidentKind::GpuMesh;
    asset.load = [load = request.load, lod = request.lod] { return load ? load(lod) : std::shared_ptr<void>(); };
    asset.finish = [this, request](std::shared_ptr<void> payload) { finish(request, std::move(payload)); };
    m_streaming.requestAsset(std::move(asset));
    return true;
}

void MeshResidency::finish(const MeshStreamRequest& request, std::shared_ptr<void> payload) {
    if (const auto pending = m_pending.find(request.key); pending != m_pending.end() && pending->second == request.lod) m_pending.erase(pending);
    const auto existing = m_entries.find(request.key);
    if (existing != m_entries.end() && existing->second.lod <= request.lod) return;
    if (request.upload) request.upload(request.lod, std::move(payload));
    ++m_uploads;
    if (existing != m_entries.end()) {
        if (existing->second.evict) existing->second.evict(existing->second.lod);
        m_resident -= existing->second.bytes;
        ++m_evictions;
    }
    ResidencyDemand demand;
    demand.kind = ResidentKind::GpuMesh;
    demand.distance = std::max(request.distance, 0.0f);
    demand.hops = request.distance < 0.0f ? std::max(request.priority, 0) : 0;
    demand.resident = true;
    const float base = m_streaming.residency().score(demand);
    m_entries[request.key] = Entry{request.lod, request.bytes, m_frame, request.evict, base};
    m_resident += request.bytes;
    // Mirrored into the unified registry: protected while touched this frame, otherwise evictable in metric order with every other kind.
    m_streaming.residency().track(ResidentKind::GpuMesh, std::hash<std::string>{}(request.key), request.bytes, base, true, [this, key = request.key]() -> bool {
        if (!m_entries.contains(key)) return false;
        release(key);
        return true;
    });
    enforceBudget(request.key);
}

void MeshResidency::enforceBudget(const std::string& keep) {
    while (m_resident > m_budget) {
        auto victim = m_entries.end();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->first == keep || it->second.lastUse >= m_frame) continue;
            if (victim == m_entries.end() || it->second.lastUse < victim->second.lastUse) victim = it;
        }
        if (victim == m_entries.end()) return;
        if (victim->second.evict) victim->second.evict(victim->second.lod);
        m_resident -= victim->second.bytes;
        ++m_evictions;
        m_streaming.residency().untrack(ResidentKind::GpuMesh, std::hash<std::string>{}(victim->first));
        m_entries.erase(victim);
    }
}

void MeshResidency::touch(const std::string& key) {
    if (const auto found = m_entries.find(key); found != m_entries.end()) {
        found->second.lastUse = m_frame;
        m_streaming.residency().update(ResidentKind::GpuMesh, std::hash<std::string>{}(key), found->second.base, true);
    }
}

void MeshResidency::advance() {
    enforceBudget({});
    ++m_frame;
    // Entries not touched since the last frame become evictable; older = less needed (score grows with age).
    constexpr float kAgeSeconds = 0.05f;
    for (const auto& [key, entry] : m_entries) {
        m_streaming.residency().update(ResidentKind::GpuMesh, std::hash<std::string>{}(key), entry.base + kAgeSeconds * static_cast<float>(m_frame - entry.lastUse), entry.lastUse >= m_frame);
    }
}

MeshResidency::~MeshResidency() {
    for (const auto& [key, entry] : m_entries) m_streaming.residency().untrack(ResidentKind::GpuMesh, std::hash<std::string>{}(key));
}

void MeshResidency::release(const std::string& key) {
    const auto found = m_entries.find(key);
    if (found == m_entries.end()) return;
    if (found->second.evict) found->second.evict(found->second.lod);
    m_resident -= found->second.bytes;
    ++m_evictions;
    m_streaming.residency().untrack(ResidentKind::GpuMesh, std::hash<std::string>{}(key));
    m_entries.erase(found);
}

int MeshResidency::residentLod(const std::string& key) const {
    const auto found = m_entries.find(key);
    return found == m_entries.end() ? -1 : found->second.lod;
}
