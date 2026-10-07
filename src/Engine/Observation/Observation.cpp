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

ObservationView observationViewFromCamera(const Camera& camera, float aspect) {
    const glm::mat4 view = camera.getViewMatrix();
    ObservationView out;
    out.origin = camera.getPosition();
    out.forward = glm::normalize(-glm::vec3(view[0][2], view[1][2], view[2][2]));
    out.up = glm::normalize(glm::vec3(view[0][1], view[1][1], view[2][1]));
    out.fovY = camera.getFov();
    out.aspect = aspect;
    return out;
}

void ObservationSystem::track(EntityHandle e) {
    if (!m_world.isAlive(e)) return;
    Tracked& t = m_entities[e.index];
    if (t.handle == e) return;
    t = Tracked{};
    t.handle = e;
}

void ObservationSystem::untrack(EntityHandle e) {
    const auto it = m_entities.find(e.index);
    if (it != m_entities.end() && it->second.handle == e) m_entities.erase(it);
}

ObservationState ObservationSystem::state(EntityHandle e) const {
    const auto it = m_entities.find(e.index);
    return it != m_entities.end() && it->second.handle == e ? it->second.state : ObservationState::Unobserved;
}

std::vector<EntityHandle> ObservationSystem::inState(ObservationState s) const {
    std::vector<EntityHandle> out;
    for (const auto& [index, t] : m_entities) {
        if (t.state == s) out.push_back(t.handle);
    }
    std::sort(out.begin(), out.end(), [](EntityHandle a, EntityHandle b) { return a.index < b.index; });
    return out;
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
    for (uint32_t y = 0; y < m_sample.height; ++y) {
        const float ny = ((static_cast<float>(y) + 0.5f) / static_cast<float>(m_sample.height)) * 2.0f - 1.0f;
        for (uint32_t x = 0; x < m_sample.width; ++x) {
            const uint32_t id = m_sample.ids[static_cast<size_t>(y) * m_sample.width + x];
            if (id == 0) continue;
            const float nx = ((static_cast<float>(x) + 0.5f) / static_cast<float>(m_sample.width)) * 2.0f - 1.0f;
            const float radial = std::sqrt(nx * nx * tanX * tanX + ny * ny * tanY * tanY);
            const auto it = m_entities.find(id - 1);
            if (it == m_entities.end() || !m_world.isAlive(it->second.handle)) continue;
            ObservationState& s = out[id - 1];
            s = std::max(s, ObservationState::Seen);
            const float cone = it->second.state == ObservationState::Attended ? coneTan * m_config.focusExitScale : coneTan;
            if (radial <= cone) s = ObservationState::Attended;
        }
    }
}

ObservationState ObservationSystem::rawCpu(const Tracked& t, float coneDegrees, float& angleOut) {
    const glm::vec4 bounds = m_world.getWorldBounds(t.handle);
    const glm::vec3 center = bounds.w > 0.0f ? glm::vec3(bounds) : m_world.getWorldPosition(t.handle);
    const float radius = std::max(bounds.w, 0.0f);
    const glm::vec3 to = center - m_view.origin;
    const float dist = glm::length(to);
    angleOut = 180.0f;
    if (dist > m_config.maxDistance + radius) return ObservationState::Unobserved;
    if (dist < 1.0e-6f) return ObservationState::Attended;

    const glm::vec3 dir = to / dist;
    const glm::vec3 right = glm::normalize(glm::cross(m_view.forward, m_view.up));
    const glm::vec3 up = glm::cross(right, m_view.forward);
    const float depth = glm::dot(to, m_view.forward);
    const float angularRadius = radius > 0.0f ? std::asin(std::min(1.0f, radius / dist)) : 0.0f;
    angleOut = std::acos(std::clamp(glm::dot(dir, m_view.forward), -1.0f, 1.0f));

    const float tanY = std::tan(glm::radians(m_view.fovY) * 0.5f);
    const float tanX = tanY * m_view.aspect;
    const float slack = radius;
    if (depth + radius <= 0.0f) return ObservationState::Unobserved;
    const float px = glm::dot(to, right);
    const float py = glm::dot(to, up);
    if (std::abs(px) > depth * tanX + slack * std::sqrt(1.0f + tanX * tanX)) return ObservationState::Unobserved;
    if (std::abs(py) > depth * tanY + slack * std::sqrt(1.0f + tanY * tanY)) return ObservationState::Unobserved;

    float travelled = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const auto hit = m_world.raycast(m_view.origin + dir * travelled, dir, dist + radius - travelled);
        if (!hit) break;
        const float at = travelled + hit->distance;
        if (hit->entity == t.handle || at >= dist - radius) break;
        if (m_ignore && (hit->entity == m_ignore || m_world.isDescendantOf(hit->entity, m_ignore))) {
            travelled = at + 1.0e-3f;
            continue;
        }
        return ObservationState::Unobserved;
    }

    const float cone = glm::radians(coneDegrees) + angularRadius;
    return angleOut <= cone ? ObservationState::Attended : ObservationState::Seen;
}

void ObservationSystem::apply(Tracked& t, ObservationState raw) {
    if (raw == t.state) {
        t.candidate = t.state;
        return;
    }
    const bool up = raw > t.state;
    const bool continuing = up ? t.candidate > t.state : t.candidate < t.state;
    if (!continuing) t.candidateSince = m_time;
    t.candidate = raw;
    const double elapsed = m_time - t.candidateSince + 1.0e-9;
    ObservationState next = t.state;
    if (up) {
        if (elapsed >= m_config.seenEnter) next = ObservationState::Seen;
        if (raw == ObservationState::Attended && elapsed >= m_config.attendEnter) next = ObservationState::Attended;
        next = std::min(next, raw);
        next = std::max(next, t.state);
    } else if (elapsed >= (t.state == ObservationState::Attended ? m_config.attendExit : m_config.seenExit)) {
        next = raw;
    }
    if (next == t.state) return;
    const ObservationState from = t.state;
    t.state = next;
    if (next == raw) t.candidate = raw;
    m_world.events().emitNow(ObservationChanged{t.handle, from, next, m_time});
}

void ObservationSystem::update(float dt) {
    m_time += dt;
    ++m_frame;

    for (auto it = m_entities.begin(); it != m_entities.end();) {
        if (!m_world.isAlive(it->second.handle)) {
            if (it->second.state != ObservationState::Unobserved) {
                m_world.events().emitNow(ObservationChanged{it->second.handle, it->second.state, ObservationState::Unobserved, m_time});
            }
            it = m_entities.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<Tracked*> order;
    order.reserve(m_entities.size());
    for (auto& [index, t] : m_entities) order.push_back(&t);
    std::sort(order.begin(), order.end(), [](const Tracked* a, const Tracked* b) { return a->handle.index < b->handle.index; });

    const bool gpuFresh = m_haveSample && (m_time - m_sample.time <= m_config.maxLatencySeconds) && (m_frame - m_sample.frame <= m_config.maxLatencyFrames);
    m_stats.usingGpu = gpuFresh;
    std::unordered_map<uint32_t, ObservationState> gpu;
    if (gpuFresh) rawGpu(gpu);
    else ++m_stats.cpuUpdates;

    m_focus = NULL_ENTITY;
    float bestAngle = 1.0e9f;
    for (Tracked* t : order) {
        ObservationState raw;
        float angle = 0.0f;
        if (gpuFresh) {
            const auto it = gpu.find(t->handle.index);
            raw = it == gpu.end() ? ObservationState::Unobserved : it->second;
            if (raw == ObservationState::Attended) {
                const glm::vec3 to = m_world.getWorldPosition(t->handle) - m_view.origin;
                const float len = glm::length(to);
                angle = len < 1.0e-6f ? 0.0f : std::acos(std::clamp(glm::dot(to / len, m_view.forward), -1.0f, 1.0f));
            }
        } else {
            const float cone = t->state == ObservationState::Attended ? m_config.focusConeDegrees * m_config.focusExitScale : m_config.focusConeDegrees;
            raw = rawCpu(*t, cone, angle);
        }
        if (raw == ObservationState::Attended && angle < bestAngle) {
            bestAngle = angle;
            m_focus = t->handle;
        }
        apply(*t, raw);
    }

    if (m_config.logLatency && m_time >= m_nextLog) {
        m_nextLog = m_time + m_config.logIntervalSeconds;
        if (m_stats.gpuSamples > 0) {
            Lit::Log::Info("observation: gpu samples {} rejected {} latency last {:.2f} ms mean {:.2f} ms max {:.2f} ms ({} frames)", m_stats.gpuSamples, m_stats.gpuRejected, m_stats.lastLatencySeconds * 1000.0, m_stats.meanLatencySeconds * 1000.0, m_stats.maxLatencySeconds * 1000.0, m_stats.maxLatencyFrames);
        }
    }
}

Json ObservationSystem::toJson() const {
    Json out = Json::object();
    out.set("time", m_time);
    out.set("frame", m_frame);
    Json list = Json::array();
    std::vector<const Tracked*> order;
    for (const auto& [index, t] : m_entities) order.push_back(&t);
    std::sort(order.begin(), order.end(), [](const Tracked* a, const Tracked* b) { return a->handle.index < b->handle.index; });
    for (const Tracked* t : order) {
        Json e = Json::object();
        e.set("index", t->handle.index);
        e.set("state", static_cast<int>(t->state));
        e.set("candidate", static_cast<int>(t->candidate));
        e.set("since", t->candidateSince);
        list.push(std::move(e));
    }
    out.set("tracked", std::move(list));
    return out;
}

bool ObservationSystem::fromJson(const Json& doc, const std::function<EntityHandle(EntityHandle)>& remap) {
    if (!doc.isObject()) return false;
    m_entities.clear();
    m_haveSample = false;
    m_focus = NULL_ENTITY;
    m_time = doc.get("time") ? doc.get("time")->asNumber() : 0.0;
    m_frame = doc.get("frame") ? static_cast<uint64_t>(doc.get("frame")->asNumber()) : 0;
    m_nextLog = m_time;
    const Json* list = doc.get("tracked");
    if (!list) return true;
    for (const Json& e : list->items()) {
        Tracked t;
        t.handle = EntityHandle{static_cast<uint32_t>(e.get("index")->asNumber()), 0};
        t.handle = remap ? remap(t.handle) : m_world.handleOf(t.handle.index);
        if (t.handle.isNull()) continue;
        t.state = static_cast<ObservationState>(std::clamp(static_cast<int>(e.get("state")->asNumber()), 0, 2));
        t.candidate = static_cast<ObservationState>(std::clamp(static_cast<int>(e.get("candidate")->asNumber()), 0, 2));
        t.candidateSince = e.get("since")->asNumber();
        m_entities[t.handle.index] = t;
    }
    return true;
}
