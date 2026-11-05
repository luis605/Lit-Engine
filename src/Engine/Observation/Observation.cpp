module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "Engine/Log/Log.hpp"

module Engine.Observation;

import Engine.glm;

namespace {
constexpr float MIN_DISTANCE = 1.0e-6f;
constexpr float OUT_OF_VIEW_ANGLE = 180.0f;
constexpr float WORST_ANGLE = 1.0e9f;
constexpr double TRANSITION_EPSILON = 1.0e-9;
constexpr float OCCLUDER_SKIP_DISTANCE = 1.0e-3f;
constexpr int MAX_OCCLUDER_HOPS = 8;
constexpr int MAX_STATE_VALUE = 2;

bool sortByHandleIndex(EntityHandle lhs, EntityHandle rhs) { return lhs.index < rhs.index; }

float pixelCenterToNdc(uint32_t pixel, uint32_t extent) { return ((static_cast<float>(pixel) + 0.5f) / static_cast<float>(extent)) * 2.0f - 1.0f; }

uint32_t clampedIndex(double value) {
    if (!(value > 0.0)) return 0;
    return value >= static_cast<double>(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(value);
}

double finiteOr(double value, double fallback) { return std::isfinite(value) ? value : fallback; }

glm::vec3 rightOf(const ObservationView& view) {
    const glm::vec3 side = glm::cross(view.forward, view.up);
    if (glm::dot(side, side) > MIN_DISTANCE) return glm::normalize(side);
    const glm::vec3 fallback = glm::cross(view.forward, std::abs(view.forward.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::normalize(fallback);
}

ObservationState clampedState(const Json* field) {
    const int value = field ? static_cast<int>(std::clamp(finiteOr(field->asNumber(), 0.0), 0.0, static_cast<double>(MAX_STATE_VALUE))) : 0;
    return static_cast<ObservationState>(std::clamp(value, 0, MAX_STATE_VALUE));
}
}

ObservationView observationViewFromCamera(const Camera& camera, float aspect) {
    const glm::mat4 view = camera.getViewMatrix();
    ObservationView result;
    result.origin = camera.getPosition();
    result.forward = glm::normalize(-glm::vec3(view[0][2], view[1][2], view[2][2]));
    result.up = glm::normalize(glm::vec3(view[0][1], view[1][1], view[2][1]));
    result.fovY = camera.getFov();
    result.aspect = aspect;
    return result;
}

void ObservationSystem::track(EntityHandle entity) {
    if (!m_world.isAlive(entity)) return;
    Tracked& tracked = m_entities[entity.index];
    if (tracked.handle == entity) return;
    tracked = Tracked{};
    tracked.handle = entity;
}

void ObservationSystem::untrack(EntityHandle entity) {
    const auto found = m_entities.find(entity.index);
    if (found != m_entities.end() && found->second.handle == entity) m_entities.erase(found);
}

ObservationState ObservationSystem::state(EntityHandle entity) const {
    const auto found = m_entities.find(entity.index);
    return found != m_entities.end() && found->second.handle == entity ? found->second.state : ObservationState::Unobserved;
}

std::vector<EntityHandle> ObservationSystem::inState(ObservationState wanted) const {
    std::vector<EntityHandle> entities;
    for (const auto& [index, tracked] : m_entities) {
        if (tracked.state == wanted) entities.push_back(tracked.handle);
    }
    std::sort(entities.begin(), entities.end(), sortByHandleIndex);
    return entities;
}

void ObservationSystem::submitGpuIds(uint64_t frameIssued, double issuedTime, uint32_t width, uint32_t height, std::span<const uint32_t> ids) {
    if (width == 0 || height == 0 || ids.size() != static_cast<size_t>(width) * height) return;
    const double latency = m_time - issuedTime;
    const uint64_t frames = m_frame >= frameIssued ? m_frame - frameIssued : 0;
    m_stats.lastLatencySeconds = latency;
    m_stats.lastLatencyFrames = static_cast<uint32_t>(frames);
    m_stats.maxLatencySeconds = std::max(m_stats.maxLatencySeconds, latency);
    m_stats.maxLatencyFrames = std::max(m_stats.maxLatencyFrames, static_cast<uint32_t>(frames));
    if (latency > m_config.maxLatencySeconds || frames > m_config.maxLatencyFrames) {
        ++m_stats.gpuRejected;
        if (m_config.logLatency) Lit::Log::Warn("observation: rejected readback, latency {:.1f} ms, {} frames", latency * 1000.0, frames);
        return;
    }
    if (m_haveSample && frameIssued < m_sample.frame) return;
    ++m_stats.gpuSamples;
    m_latencySum += latency;
    m_stats.meanLatencySeconds = m_latencySum / static_cast<double>(m_stats.gpuSamples);
    m_sample.frame = frameIssued;
    m_sample.time = issuedTime;
    m_sample.width = width;
    m_sample.height = height;
    m_sample.ids.assign(ids.begin(), ids.end());
    m_haveSample = true;
}

void ObservationSystem::rawGpu(std::unordered_map<uint32_t, ObservationState>& out) {
    const float tanY = std::tan(glm::radians(m_view.fovY) * 0.5f);
    const float tanX = tanY * m_view.aspect;
    const float coneTan = std::tan(glm::radians(m_config.focusConeDegrees));
    uint32_t cachedId = 0;
    const Tracked* cachedTracked = nullptr;
    for (uint32_t row = 0; row < m_sample.height; ++row) {
        const float ndcY = pixelCenterToNdc(row, m_sample.height);
        for (uint32_t column = 0; column < m_sample.width; ++column) {
            const uint32_t id = m_sample.ids[static_cast<size_t>(row) * m_sample.width + column];
            if (id == 0) continue;
            if (id != cachedId) {
                const auto found = m_entities.find(id - 1);
                cachedId = id;
                cachedTracked = found != m_entities.end() && m_world.isAlive(found->second.handle) ? &found->second : nullptr;
            }
            if (!cachedTracked) continue;
            const float ndcX = pixelCenterToNdc(column, m_sample.width);
            const float radial = std::sqrt(ndcX * ndcX * tanX * tanX + ndcY * ndcY * tanY * tanY);
            ObservationState& pixelState = out[id - 1];
            pixelState = std::max(pixelState, ObservationState::Seen);
            const float cone = cachedTracked->state == ObservationState::Attended ? coneTan * m_config.focusExitScale : coneTan;
            if (radial <= cone) pixelState = ObservationState::Attended;
        }
    }
}

bool ObservationSystem::occludedByOther(const Tracked& tracked, const glm::vec3& direction, float distance, float radius) {
    float travelled = 0.0f;
    for (int hop = 0; hop < MAX_OCCLUDER_HOPS; ++hop) {
        const auto hit = m_world.raycast(m_view.origin + direction * travelled, direction, distance + radius - travelled);
        if (!hit) return false;
        const float hitAt = travelled + hit->distance;
        if (hit->entity == tracked.handle || hitAt >= distance - radius) return false;
        if (m_ignore && (hit->entity == m_ignore || m_world.isDescendantOf(hit->entity, m_ignore))) {
            travelled = hitAt + OCCLUDER_SKIP_DISTANCE;
            continue;
        }
        return true;
    }
    return false;
}

ObservationState ObservationSystem::rawCpu(const Tracked& tracked, float coneDegrees, float& angleOut) {
    const glm::vec4 bounds = m_world.getWorldBounds(tracked.handle);
    const glm::vec3 center = bounds.w > 0.0f ? glm::vec3(bounds) : m_world.getWorldPosition(tracked.handle);
    const float radius = std::max(bounds.w, 0.0f);
    const glm::vec3 toCenter = center - m_view.origin;
    const float distance = glm::length(toCenter);
    angleOut = OUT_OF_VIEW_ANGLE;
    if (!std::isfinite(distance)) return ObservationState::Unobserved;
    if (distance > m_config.maxDistance + radius) return ObservationState::Unobserved;
    if (distance < MIN_DISTANCE) return ObservationState::Attended;

    const glm::vec3 direction = toCenter / distance;
    const glm::vec3 right = rightOf(m_view);
    const glm::vec3 up = glm::cross(right, m_view.forward);
    const float depth = glm::dot(toCenter, m_view.forward);
    const float angularRadius = radius > 0.0f ? std::asin(std::min(1.0f, radius / distance)) : 0.0f;
    angleOut = std::acos(std::clamp(glm::dot(direction, m_view.forward), -1.0f, 1.0f));

    const float tanY = std::tan(glm::radians(m_view.fovY) * 0.5f);
    const float tanX = tanY * m_view.aspect;
    if (depth + radius <= 0.0f) return ObservationState::Unobserved;
    const float offsetX = glm::dot(toCenter, right);
    const float offsetY = glm::dot(toCenter, up);
    if (std::abs(offsetX) > depth * tanX + radius * std::sqrt(1.0f + tanX * tanX)) return ObservationState::Unobserved;
    if (std::abs(offsetY) > depth * tanY + radius * std::sqrt(1.0f + tanY * tanY)) return ObservationState::Unobserved;
    if (occludedByOther(tracked, direction, distance, radius)) return ObservationState::Unobserved;

    const float cone = glm::radians(coneDegrees) + angularRadius;
    return angleOut <= cone ? ObservationState::Attended : ObservationState::Seen;
}

void ObservationSystem::apply(Tracked& tracked, ObservationState raw) {
    if (raw == tracked.state) {
        tracked.candidate = tracked.state;
        return;
    }
    const bool rising = raw > tracked.state;
    const bool continuing = rising ? tracked.candidate > tracked.state : tracked.candidate < tracked.state;
    if (!continuing) tracked.candidateSince = m_time;
    tracked.candidate = raw;
    const double elapsed = m_time - tracked.candidateSince + TRANSITION_EPSILON;
    ObservationState next = tracked.state;
    if (rising) {
        if (elapsed >= m_config.seenEnter) next = ObservationState::Seen;
        if (raw == ObservationState::Attended && elapsed >= m_config.attendEnter) next = ObservationState::Attended;
        next = std::min(next, raw);
        next = std::max(next, tracked.state);
    } else if (elapsed >= (tracked.state == ObservationState::Attended ? m_config.attendExit : m_config.seenExit)) {
        next = raw;
    }
    if (next == tracked.state) return;
    const ObservationState from = tracked.state;
    tracked.state = next;
    if (next == raw) tracked.candidate = raw;
    m_world.events().emitNow(ObservationChanged{tracked.handle, from, next, m_time});
}

void ObservationSystem::removeDeadEntities() {
    std::vector<ObservationChanged> departures;
    for (auto found = m_entities.begin(); found != m_entities.end();) {
        const Tracked& tracked = found->second;
        if (m_world.isAlive(tracked.handle)) {
            ++found;
            continue;
        }
        if (tracked.state != ObservationState::Unobserved) departures.push_back({tracked.handle, tracked.state, ObservationState::Unobserved, m_time});
        found = m_entities.erase(found);
    }
    for (const ObservationChanged& departure : departures) m_world.events().emitNow(departure);
}

ObservationState ObservationSystem::rawFromGpu(const Tracked& tracked, const std::unordered_map<uint32_t, ObservationState>& gpu, float& angleOut) const {
    const auto found = gpu.find(tracked.handle.index);
    if (found == gpu.end()) return ObservationState::Unobserved;
    if (found->second == ObservationState::Attended) {
        const glm::vec3 toEntity = m_world.getWorldPosition(tracked.handle) - m_view.origin;
        const float length = glm::length(toEntity);
        angleOut = length < MIN_DISTANCE ? 0.0f : std::acos(std::clamp(glm::dot(toEntity / length, m_view.forward), -1.0f, 1.0f));
    }
    return found->second;
}

void ObservationSystem::logLatency() {
    if (!m_config.logLatency || m_time < m_nextLog) return;
    m_nextLog = m_time + m_config.logIntervalSeconds;
    if (m_stats.gpuSamples == 0) return;
    Lit::Log::Info("observation: gpu samples {} rejected {} latency last {:.2f} ms mean {:.2f} ms max {:.2f} ms ({} frames)", m_stats.gpuSamples, m_stats.gpuRejected,
                   m_stats.lastLatencySeconds * 1000.0, m_stats.meanLatencySeconds * 1000.0, m_stats.maxLatencySeconds * 1000.0, m_stats.maxLatencyFrames);
}

void ObservationSystem::update(float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
    m_time += dt;
    ++m_frame;
    removeDeadEntities();

    std::vector<EntityHandle> order = trackedEntities();
    std::sort(order.begin(), order.end(), sortByHandleIndex);

    const bool gpuFresh = m_haveSample && m_frame >= m_sample.frame && (m_time - m_sample.time <= m_config.maxLatencySeconds) && (m_frame - m_sample.frame <= m_config.maxLatencyFrames);
    m_stats.usingGpu = gpuFresh;
    std::unordered_map<uint32_t, ObservationState> gpu;
    if (gpuFresh) rawGpu(gpu);
    else ++m_stats.cpuUpdates;

    m_focus = NULL_ENTITY;
    float bestAngle = WORST_ANGLE;
    for (const EntityHandle handle : order) {
        const auto found = m_entities.find(handle.index);
        if (found == m_entities.end() || found->second.handle != handle) continue;
        Tracked* tracked = &found->second;
        ObservationState raw;
        float angle = 0.0f;
        if (gpuFresh) {
            raw = rawFromGpu(*tracked, gpu, angle);
        } else {
            const float cone = tracked->state == ObservationState::Attended ? m_config.focusConeDegrees * m_config.focusExitScale : m_config.focusConeDegrees;
            raw = rawCpu(*tracked, cone, angle);
        }
        if (raw == ObservationState::Attended && angle < bestAngle) {
            bestAngle = angle;
            m_focus = tracked->handle;
        }
        apply(*tracked, raw);
    }
    logLatency();
}

Json ObservationSystem::toJson() const {
    Json result = Json::object();
    result.set("time", m_time);
    result.set("frame", m_frame);
    std::vector<const Tracked*> order;
    order.reserve(m_entities.size());
    for (const auto& [index, tracked] : m_entities) order.push_back(&tracked);
    std::sort(order.begin(), order.end(), [](const Tracked* lhs, const Tracked* rhs) { return lhs->handle.index < rhs->handle.index; });
    Json list = Json::array();
    for (const Tracked* tracked : order) {
        Json entry = Json::object();
        entry.set("index", tracked->handle.index);
        entry.set("state", static_cast<int>(tracked->state));
        entry.set("candidate", static_cast<int>(tracked->candidate));
        entry.set("since", tracked->candidateSince);
        list.push(std::move(entry));
    }
    result.set("tracked", std::move(list));
    return result;
}

bool ObservationSystem::fromJson(const Json& doc, const std::function<EntityHandle(EntityHandle)>& remap) {
    if (!doc.isObject()) return false;
    m_entities.clear();
    m_haveSample = false;
    m_focus = NULL_ENTITY;
    const Json* timeField = doc.get("time");
    const Json* frameField = doc.get("frame");
    m_time = timeField ? finiteOr(timeField->asNumber(), 0.0) : 0.0;
    m_frame = frameField ? static_cast<uint64_t>(std::clamp(finiteOr(frameField->asNumber(), 0.0), 0.0, 9.0e15)) : 0;
    m_nextLog = m_time;
    const Json* list = doc.get("tracked");
    if (!list) return true;
    for (const Json& entry : list->items()) {
        const Json* indexField = entry.get("index");
        if (!indexField) continue;
        Tracked tracked;
        const EntityHandle saved{clampedIndex(indexField->asNumber()), 0};
        tracked.handle = remap ? remap(saved) : m_world.handleOf(saved.index);
        if (tracked.handle.isNull()) continue;
        tracked.state = clampedState(entry.get("state"));
        tracked.candidate = clampedState(entry.get("candidate"));
        const Json* sinceField = entry.get("since");
        tracked.candidateSince = sinceField ? finiteOr(sinceField->asNumber(), m_time) : m_time;
        m_entities[tracked.handle.index] = tracked;
    }
    return true;
}
