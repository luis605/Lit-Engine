module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "AudioInternal.hpp"

module Engine.Audio;

using namespace AudioInternal;

namespace {
constexpr uint32_t kRenderBlockFrames = 256;
constexpr float kReverbInputScale = 0.15f;
constexpr float kCombLowpassMix = 0.8f;
constexpr float kCombLowpassKeep = 0.2f;
constexpr float kAllpassFeedback = 0.5f;
constexpr float kMinDistanceFloor = 0.01f;
constexpr float kFadeFraction = 0.2f;
constexpr float kFadeWidthFloor = 1.0e-6f;
constexpr float kAttenuationSendBoost = 4.0f;
constexpr float kDirectionEpsilon = 1.0e-6f;
constexpr float kMinDuckTimeConstant = 1.0e-6f;

float panAngle(float pan) { return (pan + 1.0f) * kPi * 0.25f; }
}

AudioMixer::VoiceMix AudioMixer::computeVoiceMix(const Voice& voice) const {
    VoiceMix mix;
    float level = voice.params.gain;
    if (voice.params.spatial) {
        glm::vec3 direction;
        float attenuation;
        if (voice.hasPath) {
            direction = voice.path.arrivalDirection;
            attenuation = voice.path.attenuation;
            mix.lowpassHz = voice.path.lowpassHz;
            mix.send = voice.path.reverbSend * voice.params.reverbSend;
        } else {
            const glm::vec3 offset = voice.params.position - m_listenerPos;
            const float distance = glm::length(offset);
            direction = distance > kDirectionEpsilon ? offset / distance : m_forward;
            const float minDistance = std::max(voice.params.minDistance, kMinDistanceFloor);
            const float fadeWidth = std::max(voice.params.maxDistance * kFadeFraction, kFadeWidthFloor);
            const float fade = std::clamp((voice.params.maxDistance - distance) / fadeWidth, 0.0f, 1.0f);
            attenuation = minDistance / std::max(distance, minDistance) * fade;
            mix.send = m_reverbEstimate.wet * std::min(1.0f, attenuation * kAttenuationSendBoost) * voice.params.reverbSend;
        }
        mix.pan = std::clamp(glm::dot(direction, m_right), -1.0f, 1.0f);
        const float backness = std::max(0.0f, -glm::dot(direction, m_forward));
        const float theta = panAngle(mix.pan);
        mix.gainLeft = std::cos(theta);
        mix.gainRight = std::sin(theta);
        level *= attenuation;
        mix.lowpassHz = std::min(mix.lowpassHz, kFullBandwidthHz + (kBackLowpassHz - kFullBandwidthHz) * backness);
    }
    mix.gainLeft *= level;
    mix.gainRight *= level;
    mix.send *= level;
    return mix;
}

size_t AudioMixer::collectReflectionTaps(const Voice& voice, std::array<ReflectionTap, kMaxReflectionTaps>& taps) const {
    size_t count = 0;
    if (!voice.hasPath || !voice.params.spatial) return count;
    for (const AudioReflection& reflection : voice.path.reflections) {
        if (count >= kMaxReflectionTaps) break;
        if (!(reflection.delay > 0.0f)) continue;
        const uint32_t delaySamples = static_cast<uint32_t>(reflection.delay * static_cast<float>(m_rate));
        if (delaySamples == 0 || delaySamples >= kRing - 2) continue;
        const float pan = std::clamp(glm::dot(reflection.direction, m_right), -1.0f, 1.0f);
        const float theta = panAngle(pan);
        const float gain = reflection.gain * voice.params.gain;
        taps[count++] = {delaySamples, std::cos(theta) * gain, std::sin(theta) * gain};
    }
    return count;
}

float AudioMixer::nextSourceSample(Voice& voice, const AudioClip& clip, double step) {
    if (!voice.active) return 0.0f;
    const size_t count = clip.samples.size();
    size_t index = static_cast<size_t>(voice.position);
    if (index >= count) {
        if (!voice.params.loop) {
            voice.active = false;
            return 0.0f;
        }
        voice.position = std::fmod(voice.position, static_cast<double>(count));
        index = static_cast<size_t>(voice.position);
    }
    const float fraction = static_cast<float>(voice.position - static_cast<double>(index));
    const size_t nextIndex = index + 1 < count ? index + 1 : (voice.params.loop ? 0 : index);
    const float sample = clip.samples[index] * (1.0f - fraction) + clip.samples[nextIndex] * fraction;
    voice.position += step;
    return sample;
}

void AudioMixer::applyHeadShadow(Voice& voice, float delaySamples, float coefficient, bool rightNear, float mono, float& left, float& right) {
    const uint32_t wholeDelay = static_cast<uint32_t>(delaySamples);
    const float fraction = delaySamples - static_cast<float>(wholeDelay);
    const float nearSample = voice.history[(voice.historyPosition - wholeDelay) & kRingMask];
    const float farSample = voice.history[(voice.historyPosition - wholeDelay - 1) & kRingMask];
    const float delayed = nearSample * (1.0f - fraction) + farSample * fraction;
    if (rightNear) {
        voice.shadowLowpassLeft += coefficient * (delayed - voice.shadowLowpassLeft);
        left = voice.shadowLowpassLeft;
        right = mono;
    } else {
        voice.shadowLowpassRight += coefficient * (delayed - voice.shadowLowpassRight);
        right = voice.shadowLowpassRight;
        left = mono;
    }
}

void AudioMixer::mixVoice(Voice& voice, uint32_t frames) {
    const AudioClip& clip = *voice.params.clip;
    Bus& bus = m_buses[voice.bus];
    const VoiceMix mix = computeVoiceMix(voice);
    if (voice.previousGainLeft < 0.0f) {
        voice.previousGainLeft = mix.gainLeft;
        voice.previousGainRight = mix.gainRight;
        voice.previousSend = mix.send;
    }
    const float lowpassCoefficient = onePole(mix.lowpassHz, m_rate);
    const float shadowCoefficient = onePole(kFullBandwidthHz + (kShadowLowpassHz - kFullBandwidthHz) * std::fabs(mix.pan), m_rate);
    const float delaySamples = voice.params.spatial ? std::fabs(mix.pan) * kMaxInterauralDelaySeconds * static_cast<float>(m_rate) : 0.0f;
    const bool rightNear = mix.pan >= 0.0f;
    const double step = static_cast<double>(std::max(voice.params.pitch, 0.0f)) * clip.sampleRate / m_rate;
    std::array<ReflectionTap, kMaxReflectionTaps> taps;
    const size_t tapCount = collectReflectionTaps(voice, taps);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const float blend = static_cast<float>(frame + 1) / static_cast<float>(frames);
        const float gainLeft = voice.previousGainLeft + (mix.gainLeft - voice.previousGainLeft) * blend;
        const float gainRight = voice.previousGainRight + (mix.gainRight - voice.previousGainRight) * blend;
        const float send = voice.previousSend + (mix.send - voice.previousSend) * blend;
        const float source = nextSourceSample(voice, clip, step);
        voice.lowpassState += lowpassCoefficient * (source - voice.lowpassState);
        const float mono = voice.lowpassState;
        voice.history[voice.historyPosition & kRingMask] = mono;
        float left = mono;
        float right = mono;
        if (delaySamples > 0.0f) applyHeadShadow(voice, delaySamples, shadowCoefficient, rightNear, mono, left, right);
        float outLeft = left * gainLeft;
        float outRight = right * gainRight;
        for (size_t tap = 0; tap < tapCount; ++tap) {
            const float tapSample = voice.history[(voice.historyPosition - taps[tap].delaySamples) & kRingMask];
            outLeft += tapSample * taps[tap].gainLeft;
            outRight += tapSample * taps[tap].gainRight;
        }
        ++voice.historyPosition;
        bus.left[frame] += outLeft;
        bus.right[frame] += outRight;
        m_reverbInput[frame] += mono * send;
    }
    voice.previousGainLeft = mix.gainLeft;
    voice.previousGainRight = mix.gainRight;
    voice.previousSend = mix.send;
}

void AudioMixer::processReverb(uint32_t frames) {
    for (int channel = 0; channel < 2; ++channel) {
        std::vector<float>& output = channel ? m_reverbRight : m_reverbLeft;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const float input = m_reverbInput[frame] * kReverbInputScale;
            float accumulated = 0.0f;
            for (int line = 0; line < 4; ++line) {
                std::vector<float>& buffer = m_reverb.comb[channel][line];
                uint32_t& position = m_reverb.combPosition[channel][line];
                float& lowpass = m_reverb.combLowpass[channel][line];
                const float delayed = buffer[position];
                lowpass = delayed * kCombLowpassMix + lowpass * kCombLowpassKeep;
                buffer[position] = input + lowpass * m_reverb.feedback[line];
                if (++position >= buffer.size()) position = 0;
                accumulated += delayed;
            }
            for (int line = 0; line < 2; ++line) {
                std::vector<float>& buffer = m_reverb.allpass[channel][line];
                uint32_t& position = m_reverb.allpassPosition[channel][line];
                const float stored = buffer[position];
                const float filtered = -accumulated + stored;
                buffer[position] = accumulated + stored * kAllpassFeedback;
                if (++position >= buffer.size()) position = 0;
                accumulated = filtered;
            }
            output[frame] += accumulated;
        }
    }
}

void AudioMixer::mixBusesToMaster(uint32_t frames) {
    for (size_t index = m_buses.size(); index-- > 0;) {
        Bus& bus = m_buses[index];
        const float gain = (bus.mute ? 0.0f : bus.gain) * bus.duck;
        double energy = 0.0;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            bus.left[frame] *= gain;
            bus.right[frame] *= gain;
            energy += 0.5 * (static_cast<double>(bus.left[frame]) * bus.left[frame] + static_cast<double>(bus.right[frame]) * bus.right[frame]);
        }
        bus.level = frames ? static_cast<float>(std::sqrt(energy / frames)) : 0.0f;
        if (index == 0) continue;
        Bus& parent = m_buses[bus.parent];
        for (uint32_t frame = 0; frame < frames; ++frame) {
            parent.left[frame] += bus.left[frame];
            parent.right[frame] += bus.right[frame];
        }
    }
}

void AudioMixer::updateDucking(uint32_t frames) {
    for (Bus& bus : m_buses) bus.duckGoal = 1.0f;
    for (const Duck& duck : m_ducks) {
        Bus& target = m_buses[duck.target];
        if (m_buses[duck.trigger].level > duck.threshold && duck.amount < target.duckGoal) {
            target.duckGoal = duck.amount;
            target.duckAttack = duck.attack;
            target.duckRelease = duck.release;
        }
    }
    const float blockSeconds = static_cast<float>(frames) / static_cast<float>(m_rate);
    for (Bus& bus : m_buses) {
        const float timeConstant = bus.duckGoal < bus.duck ? bus.duckAttack : bus.duckRelease;
        const float smoothing = timeConstant > kMinDuckTimeConstant ? 1.0f - std::exp(-blockSeconds / timeConstant) : 1.0f;
        bus.duck += (bus.duckGoal - bus.duck) * smoothing;
    }
}

void AudioMixer::renderBlock(uint32_t frames, float* out) {
    for (Bus& bus : m_buses) {
        bus.left.assign(frames, 0.0f);
        bus.right.assign(frames, 0.0f);
    }
    m_reverbInput.assign(frames, 0.0f);
    m_reverbLeft.assign(frames, 0.0f);
    m_reverbRight.assign(frames, 0.0f);
    for (Voice& voice : m_voices) {
        if (voice.active) mixVoice(voice, frames);
    }
    processReverb(frames);
    Bus& master = m_buses[0];
    for (uint32_t frame = 0; frame < frames; ++frame) {
        master.left[frame] += m_reverbLeft[frame] * m_reverbReturn;
        master.right[frame] += m_reverbRight[frame] * m_reverbReturn;
    }
    mixBusesToMaster(frames);
    updateDucking(frames);
    float peak = 0.0f;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        out[frame * 2] = master.left[frame];
        out[frame * 2 + 1] = master.right[frame];
        peak = std::max({peak, std::fabs(out[frame * 2]), std::fabs(out[frame * 2 + 1])});
    }
    m_stats.peak = peak;
    m_stats.framesRendered += frames;
    std::erase_if(m_voices, [](const Voice& voice) { return !voice.active; });
    m_stats.activeVoices = static_cast<uint32_t>(m_voices.size());
}

void AudioMixer::render(float* out, uint32_t frames) {
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t blockFrames = std::min<uint32_t>(kRenderBlockFrames, frames - done);
        renderBlock(blockFrames, out + static_cast<size_t>(done) * 2);
        done += blockFrames;
    }
}
