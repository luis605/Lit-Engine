module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "AudioInternal.hpp"

module Engine.Audio;

import Engine.Rng;

using namespace AudioInternal;

namespace {
constexpr size_t kWavHeaderBytes = 44;
constexpr size_t kWavChunkHeaderBytes = 8;
constexpr size_t kWavFmtBodyBytes = 16;
constexpr size_t kRiffFormOffset = 8;
constexpr size_t kFirstChunkOffset = 12;
constexpr uint16_t kWavFormatPcm = 1;
constexpr uint16_t kWavFormatFloat = 3;
constexpr uint16_t kWavFormatExtensible = 0xFFFE;
constexpr float kPcm16Scale = 32767.0f;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

void appendUint16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void appendUint32(std::vector<uint8_t>& out, uint32_t value) {
    appendUint16(out, static_cast<uint16_t>(value & 0xFFFF));
    appendUint16(out, static_cast<uint16_t>(value >> 16));
}

bool writeWav(const std::string& path, const std::vector<float>& data, uint32_t rate, uint16_t channels) {
    std::vector<uint8_t> out;
    const uint32_t dataBytes = static_cast<uint32_t>(data.size() * 2);
    out.reserve(kWavHeaderBytes + data.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendUint32(out, 36 + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    appendUint32(out, 16);
    appendUint16(out, 1);
    appendUint16(out, channels);
    appendUint32(out, rate);
    appendUint32(out, rate * channels * 2);
    appendUint16(out, static_cast<uint16_t>(channels * 2));
    appendUint16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendUint32(out, dataBytes);
    for (const float sample : data) {
        const int16_t quantized = static_cast<int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * kPcm16Scale));
        appendUint16(out, static_cast<uint16_t>(quantized));
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

struct WavFormat {
    uint16_t format = 0;
    uint16_t channels = 1;
    uint32_t rate = 48000;
    uint16_t bits = 16;
};

uint16_t readUint16(const std::vector<uint8_t>& bytes, size_t offset) { return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8)); }

uint32_t readUint32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint32_t>(readUint16(bytes, offset)) | (static_cast<uint32_t>(readUint16(bytes, offset + 2)) << 16);
}

bool supportedFormatTag(uint16_t tag) { return tag == kWavFormatPcm || tag == kWavFormatFloat || tag == kWavFormatExtensible; }

bool decodeFrameSample(const std::vector<uint8_t>& bytes, size_t offset, const WavFormat& format, float& sample) {
    if (format.format == kWavFormatFloat && format.bits == 32) {
        std::memcpy(&sample, bytes.data() + offset, sizeof(float));
        if (!std::isfinite(sample)) sample = 0.0f;
        return true;
    }
    if (format.bits == 16) {
        sample = static_cast<float>(static_cast<int16_t>(readUint16(bytes, offset))) / 32768.0f;
        return true;
    }
    if (format.bits == 8) {
        sample = (static_cast<float>(bytes[offset]) - 128.0f) / 128.0f;
        return true;
    }
    return false;
}

bool decodeWavData(const std::vector<uint8_t>& bytes, size_t body, uint32_t size, const WavFormat& format, std::vector<float>& samples) {
    const size_t bytesPerSample = format.bits / 8;
    const size_t stride = bytesPerSample * format.channels;
    if (stride == 0) return false;
    const size_t end = std::min(bytes.size(), body + size);
    if (end > body) samples.reserve((end - body) / stride);
    for (size_t frameStart = body; frameStart + stride <= end; frameStart += stride) {
        float sum = 0.0f;
        for (uint16_t channel = 0; channel < format.channels; ++channel) {
            float channelSample = 0.0f;
            if (!decodeFrameSample(bytes, frameStart + channel * bytesPerSample, format, channelSample)) return false;
            sum += channelSample;
        }
        samples.push_back(sum / static_cast<float>(format.channels));
    }
    return true;
}
}

float AudioClip::rms(float timeSeconds, float windowSeconds) const {
    if (samples.empty()) return 0.0f;
    const int64_t half = static_cast<int64_t>(windowSeconds * 0.5f * static_cast<float>(sampleRate));
    const int64_t center = static_cast<int64_t>(timeSeconds * static_cast<float>(sampleRate));
    const int64_t first = std::max<int64_t>(0, center - half);
    const int64_t last = std::min<int64_t>(static_cast<int64_t>(samples.size()), center + half + 1);
    if (last <= first) return 0.0f;
    double sum = 0.0;
    for (int64_t index = first; index < last; ++index) sum += static_cast<double>(samples[index]) * samples[index];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(last - first)));
}

std::shared_ptr<AudioClip> AudioClip::sine(const std::string& name, float frequency, float seconds, float amplitude, uint32_t rate) {
    auto clip = std::make_shared<AudioClip>();
    clip->name = name;
    clip->sampleRate = rate;
    const size_t count = static_cast<size_t>(std::max(seconds, 0.0f) * static_cast<float>(rate));
    clip->samples.resize(count);
    for (size_t index = 0; index < count; ++index) clip->samples[index] = amplitude * std::sin(2.0f * kPi * frequency * static_cast<float>(index) / static_cast<float>(rate));
    return clip;
}

std::shared_ptr<AudioClip> AudioClip::noise(const std::string& name, float seconds, uint32_t seed, float amplitude, uint32_t rate) {
    auto clip = std::make_shared<AudioClip>();
    clip->name = name;
    clip->sampleRate = rate;
    Pcg32 rng(seed);
    const size_t count = static_cast<size_t>(std::max(seconds, 0.0f) * static_cast<float>(rate));
    clip->samples.resize(count);
    for (size_t index = 0; index < count; ++index) clip->samples[index] = amplitude * (rng.uniform() * 2.0f - 1.0f);
    return clip;
}

std::shared_ptr<AudioClip> AudioClip::loadWav(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return nullptr;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() < kWavHeaderBytes || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + kRiffFormOffset, "WAVE", 4) != 0) return nullptr;
    WavFormat format;
    bool sawFormat = false;
    size_t chunkStart = kFirstChunkOffset;
    while (chunkStart + kWavChunkHeaderBytes <= bytes.size()) {
        const uint32_t chunkSize = readUint32(bytes, chunkStart + 4);
        const size_t body = chunkStart + kWavChunkHeaderBytes;
        if (std::memcmp(bytes.data() + chunkStart, "fmt ", 4) == 0 && body + kWavFmtBodyBytes <= bytes.size()) {
            format.format = readUint16(bytes, body);
            format.channels = readUint16(bytes, body + 2);
            format.rate = readUint32(bytes, body + 4);
            format.bits = readUint16(bytes, body + 14);
            sawFormat = true;
        } else if (std::memcmp(bytes.data() + chunkStart, "data", 4) == 0) {
            if (!sawFormat || format.rate == 0 || !supportedFormatTag(format.format)) return nullptr;
            auto clip = std::make_shared<AudioClip>();
            if (!decodeWavData(bytes, body, chunkSize, format, clip->samples)) return nullptr;
            clip->sampleRate = format.rate;
            clip->name = path;
            return clip;
        }
        chunkStart = body + chunkSize + (chunkSize & 1u);
    }
    return nullptr;
}

bool AudioClip::saveWav(const std::string& path) const { return writeWav(path, samples, sampleRate, 1); }

void NullAudioBackend::submit(const float* data, uint32_t frames) {
    m_frames += frames;
    const size_t sampleCount = static_cast<size_t>(frames) * 2;
    for (size_t index = 0; index < sampleCount; ++index) {
        m_peak = std::max(m_peak, std::fabs(data[index]));
        m_hash = (m_hash ^ static_cast<uint64_t>(std::llround(data[index] * kPcm16Scale) & 0xFFFF)) * kFnvPrime;
    }
}

void WavAudioBackend::close() { writeWav(m_path, m_data, m_rate, 2); }

void renderAudio(AudioMixer& mixer, AudioBackend& backend, float seconds, uint32_t blockFrames) {
    backend.open(mixer.sampleRate());
    const uint32_t safeBlock = std::max<uint32_t>(blockFrames, 1);
    std::vector<float> buffer(static_cast<size_t>(safeBlock) * 2);
    uint64_t remaining = static_cast<uint64_t>(std::max(seconds, 0.0f) * static_cast<float>(mixer.sampleRate()));
    while (remaining > 0) {
        const uint32_t frames = static_cast<uint32_t>(std::min<uint64_t>(safeBlock, remaining));
        mixer.update();
        mixer.render(buffer.data(), frames);
        backend.submit(buffer.data(), frames);
        remaining -= frames;
    }
    backend.close();
}
