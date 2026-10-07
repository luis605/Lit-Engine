module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

module Engine.AudioTrace;

namespace {
glm::vec3 fibonacci(int i, int n) {
    const float golden = 2.39996323f;
    const float y = 1.0f - (static_cast<float>(i) + 0.5f) * 2.0f / static_cast<float>(n);
    const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float phi = golden * static_cast<float>(i);
    return glm::vec3(std::cos(phi) * r, y, std::sin(phi) * r);
}

glm::vec3 unit(const glm::vec3& v) {
    const float l = glm::length(v);
    return l > 1.0e-8f ? v / l : glm::vec3(0.0f, 0.0f, 1.0f);
}
}

void AudioPropagation::setScene(const TriangleSoup& soup) { m_bvh.build(soup); }

bool AudioPropagation::blocked(const glm::vec3& a, const glm::vec3& b) {
    ++m_used;
    return m_bvh.occluded(a, b);
}

float AudioPropagation::distanceGain(float d) const {
    const float ref = m_config.referenceDistance;
    const float g = ref / std::max(d, ref);
    const float fade = std::clamp((m_config.maxDistance - d) / (m_config.maxDistance * 0.2f), 0.0f, 1.0f);
    return g * fade;
}

void AudioPropagation::beginFrame(const glm::vec3& listener) {
    m_used = 0;
    m_stale = 0;
    m_listener = listener;
    m_fan.clear();
    const int n = m_config.reverbRays;
    float sum = 0.0f;
    int hits = 0;
    for (int i = 0; i < n && canCast(); ++i) {
        FanHit f;
        f.direction = fibonacci(i, n);
        ++m_used;
        const auto h = m_bvh.raycast(listener, f.direction, m_config.maxDistance);
        if (h) {
            f.hit = true;
            f.distance = h->t;
            f.point = h->point + h->normal * 0.02f;
            sum += h->t;
            ++hits;
        } else {
            f.distance = m_config.maxDistance;
        }
        m_fan.push_back(f);
    }
    ReverbEstimate r;
    const int total = static_cast<int>(m_fan.size());
    if (total > 0) {
        r.escape = static_cast<float>(total - hits) / static_cast<float>(total);
        r.meanFreePath = hits > 0 ? sum / static_cast<float>(hits) : m_config.maxDistance;
        const float aEff = m_config.wallAbsorption * (1.0f - r.escape) + r.escape;
        r.rt60 = 0.161f * (r.meanFreePath * 0.25f) / std::max(aEff, 0.01f);
        r.wet = std::clamp(1.0f - r.escape, 0.0f, 1.0f);
        if (hits == 0) r.rt60 = 0.0f;
    }
    m_reverb = r;
}

bool AudioPropagation::hopSearch(const glm::vec3& from, const glm::vec3& source, int depth, float& length, glm::vec3& firstEntry, int& hopId, int& hops) {
    if (!m_hook || depth > m_config.maxHops) return false;
    std::vector<AudioHop> hopsOut;
    m_hook(from, hopsOut);
    bool found = false;
    float best = 1.0e30f;
    for (const AudioHop& h : hopsOut) {
        if (!canCast()) break;
        if (blocked(from, h.entry)) continue;
        const float lead = glm::length(h.entry - from) + h.cost;
        if (!canCast()) break;
        if (!blocked(h.exit, source)) {
            const float total = lead + glm::length(source - h.exit);
            if (total < best) {
                best = total;
                firstEntry = h.entry;
                hopId = h.id;
                hops = depth;
                found = true;
            }
        } else if (depth < m_config.maxHops) {
            float sub = 0.0f;
            glm::vec3 entry2;
            int id2 = -1;
            int hops2 = 0;
            if (hopSearch(h.exit, source, depth + 1, sub, entry2, id2, hops2) && lead + sub < best) {
                best = lead + sub;
                firstEntry = h.entry;
                hopId = h.id;
                hops = hops2;
                found = true;
            }
        }
    }
    length = best;
    return found;
}

AudioPath AudioPropagation::trace(const glm::vec3& listener, const glm::vec3& source) {
    AudioPath path;
    const uint32_t startRays = m_used;
    const glm::vec3 d = source - listener;
    const float dist = glm::length(d);
    path.pathLength = dist;
    path.arrivalDirection = unit(d);
    path.stale = false;
    if (dist > m_config.maxDistance) {
        path.audible = false;
        path.raysUsed = m_used - startRays;
        return path;
    }
    const glm::vec3 dir = path.arrivalDirection;
    glm::vec3 helper = std::fabs(dir.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 u = unit(glm::cross(dir, helper));
    const glm::vec3 v = glm::cross(dir, u);
    const int n = std::max(1, m_config.occlusionRays);
    int blockedCount = 0;
    int cast = 0;
    for (int i = 0; i < n && canCast(); ++i) {
        glm::vec3 p = source;
        if (i > 0) {
            const float rr = m_config.sourceRadius * std::sqrt((static_cast<float>(i) + 0.5f) / static_cast<float>(n));
            const float a = 2.39996323f * static_cast<float>(i);
            p = source + u * (std::cos(a) * rr) + v * (std::sin(a) * rr);
        }
        ++cast;
        if (blocked(listener, p)) ++blockedCount;
    }
    path.directOcclusion = cast > 0 ? static_cast<float>(blockedCount) / static_cast<float>(cast) : 1.0f;
    path.occlusion = path.directOcclusion;
    path.direct = path.directOcclusion < 1.0f;
    const float dg = distanceGain(dist);
    float directGain = dg * (1.0f - path.directOcclusion * (1.0f - m_config.occludedTransmission));
    path.attenuation = directGain;
    path.lowpassHz = 20000.0f + (m_config.occludedCutoffHz - 20000.0f) * path.directOcclusion;

    if (path.directOcclusion >= m_config.hopOcclusionThreshold) {
        float len = 0.0f;
        glm::vec3 entry(0.0f);
        int hopId = -1;
        int hops = 0;
        if (hopSearch(listener, source, 1, len, entry, hopId, hops)) {
            const float hg = distanceGain(len);
            if (hg > directGain) {
                path.viaHop = true;
                path.hopId = hopId;
                path.hops = hops;
                path.pathLength = len;
                path.arrivalDirection = unit(entry - listener);
                path.occlusion = 0.0f;
                path.attenuation = hg;
                path.lowpassHz = 20000.0f;
                directGain = hg;
            }
        }
    }
    path.audible = path.attenuation > 1.0e-4f;

    if (!path.viaHop && path.directOcclusion < 0.5f) {
        std::vector<const FanHit*> cands;
        for (const FanHit& f : m_fan) {
            if (f.hit && glm::length(f.point - m_listener) < m_config.maxDistance * 0.5f) cands.push_back(&f);
        }
        std::stable_sort(cands.begin(), cands.end(), [](const FanHit* a, const FanHit* b) { return a->distance < b->distance; });
        int tested = 0;
        for (const FanHit* f : cands) {
            if (tested >= m_config.reflectionCandidates || !canCast()) break;
            ++tested;
            if (blocked(f->point, source)) continue;
            const float len = f->distance + glm::length(source - f->point);
            const float delay = (len - dist) / m_config.speedOfSound;
            if (delay <= 0.0005f) continue;
            path.reflections.push_back({delay, (1.0f - m_config.wallAbsorption) * distanceGain(len), unit(f->point - listener)});
        }
        std::sort(path.reflections.begin(), path.reflections.end(), [](const AudioReflection& a, const AudioReflection& b) { return a.delay < b.delay; });
        if (path.reflections.size() > 4) path.reflections.resize(4);
    }
    path.reverbSend = m_reverb.wet * std::min(1.0f, path.attenuation * 4.0f);
    path.raysUsed = m_used - startRays;
    return path;
}

void AudioPropagation::update(const glm::vec3& listener, const std::vector<AudioEmitter>& emitters) {
    beginFrame(listener);
    std::vector<size_t> order(emitters.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return glm::length(emitters[a].position - listener) < glm::length(emitters[b].position - listener); });
    std::map<uint32_t, bool> present;
    for (const size_t i : order) {
        const AudioEmitter& e = emitters[i];
        present[e.id] = true;
        const uint32_t before = m_used;
        const uint32_t need = static_cast<uint32_t>(std::max(1, m_config.occlusionRays));
        if (m_used + need > m_config.raysPerFrame) {
            auto it = m_results.find(e.id);
            if (it == m_results.end()) {
                AudioPath p;
                p.pathLength = glm::length(e.position - listener);
                p.arrivalDirection = unit(e.position - listener);
                p.stale = true;
                p.attenuation = distanceGain(p.pathLength);
                p.audible = p.attenuation > 1.0e-4f;
                m_results[e.id] = p;
            } else {
                it->second.stale = true;
            }
            ++m_stale;
            continue;
        }
        AudioPath p = trace(listener, e.position);
        p.raysUsed = m_used - before;
        m_results[e.id] = std::move(p);
    }
    for (auto it = m_results.begin(); it != m_results.end();) {
        it = present.contains(it->first) ? std::next(it) : m_results.erase(it);
    }
}

const AudioPath* AudioPropagation::result(uint32_t id) const {
    const auto it = m_results.find(id);
    return it == m_results.end() ? nullptr : &it->second;
}
