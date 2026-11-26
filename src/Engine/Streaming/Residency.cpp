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
#include <utility>
#include <vector>

module Engine.Streaming;

import Engine.glm;

namespace {
constexpr float kMaxTrackedSpeed = 1000.0f;
constexpr float kVelocitySmoothing = 0.35f;
constexpr double kStaleBudgetMillis = 50.0;
constexpr int kMaxHops = 1000;
constexpr float kSizeTieBreak = 1.0e-3f;
}

const char* residentKindName(ResidentKind kind) {
    switch (kind) {
    case ResidentKind::PortalCell: return "portal_cell";
    case ResidentKind::GridCell: return "grid_cell";
    case ResidentKind::TerrainTile: return "terrain_tile";
    case ResidentKind::GpuMesh: return "gpu_mesh";
    case ResidentKind::ScatterBatch: return "scatter_batch";
    }
    return "unknown";
}

ResidencyManager::ResidencyManager(size_t workers, ResidencyConfig config) : m_config(config), m_jobs(workers) {}

ResidencyManager::~ResidencyManager() { m_jobs.waitIdle(); }

void ResidencyManager::observeCamera(int space, const glm::vec3& position) {
    Tracker& tracker = m_trackers[static_cast<size_t>(std::clamp(space, 0, 1))];
    const auto now = std::chrono::steady_clock::now();
    if (tracker.valid) {
        const float dt = std::chrono::duration<float>(now - tracker.time).count();
        if (dt < 1.0e-3f) return;
        const glm::vec3 instant = (position - tracker.position) / dt;
        if (glm::length(instant) > kMaxTrackedSpeed) tracker.velocity = glm::vec3(0.0f);
        else tracker.velocity = glm::mix(tracker.velocity, instant, kVelocitySmoothing);
    }
    tracker.position = position;
    tracker.time = now;
    tracker.valid = true;
}

void ResidencyManager::setVelocity(int space, const glm::vec3& velocity) { m_trackers[static_cast<size_t>(std::clamp(space, 0, 1))].velocity = velocity; }

glm::vec3 ResidencyManager::velocity(int space) const { return m_trackers[static_cast<size_t>(std::clamp(space, 0, 1))].velocity; }

float ResidencyManager::score(const ResidencyDemand& demand) const {
    const float distance = std::max(demand.distance, 0.0f) + m_config.hopMetres * static_cast<float>(std::clamp(demand.hops, 0, kMaxHops));
    float rate = std::max(m_config.minSpeed, 0.01f);
    const glm::vec3& velocity = m_trackers[static_cast<size_t>(std::clamp(demand.space, 0, 1))].velocity;
    if (!m_config.deterministic && glm::dot(demand.direction, demand.direction) > 0.5f) rate = std::max(rate, glm::dot(velocity, demand.direction));
    float seconds = distance / rate;
    if (!demand.inView) seconds += m_config.outOfViewSeconds;
    seconds += m_config.kindBias[static_cast<size_t>(demand.kind)];
    if (demand.resident) seconds *= m_config.residentBias;
    const float importance = demand.radius / (std::max(demand.distance, 0.0f) + demand.radius + 1.0f);
    return seconds - kSizeTieBreak * importance;
}

int ResidencyManager::poolPriority(float score) {
    const float scaled = std::clamp(score * 100.0f, -1.0e8f, 1.0e8f);
    return static_cast<int>(std::lround(scaled));
}

void ResidencyManager::submit(const ResidencyDemand& demand, std::function<void(bool)> job, std::shared_ptr<std::atomic<bool>> cancel) {
    m_jobs.submit(poolPriority(score(demand)), std::move(job), std::move(cancel));
}

StreamBudget& ResidencyManager::frameBudget(uint64_t frame, double millis, size_t bytes) {
    const auto now = std::chrono::steady_clock::now();
    const bool stale = std::chrono::duration<double, std::milli>(now - m_budget.start).count() > kStaleBudgetMillis;
    if (m_budgetFrame != frame || stale) {
        m_budget = StreamBudget{};
        m_budget.start = now;
        m_budget.millis = millis;
        m_budget.bytes = bytes;
        m_budgetFrame = frame;
        m_frameCommit.fill(0.0);
    }
    return m_budget;
}

void ResidencyManager::offer(const void* owner, ResidentKind kind, float score, std::function<void(StreamBudget&)> run) {
    m_tasks.push_back({owner, kind, score, m_order++, std::move(run)});
}

void ResidencyManager::dropOwner(const void* owner) {
    std::erase_if(m_tasks, [owner](const Task& task) { return task.owner == owner; });
}

size_t ResidencyManager::flush(StreamBudget& budget) {
    std::vector<Task> tasks;
    tasks.swap(m_tasks);
    std::stable_sort(tasks.begin(), tasks.end(), [](const Task& left, const Task& right) { return left.score != right.score ? left.score < right.score : left.order < right.order; });
    size_t ran = 0;
    for (Task& task : tasks) {
        if (ran > 0 && budget.exhausted()) {
            ++m_stats.commitsDeferred;
            continue;
        }
        const auto start = std::chrono::steady_clock::now();
        task.run(budget);
        const double millis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const size_t kind = static_cast<size_t>(task.kind);
        m_frameCommit[kind] += millis;
        ResidencyKindStats& kindStats = m_stats.kinds[kind];
        kindStats.commitMillis = m_frameCommit[kind];
        kindStats.commitMillisMax = std::max(kindStats.commitMillisMax, millis);
        ++m_stats.commitsRun;
        ++ran;
    }
    return ran;
}

bool ResidencyManager::arrive(const void* owner, StreamBudget& budget) {
    m_arrivals[owner] = m_budgetFrame;
    if (!budget.unlimited) {
        for (const auto& [other, frame] : m_arrivals) {
            const bool active = frame == m_budgetFrame - 1 && m_budgetFrame != ~0ull;
            if (other != owner && active) return false;
        }
    }
    flush(budget);
    return true;
}

void ResidencyManager::track(ResidentKind kind, uint64_t id, size_t bytes, float score, bool protectedItem, std::function<bool()> evict) {
    Entry& entry = m_entries[{static_cast<uint8_t>(kind), id}];
    entry.bytes = bytes;
    entry.score = score;
    entry.protectedItem = protectedItem;
    entry.evicting = false;
    entry.evict = std::move(evict);
}

void ResidencyManager::update(ResidentKind kind, uint64_t id, float score, bool protectedItem) {
    const auto found = m_entries.find({static_cast<uint8_t>(kind), id});
    if (found == m_entries.end()) return;
    found->second.score = score;
    found->second.protectedItem = protectedItem;
}

void ResidencyManager::addBytes(ResidentKind kind, uint64_t id, size_t bytes) {
    const auto found = m_entries.find({static_cast<uint8_t>(kind), id});
    if (found != m_entries.end()) found->second.bytes += bytes;
}

void ResidencyManager::untrack(ResidentKind kind, uint64_t id) { m_entries.erase({static_cast<uint8_t>(kind), id}); }

bool ResidencyManager::tracked(ResidentKind kind, uint64_t id) const { return m_entries.contains({static_cast<uint8_t>(kind), id}); }

size_t ResidencyManager::residentBytes() const {
    size_t total = 0;
    for (const auto& [key, entry] : m_entries) total += entry.evicting ? 0 : entry.bytes;
    return total;
}

size_t ResidencyManager::enforceByteBudget() {
    if (m_config.byteBudget == 0) return 0;
    size_t live = residentBytes();
    if (live <= m_config.byteBudget) return 0;
    struct Victim {
        std::pair<uint8_t, uint64_t> key;
        float score;
    };
    std::vector<Victim> victims;
    for (const auto& [key, entry] : m_entries) {
        if (!entry.evict || entry.protectedItem || entry.evicting) continue;
        victims.push_back({key, entry.score});
    }
    std::stable_sort(victims.begin(), victims.end(), [](const Victim& left, const Victim& right) { return left.score > right.score; });
    size_t requested = 0;
    for (const Victim& victim : victims) {
        if (live <= m_config.byteBudget) break;
        auto found = m_entries.find(victim.key);
        if (found == m_entries.end()) continue;
        const size_t bytes = found->second.bytes;
        const std::function<bool()> evict = found->second.evict;
        if (!evict()) continue;
        found = m_entries.find(victim.key);
        if (found != m_entries.end()) found->second.evicting = true;
        live -= std::min(live, bytes);
        ++m_stats.memoryEvictions;
        ++m_stats.kinds[victim.key.first].evictedForMemory;
        ++requested;
    }
    return requested;
}

void ResidencyManager::noteLoaded(ResidentKind kind, size_t bytes) {
    ResidencyKindStats& stats = m_stats.kinds[static_cast<size_t>(kind)];
    ++stats.loaded;
    stats.bytesCommitted += bytes;
}

void ResidencyManager::noteUnloaded(ResidentKind kind) { ++m_stats.kinds[static_cast<size_t>(kind)].unloaded; }

void ResidencyManager::noteCancelled(ResidentKind kind) { ++m_stats.kinds[static_cast<size_t>(kind)].cancelled; }

void ResidencyManager::setPending(ResidentKind kind, uint32_t count) { m_pending[static_cast<size_t>(kind)] = count; }

void ResidencyManager::recordFrame(double millis, double hitchMillis) {
    if (millis > hitchMillis) {
        size_t worst = kResidentKinds;
        double most = 0.0;
        for (size_t kind = 0; kind < kResidentKinds; ++kind) {
            if (m_frameCommit[kind] > most) {
                most = m_frameCommit[kind];
                worst = kind;
            }
        }
        if (worst < kResidentKinds) ++m_stats.kinds[worst].hitches;
    }
    m_frameCommit.fill(0.0);
}

ResidencyStats ResidencyManager::stats() const {
    ResidencyStats out = m_stats;
    for (size_t kind = 0; kind < kResidentKinds; ++kind) {
        out.kinds[kind].pending = m_pending[kind];
        out.kinds[kind].resident = 0;
        out.kinds[kind].residentBytes = 0;
    }
    for (const auto& [key, entry] : m_entries) {
        if (entry.evicting) continue;
        ++out.kinds[key.first].resident;
        out.kinds[key.first].residentBytes += entry.bytes;
        out.residentBytes += entry.bytes;
    }
    out.byteBudget = m_config.byteBudget;
    out.jobsCancelled = m_jobs.cancelledJobs();
    return out;
}

void ResidencyManager::resetStats() {
    m_stats = {};
    m_frameCommit.fill(0.0);
}
