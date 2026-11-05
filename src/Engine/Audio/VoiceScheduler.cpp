module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

module Engine.Audio;

namespace {
constexpr float kEnergyWindowSeconds = 0.03f;
constexpr float kEnergyGain = 3.0f;
constexpr float kResumeEndMargin = 0.05f;
constexpr float kMinPlaybackDistance = 5.0f;
constexpr float kPlaybackDistanceScale = 1.5f;
}

float VoiceScheduler::clipTime() const { return m_active ? m_mixer.voiceTime(m_voice) : m_lastTime; }

float VoiceScheduler::energy() const {
    if (!m_active || !m_line.clip) return 0.0f;
    return std::clamp(m_line.clip->rms(clipTime(), kEnergyWindowSeconds) * kEnergyGain, 0.0f, 1.0f);
}

void VoiceScheduler::log(LineEvent type, const std::string& id, float clipTimeSeconds) { m_events.push_back({type, id, clipTimeSeconds, m_update}); }

void VoiceScheduler::start(VoiceLine line, float offset, bool resumed) {
    VoiceParams params;
    params.clip = line.clip;
    params.bus = line.bus;
    params.gain = line.gain;
    params.spatial = line.positional;
    params.position = line.position;
    params.maxDistance = std::max(line.maxDistance * kPlaybackDistanceScale, kMinPlaybackDistance);
    params.startTime = offset;
    m_voice = m_mixer.play(params);
    m_line = std::move(line);
    m_active = m_voice != 0;
    m_lastTime = offset;
    log(resumed ? LineEvent::Resumed : LineEvent::Started, m_line.id, offset);
    if (!m_active) log(LineEvent::Dropped, m_line.id, offset);
}

void VoiceScheduler::interruptCurrent() {
    const float time = clipTime();
    m_mixer.stop(m_voice);
    m_active = false;
    m_lastTime = time;
    log(LineEvent::Interrupted, m_line.id, time);
    if (m_line.resumable && m_line.clip && time < m_line.clip->duration() - kResumeEndMargin) {
        m_pending.push_back({m_line, std::max(0.0f, time - m_line.rewindSeconds), m_activeSeq, true});
    } else {
        log(LineEvent::Dropped, m_line.id, time);
    }
}

bool VoiceScheduler::eligible(const Pending& pending) const {
    if (!pending.line.positional) return true;
    const float distance = glm::length(pending.line.position - m_mixer.listenerPosition());
    return distance <= (pending.resumed ? pending.line.resumeDistance : pending.line.maxDistance);
}

bool VoiceScheduler::hasInterruptingPending() const {
    return std::ranges::any_of(m_pending, [this](const Pending& pending) { return pending.line.priority > m_line.priority && eligible(pending); });
}

void VoiceScheduler::startNext() {
    const auto better = [](const Pending& candidate, const Pending& incumbent) {
        return candidate.line.priority > incumbent.line.priority || (candidate.line.priority == incumbent.line.priority && candidate.seq < incumbent.seq);
    };
    auto best = m_pending.end();
    for (auto candidate = m_pending.begin(); candidate != m_pending.end(); ++candidate) {
        if (!eligible(*candidate)) continue;
        if (best == m_pending.end() || better(*candidate, *best)) best = candidate;
    }
    if (best == m_pending.end()) return;
    Pending next = std::move(*best);
    m_pending.erase(best);
    m_activeSeq = next.seq;
    start(std::move(next.line), next.offset, next.resumed);
}

void VoiceScheduler::enqueue(VoiceLine line) {
    const uint64_t seq = m_seq++;
    if (m_active && line.priority > m_line.priority && m_line.interruptible) interruptCurrent();
    if (!m_active) {
        m_activeSeq = seq;
        start(std::move(line), 0.0f, false);
        return;
    }
    m_pending.push_back({std::move(line), 0.0f, seq, false});
}

void VoiceScheduler::stop(const std::string& id) {
    if (m_active && m_line.id == id) {
        m_lastTime = clipTime();
        m_mixer.stop(m_voice);
        m_active = false;
        log(LineEvent::Dropped, id, m_lastTime);
    }
    std::erase_if(m_pending, [&](const Pending& pending) { return pending.line.id == id; });
}

void VoiceScheduler::stopAll() {
    if (m_active) {
        m_mixer.stop(m_voice);
        m_active = false;
        log(LineEvent::Dropped, m_line.id, clipTime());
    }
    m_pending.clear();
}

void VoiceScheduler::setSpeakerPosition(const glm::vec3& position) {
    if (!m_active) return;
    m_line.position = position;
    m_mixer.setVoicePosition(m_voice, position);
}

void VoiceScheduler::update(float) {
    ++m_update;
    if (m_active) {
        m_lastTime = m_mixer.voiceTime(m_voice);
        if (!m_mixer.isPlaying(m_voice)) {
            m_active = false;
            log(LineEvent::Finished, m_line.id, m_lastTime);
        } else if (m_line.positional && glm::length(m_line.position - m_mixer.listenerPosition()) > m_line.maxDistance) {
            interruptCurrent();
        } else if (m_line.interruptible && hasInterruptingPending()) {
            interruptCurrent();
        }
    }
    if (!m_active) startNext();
}
