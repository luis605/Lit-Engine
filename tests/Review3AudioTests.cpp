#include "Review3Suites.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

import Engine.Audio;
import Engine.glm;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

std::string wavPath(const char* name) { return testsupport::tempFilePath("lit_review3_", name); }

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    put16(out, static_cast<uint16_t>(value & 0xFFFF));
    put16(out, static_cast<uint16_t>(value >> 16));
}

std::vector<uint8_t> makeWav(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits, const std::vector<uint8_t>& payload, uint32_t declared) {
    std::vector<uint8_t> out = {'R', 'I', 'F', 'F'};
    put32(out, 36 + declared);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(out, 16);
    put16(out, format);
    put16(out, channels);
    put32(out, rate);
    put32(out, rate * channels * bits / 8);
    put16(out, static_cast<uint16_t>(channels * bits / 8));
    put16(out, bits);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(out, declared);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::shared_ptr<AudioClip> loadBytes(const char* name, const std::vector<uint8_t>& bytes) {
    const std::string path = wavPath(name);
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    auto clip = AudioClip::loadWav(path);
    std::remove(path.c_str());
    return clip;
}

void testWavAdversarial() {
    const std::vector<uint8_t> pcm(16, 0x10);
    CHECK(loadBytes("truncated", makeWav(1, 1, 48000, 16, pcm, 4000000000u)) != nullptr);
    CHECK(loadBytes("zero_rate", makeWav(1, 1, 0, 16, pcm, 16)) == nullptr);
    CHECK(loadBytes("zero_channels", makeWav(1, 0, 48000, 16, pcm, 16)) == nullptr);
    CHECK(loadBytes("bits24", makeWav(1, 1, 48000, 24, pcm, 15)) == nullptr);
    CHECK(loadBytes("bits12", makeWav(1, 1, 48000, 12, pcm, 16)) == nullptr);
    CHECK(loadBytes("adpcm", makeWav(2, 1, 48000, 16, pcm, 16)) == nullptr);
    CHECK(loadBytes("short", std::vector<uint8_t>(10, 0)) == nullptr);

    std::vector<uint8_t> nanPayload(8);
    const float poison = kNan;
    std::memcpy(nanPayload.data(), &poison, 4);
    const float fine = 0.5f;
    std::memcpy(nanPayload.data() + 4, &fine, 4);
    const auto clip = loadBytes("nan", makeWav(3, 1, 48000, 32, nanPayload, 8));
    CHECK(clip != nullptr);
    if (clip) {
        for (const float sample : clip->samples) CHECK(std::isfinite(sample));
    }
}

bool renderFinite(AudioMixer& mixer) {
    std::vector<float> buffer(512 * 2);
    mixer.update();
    mixer.render(buffer.data(), 512);
    for (const float sample : buffer) {
        if (!std::isfinite(sample)) return false;
    }
    return true;
}

void testMixerNonFiniteParams() {
    const auto clip = AudioClip::sine("tone", 440.0f, 0.5f);
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {kNan, inf, -inf}) {
        for (const bool loop : {false, true}) {
            AudioMixer mixer;
            VoiceParams pitched;
            pitched.clip = clip;
            pitched.loop = loop;
            pitched.pitch = bad;
            mixer.play(pitched);
            VoiceParams started;
            started.clip = clip;
            started.loop = loop;
            started.startTime = bad;
            mixer.play(started);
            VoiceParams gained;
            gained.clip = clip;
            gained.loop = loop;
            gained.gain = bad;
            gained.position = glm::vec3(bad, 0.0f, 1.0f);
            gained.maxDistance = bad;
            mixer.play(gained);
            CHECK(renderFinite(mixer));
            CHECK(renderFinite(mixer));
        }
    }
    AudioMixer zero;
    VoiceParams still;
    still.clip = clip;
    still.pitch = 0.0f;
    const VoiceId voice = zero.play(still);
    CHECK(renderFinite(zero));
    CHECK(std::isfinite(zero.voiceTime(voice)));
}

void testSchedulerNullClip() {
    AudioMixer mixer;
    VoiceScheduler scheduler(mixer);
    VoiceLine line;
    line.id = "empty";
    scheduler.enqueue(line);
    scheduler.update(0.016f);
    CHECK(scheduler.current() == nullptr);
    CHECK(scheduler.pending() == 0);
    VoiceLine nan;
    nan.id = "nan";
    nan.clip = AudioClip::sine("t", 220.0f, 1.0f);
    nan.position = glm::vec3(kNan);
    scheduler.enqueue(nan);
    for (int step = 0; step < 4; ++step) scheduler.update(0.016f);
    CHECK(scheduler.energy() >= 0.0f);
}

}

void runReview3AudioTests() {
    testWavAdversarial();
    testMixerNonFiniteParams();
    testSchedulerNullClip();
}
