module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

module Engine.AudioTrace;

namespace {
constexpr float kGoldenAngle = 2.39996323f;
constexpr float kFanHitOffset = 0.02f;
constexpr float kNormalizeEpsilon = 1.0e-8f;
constexpr float kFullBandwidthHz = 20000.0f;
constexpr float kSabineConstant = 0.161f;
constexpr float kMinAbsorption = 0.01f;
constexpr float kFadeFraction = 0.2f;
constexpr float kFadeWidthFloor = 1.0e-6f;
constexpr float kAudibleThreshold = 1.0e-4f;
constexpr float kReflectionMinDelay = 0.0005f;
constexpr float kReflectionRangeFraction = 0.5f;
constexpr float kReflectionOcclusionLimit = 0.5f;
constexpr float kHelperAxisLimit = 0.9f;
constexpr float kAttenuationSendBoost = 4.0f;
constexpr float kNoPathLength = 1.0e30f;
constexpr size_t kMaxReflections = 4;

glm::vec3 fibonacciDirection(int index, int count) {
    const float y = 1.0f - (static_cast<float>(index) + 0.5f) * 2.0f / static_cast<float>(count);
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float phi = kGoldenAngle * static_cast<float>(index);
    return glm::vec3(std::cos(phi) * radius, y, std::sin(phi) * radius);
}

glm::vec3 unit(const glm::vec3& vector) {
    const float length = glm::length(vector);
    return length > kNormalizeEpsilon ? vector / length : glm::vec3(0.0f, 0.0f, 1.0f);
}
}

void AudioPropagation::setScene(const TriangleSoup& soup) { m_bvh.build(soup); }

bool AudioPropagation::blocked(const glm::vec3& from, const glm::vec3& to) {
    ++m_used;
    return m_bvh.occluded(from, to);
}

float AudioPropagation::distanceGain(float distance) const {
    const float reference = m_config.referenceDistance;
    const float rolloff = reference / std::max(distance, reference);
    const float fadeWidth = std::max(m_config.maxDistance * kFadeFraction, kFadeWidthFloor);
    const float fade = std::clamp((m_config.maxDistance - distance) / fadeWidth, 0.0f, 1.0f);
    return rolloff * fade;
}

void AudioPropagation::beginFrame(const glm::vec3& listener) {
    m_used = 0;
    m_stale = 0;
    m_listener = listener;
    m_fan.clear();
    const int rayCount = m_config.reverbRays;
    float distanceSum = 0.0f;
    int hits = 0;
    for (int index = 0; index < rayCount && canCast(); ++index) {
        FanHit fanHit;
        fanHit.direction = fibonacciDirection(index, rayCount);
        ++m_used;
        const auto hit = m_bvh.raycast(listener, fanHit.direction, m_config.maxDistance);
        if (hit) {
            fanHit.hit = true;
            fanHit.distance = hit->t;
            fanHit.point = hit->point + hit->normal * kFanHitOffset;
            distanceSum += hit->t;
            ++hits;
        } else {
            fanHit.distance = m_config.maxDistance;
        }
        m_fan.push_back(fanHit);
    }
    ReverbEstimate estimate;
    const int total = static_cast<int>(m_fan.size());
    if (total > 0) {
        estimate.escape = static_cast<float>(total - hits) / static_cast<float>(total);
        estimate.meanFreePath = hits > 0 ? distanceSum / static_cast<float>(hits) : m_config.maxDistance;
        const float effectiveAbsorption = m_config.wallAbsorption * (1.0f - estimate.escape) + estimate.escape;
        estimate.rt60 = kSabineConstant * (estimate.meanFreePath * 0.25f) / std::max(effectiveAbsorption, kMinAbsorption);
        estimate.wet = std::clamp(1.0f - estimate.escape, 0.0f, 1.0f);
        if (hits == 0) estimate.rt60 = 0.0f;
    }
    m_reverb = estimate;
}

bool AudioPropagation::hopSearch(const glm::vec3& from, const glm::vec3& source, int depth, float& length, glm::vec3& firstEntry, int& hopId, int& hops) {
    if (!m_hook || depth > m_config.maxHops) return false;
    std::vector<AudioHop>& candidates = m_hopCandidates[static_cast<size_t>(depth)];
    candidates.clear();
    m_hook(from, candidates);
    bool found = false;
    float best = kNoPathLength;
    for (const AudioHop& hop : candidates) {
        if (!canCast()) break;
        if (blocked(from, hop.entry)) continue;
        const float lead = glm::length(hop.entry - from) + hop.cost;
        if (!canCast()) break;
        if (!blocked(hop.exit, source)) {
            const float total = lead + glm::length(source - hop.exit);
            if (total < best) {
                best = total;
                firstEntry = hop.entry;
                hopId = hop.id;
                hops = depth;
                found = true;
            }
        } else if (depth < m_config.maxHops) {
            float subLength = 0.0f;
            glm::vec3 subEntry(0.0f);
            int subHopId = -1;
            int subHops = 0;
            if (hopSearch(hop.exit, source, depth + 1, subLength, subEntry, subHopId, subHops) && lead + subLength < best) {
                best = lead + subLength;
                firstEntry = hop.entry;
                hopId = hop.id;
                hops = subHops;
                found = true;
            }
        }
    }
    length = best;
    return found;
}

float AudioPropagation::measureOcclusion(const glm::vec3& listener, const glm::vec3& source, const glm::vec3& direction) {
    const glm::vec3 helper = std::fabs(direction.y) < kHelperAxisLimit ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangent = unit(glm::cross(direction, helper));
    const glm::vec3 bitangent = glm::cross(direction, tangent);
    const int rayCount = std::max(1, m_config.occlusionRays);
    int blockedCount = 0;
    int cast = 0;
    for (int index = 0; index < rayCount && canCast(); ++index) {
        glm::vec3 target = source;
        if (index > 0) {
            const float radius = m_config.sourceRadius * std::sqrt((static_cast<float>(index) + 0.5f) / static_cast<float>(rayCount));
            const float angle = kGoldenAngle * static_cast<float>(index);
            target = source + tangent * (std::cos(angle) * radius) + bitangent * (std::sin(angle) * radius);
        }
        ++cast;
        if (blocked(listener, target)) ++blockedCount;
    }
    return cast > 0 ? static_cast<float>(blockedCount) / static_cast<float>(cast) : 1.0f;
}

void AudioPropagation::applyHopRoute(AudioPath& path, const glm::vec3& listener, const glm::vec3& source) {
    float length = 0.0f;
    glm::vec3 entry(0.0f);
    int hopId = -1;
    int hops = 0;
    const size_t depthSlots = static_cast<size_t>(std::max(m_config.maxHops, 0)) + 2;
    if (m_hopCandidates.size() < depthSlots) m_hopCandidates.resize(depthSlots);
    if (!hopSearch(listener, source, 1, length, entry, hopId, hops)) return;
    const float hopGain = distanceGain(length);
    if (hopGain <= path.attenuation) return;
    path.viaHop = true;
    path.hopId = hopId;
    path.hops = hops;
    path.pathLength = length;
    path.arrivalDirection = unit(entry - listener);
    path.occlusion = 0.0f;
    path.attenuation = hopGain;
    path.lowpassHz = kFullBandwidthHz;
}

void AudioPropagation::collectReflections(AudioPath& path, const glm::vec3& listener, const glm::vec3& source, float directDistance) {
    m_reflectionOrder.clear();
    for (size_t fanIndex = 0; fanIndex < m_fan.size(); ++fanIndex) {
        const FanHit& fanHit = m_fan[fanIndex];
        if (fanHit.hit && glm::length(fanHit.point - m_listener) < m_config.maxDistance * kReflectionRangeFraction) m_reflectionOrder.emplace_back(fanHit.distance, static_cast<uint32_t>(fanIndex));
    }
    std::sort(m_reflectionOrder.begin(), m_reflectionOrder.end());
    int tested = 0;
    for (const auto& [fanDistance, fanIndex] : m_reflectionOrder) {
        const FanHit* fanHit = &m_fan[fanIndex];
        if (tested >= m_config.reflectionCandidates || !canCast()) break;
        ++tested;
        if (blocked(fanHit->point, source)) continue;
        const float length = fanHit->distance + glm::length(source - fanHit->point);
        const float delay = (length - directDistance) / m_config.speedOfSound;
        if (delay <= kReflectionMinDelay) continue;
        path.reflections.push_back({delay, (1.0f - m_config.wallAbsorption) * distanceGain(length), unit(fanHit->point - listener)});
    }
    std::sort(path.reflections.begin(), path.reflections.end(), [](const AudioReflection& lhs, const AudioReflection& rhs) { return lhs.delay < rhs.delay; });
    if (path.reflections.size() > kMaxReflections) path.reflections.resize(kMaxReflections);
}

AudioPath AudioPropagation::trace(const glm::vec3& listener, const glm::vec3& source) {
    AudioPath path;
    traceInto(path, listener, source);
    return path;
}

void AudioPropagation::traceInto(AudioPath& path, const glm::vec3& listener, const glm::vec3& source) {
    std::vector<AudioReflection> reflections = std::move(path.reflections);
    reflections.clear();
    path = AudioPath{};
    path.reflections = std::move(reflections);
    const uint32_t startRays = m_used;
    const glm::vec3 offset = source - listener;
    const float distance = glm::length(offset);
    path.pathLength = distance;
    path.arrivalDirection = unit(offset);
    path.stale = false;
    if (distance > m_config.maxDistance) {
        path.audible = false;
        path.raysUsed = m_used - startRays;
        return;
    }
    path.directOcclusion = measureOcclusion(listener, source, path.arrivalDirection);
    path.occlusion = path.directOcclusion;
    path.direct = path.directOcclusion < 1.0f;
    path.attenuation = distanceGain(distance) * (1.0f - path.directOcclusion * (1.0f - m_config.occludedTransmission));
    path.lowpassHz = kFullBandwidthHz + (m_config.occludedCutoffHz - kFullBandwidthHz) * path.directOcclusion;
    if (path.directOcclusion >= m_config.hopOcclusionThreshold) applyHopRoute(path, listener, source);
    path.audible = path.attenuation > kAudibleThreshold;
    if (!path.viaHop && path.directOcclusion < kReflectionOcclusionLimit) collectReflections(path, listener, source, distance);
    path.reverbSend = m_reverb.wet * std::min(1.0f, path.attenuation * kAttenuationSendBoost);
    path.raysUsed = m_used - startRays;
}

void AudioPropagation::update(const glm::vec3& listener, const std::vector<AudioEmitter>& emitters) {
    beginFrame(listener);
    ++m_frame;
    m_order.clear();
    for (size_t index = 0; index < emitters.size(); ++index) m_order.emplace_back(glm::length(emitters[index].position - listener), static_cast<uint32_t>(index));
    std::sort(m_order.begin(), m_order.end());
    const uint32_t raysNeeded = static_cast<uint32_t>(std::max(1, m_config.occlusionRays));
    for (const auto& [distance, index] : m_order) {
        const AudioEmitter& emitter = emitters[index];
        if (m_used + raysNeeded > m_config.raysPerFrame) {
            markStale(emitter, listener, distance);
            continue;
        }
        const uint32_t before = m_used;
        ResultSlot& slot = m_results[emitter.id];
        slot.frame = m_frame;
        traceInto(slot.path, listener, emitter.position);
        slot.path.raysUsed = m_used - before;
    }
    std::erase_if(m_results, [&](const auto& entry) { return entry.second.frame != m_frame; });
}

void AudioPropagation::markStale(const AudioEmitter& emitter, const glm::vec3& listener, float distance) {
    ++m_stale;
    const auto existing = m_results.find(emitter.id);
    if (existing != m_results.end()) {
        existing->second.path.stale = true;
        existing->second.frame = m_frame;
        return;
    }
    AudioPath path;
    path.pathLength = distance;
    path.arrivalDirection = unit(emitter.position - listener);
    path.stale = true;
    path.attenuation = distanceGain(distance);
    path.audible = path.attenuation > kAudibleThreshold;
    ResultSlot& slot = m_results[emitter.id];
    slot.path = std::move(path);
    slot.frame = m_frame;
}

const AudioPath* AudioPropagation::result(uint32_t id) const {
    const auto found = m_results.find(id);
    return found == m_results.end() ? nullptr : &found->second.path;
}
