module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "AudioInternal.hpp"

module Engine.Audio;

using namespace AudioInternal;

namespace {
constexpr uint32_t kReferenceRate = 44100;
constexpr uint32_t kRightChannelDelayOffset = 23;
constexpr float kDefaultReverbRt60 = 0.5f;
constexpr float kDefaultReverbWet = 0.3f;
constexpr float kMinReverbRt60 = 0.1f;
constexpr float kMaxReverbRt60 = 6.0f;
constexpr float kMaxCombFeedback = 0.97f;
constexpr float kDirectionEpsilon = 1.0e-8f;
constexpr uint32_t kCombDelays[4] = {1557, 1617, 1491, 1422};
constexpr uint32_t kAllpassDelays[2] = {225, 556};

float decibelsToLinear(float decibels) { return std::pow(10.0f, decibels / 20.0f); }

uint32_t scaledDelay(uint32_t baseDelay, int channel, uint32_t rate) { return (baseDelay + (channel ? kRightChannelDelayOffset : 0)) * rate / kReferenceRate + 1; }
}

AudioMixer::AudioMixer(uint32_t sampleRate) : m_rate(sampleRate) {
    m_buses.push_back({"master"});
    for (int channel = 0; channel < 2; ++channel) {
        for (int line = 0; line < 4; ++line) m_reverb.comb[channel][line].assign(scaledDelay(kCombDelays[line], channel, m_rate), 0.0f);
        for (int line = 0; line < 2; ++line) m_reverb.allpass[channel][line].assign(scaledDelay(kAllpassDelays[line], channel, m_rate), 0.0f);
    }
    ReverbEstimate estimate;
    estimate.rt60 = kDefaultReverbRt60;
    estimate.wet = kDefaultReverbWet;
    setReverb(estimate);
}

int AudioMixer::addBus(const std::string& name, const std::string& parent) {
    const int existing = busIndex(name);
    if (existing >= 0) return existing;
    const int parentIndex = busIndex(parent);
    Bus bus;
    bus.name = name;
    bus.parent = parentIndex < 0 ? 0 : parentIndex;
    m_buses.push_back(std::move(bus));
    return static_cast<int>(m_buses.size() - 1);
}

int AudioMixer::busIndex(const std::string& name) const {
    for (size_t index = 0; index < m_buses.size(); ++index) {
        if (m_buses[index].name == name) return static_cast<int>(index);
    }
    return -1;
}

void AudioMixer::setBusGain(const std::string& name, float linear) {
    const int index = busIndex(name);
    if (index >= 0) m_buses[index].gain = std::max(0.0f, linear);
}

void AudioMixer::setBusGainDb(const std::string& name, float db) { setBusGain(name, decibelsToLinear(db)); }

void AudioMixer::setBusMute(const std::string& name, bool mute) {
    const int index = busIndex(name);
    if (index >= 0) m_buses[index].mute = mute;
}

float AudioMixer::busGain(const std::string& name) const {
    const int index = busIndex(name);
    return index >= 0 ? m_buses[index].gain : 0.0f;
}

float AudioMixer::busLevel(const std::string& name) const {
    const int index = busIndex(name);
    return index >= 0 ? m_buses[index].level : 0.0f;
}

float AudioMixer::busDuck(const std::string& name) const {
    const int index = busIndex(name);
    return index >= 0 ? m_buses[index].duck : 1.0f;
}

void AudioMixer::bindBusParam(const std::string& bus, const std::string& param, float gainAtZero, float gainAtOne) {
    const int index = busIndex(bus);
    if (index >= 0) m_bindings.push_back({index, param, gainAtZero, gainAtOne});
}

void AudioMixer::addDuck(const std::string& trigger, const std::string& target, float amountDb, float threshold, float attackSeconds, float releaseSeconds) {
    const int triggerIndex = busIndex(trigger);
    const int targetIndex = busIndex(target);
    if (triggerIndex < 0 || targetIndex < 0) return;
    m_ducks.push_back({triggerIndex, targetIndex, decibelsToLinear(amountDb), threshold, attackSeconds, releaseSeconds});
}

void AudioMixer::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    m_listenerPos = position;
    const float forwardLength = glm::length(forward);
    m_forward = forwardLength > kDirectionEpsilon ? forward / forwardLength : glm::vec3(0.0f, 0.0f, 1.0f);
    m_up = up;
    const glm::vec3 right = glm::cross(m_forward, m_up);
    const float rightLength = glm::length(right);
    m_right = rightLength > kDirectionEpsilon ? right / rightLength : glm::vec3(1.0f, 0.0f, 0.0f);
}

void AudioMixer::setReverb(const ReverbEstimate& estimate) {
    m_reverbEstimate = estimate;
    const float rt60 = std::clamp(estimate.rt60, kMinReverbRt60, kMaxReverbRt60);
    for (int line = 0; line < 4; ++line) {
        const float delay = static_cast<float>(m_reverb.comb[0][line].size());
        m_reverb.feedback[line] = std::min(kMaxCombFeedback, std::pow(10.0f, -3.0f * delay / (rt60 * static_cast<float>(m_rate))));
    }
}

AudioMixer::Voice* AudioMixer::findVoice(VoiceId id) {
    const auto found = std::ranges::lower_bound(m_voices, id, {}, &Voice::id);
    return found != m_voices.end() && found->id == id ? &*found : nullptr;
}

const AudioMixer::Voice* AudioMixer::findVoice(VoiceId id) const {
    const auto found = std::ranges::lower_bound(m_voices, id, {}, &Voice::id);
    return found != m_voices.end() && found->id == id ? &*found : nullptr;
}

namespace {
float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

glm::vec3 finitePosition(const glm::vec3& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) ? position : glm::vec3(0.0f);
}

VoiceParams sanitized(VoiceParams params) {
    params.gain = finiteOr(params.gain, 0.0f);
    params.pitch = finiteOr(params.pitch, 1.0f);
    params.minDistance = finiteOr(params.minDistance, 1.0f);
    params.maxDistance = finiteOr(params.maxDistance, 50.0f);
    params.reverbSend = finiteOr(params.reverbSend, 0.0f);
    params.startTime = finiteOr(params.startTime, 0.0f);
    params.position = finitePosition(params.position);
    return params;
}
}

VoiceId AudioMixer::play(const VoiceParams& requested) {
    if (!requested.clip || requested.clip->samples.empty() || requested.clip->sampleRate == 0) return 0;
    const VoiceParams params = sanitized(requested);
    Voice voice;
    voice.id = m_nextVoice++;
    voice.params = params;
    const int bus = busIndex(params.bus);
    voice.bus = bus < 0 ? 0 : bus;
    voice.position = static_cast<double>(std::max(params.startTime, 0.0f)) * params.clip->sampleRate;
    voice.active = true;
    voice.history.assign(kRing, 0.0f);
    m_voices.push_back(std::move(voice));
    return m_voices.back().id;
}

void AudioMixer::stop(VoiceId id) {
    if (Voice* voice = findVoice(id)) voice->active = false;
}

bool AudioMixer::isPlaying(VoiceId id) const {
    const Voice* voice = findVoice(id);
    return voice && voice->active;
}

float AudioMixer::voiceTime(VoiceId id) const {
    const Voice* voice = findVoice(id);
    return voice ? static_cast<float>(voice->position / voice->params.clip->sampleRate) : 0.0f;
}

void AudioMixer::setVoicePosition(VoiceId id, const glm::vec3& position) {
    if (Voice* voice = findVoice(id)) voice->params.position = finitePosition(position);
}

void AudioMixer::setVoiceGain(VoiceId id, float gain) {
    if (Voice* voice = findVoice(id)) voice->params.gain = finiteOr(gain, 0.0f);
}

void AudioMixer::setVoicePath(VoiceId id, const AudioPath& path) {
    Voice* voice = findVoice(id);
    if (!voice) return;
    voice->path = path;
    voice->hasPath = true;
}

void AudioMixer::setVoiceBus(VoiceId id, const std::string& bus) {
    const int index = busIndex(bus);
    if (index < 0) return;
    if (Voice* voice = findVoice(id)) voice->bus = index;
}

uint32_t AudioMixer::voiceCount() const {
    return static_cast<uint32_t>(std::ranges::count_if(m_voices, [](const Voice& voice) { return voice.active; }));
}

void AudioMixer::update(const ParamBus* params) {
    if (!params) return;
    for (const Binding& binding : m_bindings) {
        if (!params->has(binding.param)) continue;
        const float normalized = std::clamp(params->get(binding.param), 0.0f, 1.0f);
        m_buses[binding.bus].gain = binding.gainAtZero + (binding.gainAtOne - binding.gainAtZero) * normalized;
    }
}
