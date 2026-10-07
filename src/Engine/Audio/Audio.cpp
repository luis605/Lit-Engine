module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

module Engine.Audio;

import Engine.Rng;

namespace {
constexpr float kPi = 3.14159265f;
constexpr uint32_t kRing = 4096;

float onePole(float cutoffHz, uint32_t rate) {
    const float fc = std::clamp(cutoffHz, 20.0f, static_cast<float>(rate) * 0.45f);
    return 1.0f - std::exp(-2.0f * kPi * fc / static_cast<float>(rate));
}

void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    put16(out, static_cast<uint16_t>(v & 0xFFFF));
    put16(out, static_cast<uint16_t>(v >> 16));
}

bool writeWav(const std::string& path, const std::vector<float>& data, uint32_t rate, uint16_t channels) {
    std::vector<uint8_t> out;
    const uint32_t bytes = static_cast<uint32_t>(data.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(out, 36 + bytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(out, 16);
    put16(out, 1);
    put16(out, channels);
    put32(out, rate);
    put32(out, rate * channels * 2);
    put16(out, static_cast<uint16_t>(channels * 2));
    put16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(out, bytes);
    for (const float f : data) put16(out, static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f))));
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}
}

float AudioClip::rms(float timeSeconds, float windowSeconds) const {
    if (samples.empty()) return 0.0f;
    const int64_t half = static_cast<int64_t>(windowSeconds * 0.5f * static_cast<float>(sampleRate));
    const int64_t center = static_cast<int64_t>(timeSeconds * static_cast<float>(sampleRate));
    const int64_t lo = std::max<int64_t>(0, center - half);
    const int64_t hi = std::min<int64_t>(static_cast<int64_t>(samples.size()), center + half + 1);
    if (hi <= lo) return 0.0f;
    double sum = 0.0;
    for (int64_t i = lo; i < hi; ++i) sum += static_cast<double>(samples[i]) * samples[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(hi - lo)));
}

std::shared_ptr<AudioClip> AudioClip::sine(const std::string& name, float frequency, float seconds, float amplitude, uint32_t rate) {
    auto c = std::make_shared<AudioClip>();
    c->name = name;
    c->sampleRate = rate;
    const size_t n = static_cast<size_t>(seconds * static_cast<float>(rate));
    c->samples.resize(n);
    for (size_t i = 0; i < n; ++i) c->samples[i] = amplitude * std::sin(2.0f * kPi * frequency * static_cast<float>(i) / static_cast<float>(rate));
    return c;
}

std::shared_ptr<AudioClip> AudioClip::noise(const std::string& name, float seconds, uint32_t seed, float amplitude, uint32_t rate) {
    auto c = std::make_shared<AudioClip>();
    c->name = name;
    c->sampleRate = rate;
    Pcg32 rng(seed);
    const size_t n = static_cast<size_t>(seconds * static_cast<float>(rate));
    c->samples.resize(n);
    for (size_t i = 0; i < n; ++i) c->samples[i] = amplitude * (rng.uniform() * 2.0f - 1.0f);
    return c;
}

std::shared_ptr<AudioClip> AudioClip::loadWav(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return nullptr;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return nullptr;
    const auto u16 = [&](size_t o) { return static_cast<uint16_t>(d[o] | (d[o + 1] << 8)); };
    const auto u32 = [&](size_t o) { return static_cast<uint32_t>(u16(o)) | (static_cast<uint32_t>(u16(o + 2)) << 16); };
    uint16_t format = 0;
    uint16_t channels = 1;
    uint32_t rate = 48000;
    uint16_t bits = 16;
    size_t pos = 12;
    auto c = std::make_shared<AudioClip>();
    while (pos + 8 <= d.size()) {
        const uint32_t size = u32(pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(d.data() + pos, "fmt ", 4) == 0 && body + 16 <= d.size()) {
            format = u16(body);
            channels = u16(body + 2);
            rate = u32(body + 4);
            bits = u16(body + 14);
        } else if (std::memcmp(d.data() + pos, "data", 4) == 0) {
            const size_t end = std::min(d.size(), body + size);
            const size_t stride = static_cast<size_t>(bits / 8) * std::max<uint16_t>(channels, 1);
            if (stride == 0) return nullptr;
            for (size_t o = body; o + stride <= end; o += stride) {
                float sum = 0.0f;
                for (uint16_t ch = 0; ch < channels; ++ch) {
                    const size_t so = o + static_cast<size_t>(ch) * (bits / 8);
                    if (format == 3 && bits == 32) {
                        float f;
                        std::memcpy(&f, d.data() + so, 4);
                        sum += f;
                    } else if (bits == 16) {
                        sum += static_cast<float>(static_cast<int16_t>(u16(so))) / 32768.0f;
                    } else if (bits == 8) {
                        sum += (static_cast<float>(d[so]) - 128.0f) / 128.0f;
                    } else {
                        return nullptr;
                    }
                }
                c->samples.push_back(sum / static_cast<float>(channels));
            }
            c->sampleRate = rate;
            c->name = path;
            return c;
        }
        pos = body + size + (size & 1u);
    }
    return nullptr;
}

bool AudioClip::saveWav(const std::string& path) const { return writeWav(path, samples, sampleRate, 1); }

void NullAudioBackend::submit(const float* data, uint32_t frames) {
    m_frames += frames;
    for (uint32_t i = 0; i < frames * 2; ++i) {
        m_peak = std::max(m_peak, std::fabs(data[i]));
        m_hash = (m_hash ^ static_cast<uint64_t>(std::llround(data[i] * 32767.0f) & 0xFFFF)) * 0x100000001b3ULL;
    }
}

void WavAudioBackend::close() { writeWav(m_path, m_data, m_rate, 2); }

void renderAudio(AudioMixer& mixer, AudioBackend& backend, float seconds, uint32_t block) {
    backend.open(mixer.sampleRate());
    std::vector<float> buf(static_cast<size_t>(block) * 2);
    uint64_t remaining = static_cast<uint64_t>(seconds * static_cast<float>(mixer.sampleRate()));
    while (remaining > 0) {
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(block, remaining));
        mixer.update();
        mixer.render(buf.data(), n);
        backend.submit(buf.data(), n);
        remaining -= n;
    }
    backend.close();
}

AudioMixer::AudioMixer(uint32_t sampleRate) : m_rate(sampleRate) {
    m_buses.push_back({"master"});
    const uint32_t combBase[4] = {1557, 1617, 1491, 1422};
    const uint32_t apBase[2] = {225, 556};
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < 4; ++i) m_reverb.comb[c][i].assign((combBase[i] + (c ? 23 : 0)) * m_rate / 44100 + 1, 0.0f);
        for (int i = 0; i < 2; ++i) m_reverb.allpass[c][i].assign((apBase[i] + (c ? 23 : 0)) * m_rate / 44100 + 1, 0.0f);
    }
    ReverbEstimate e;
    e.rt60 = 0.5f;
    e.wet = 0.3f;
    setReverb(e);
}

int AudioMixer::addBus(const std::string& name, const std::string& parent) {
    const int existing = busIndex(name);
    if (existing >= 0) return existing;
    const int p = busIndex(parent);
    Bus b;
    b.name = name;
    b.parent = p < 0 ? 0 : p;
    m_buses.push_back(std::move(b));
    return static_cast<int>(m_buses.size() - 1);
}

int AudioMixer::busIndex(const std::string& name) const {
    for (size_t i = 0; i < m_buses.size(); ++i) {
        if (m_buses[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void AudioMixer::setBusGain(const std::string& name, float linear) {
    const int i = busIndex(name);
    if (i >= 0) m_buses[i].gain = std::max(0.0f, linear);
}

void AudioMixer::setBusGainDb(const std::string& name, float db) { setBusGain(name, std::pow(10.0f, db / 20.0f)); }

void AudioMixer::setBusMute(const std::string& name, bool mute) {
    const int i = busIndex(name);
    if (i >= 0) m_buses[i].mute = mute;
}

float AudioMixer::busGain(const std::string& name) const {
    const int i = busIndex(name);
    return i >= 0 ? m_buses[i].gain : 0.0f;
}

float AudioMixer::busLevel(const std::string& name) const {
    const int i = busIndex(name);
    return i >= 0 ? m_buses[i].level : 0.0f;
}

float AudioMixer::busDuck(const std::string& name) const {
    const int i = busIndex(name);
    return i >= 0 ? m_buses[i].duck : 1.0f;
}

void AudioMixer::bindBusParam(const std::string& bus, const std::string& param, float zero, float one) {
    const int i = busIndex(bus);
    if (i >= 0) m_bindings.push_back({i, param, zero, one});
}

void AudioMixer::addDuck(const std::string& trigger, const std::string& target, float amountDb, float threshold, float attack, float release) {
    const int t = busIndex(trigger);
    const int g = busIndex(target);
    if (t < 0 || g < 0) return;
    m_ducks.push_back({t, g, std::pow(10.0f, amountDb / 20.0f), threshold, attack, release});
}

void AudioMixer::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    m_listenerPos = position;
    const float fl = glm::length(forward);
    m_forward = fl > 1.0e-8f ? forward / fl : glm::vec3(0.0f, 0.0f, 1.0f);
    m_up = up;
    glm::vec3 r = glm::cross(m_forward, m_up);
    const float rl = glm::length(r);
    m_right = rl > 1.0e-8f ? r / rl : glm::vec3(1.0f, 0.0f, 0.0f);
}

void AudioMixer::setReverb(const ReverbEstimate& e) {
    m_reverbEstimate = e;
    const float rt = std::clamp(e.rt60, 0.1f, 6.0f);
    for (int i = 0; i < 4; ++i) {
        const float delay = static_cast<float>(m_reverb.comb[0][i].size());
        m_reverb.feedback[i] = std::min(0.97f, std::pow(10.0f, -3.0f * delay / (rt * static_cast<float>(m_rate))));
    }
}

VoiceId AudioMixer::play(const VoiceParams& params) {
    if (!params.clip || params.clip->samples.empty()) return 0;
    Voice v;
    v.id = m_nextVoice++;
    v.params = params;
    const int b = busIndex(params.bus);
    v.bus = b < 0 ? 0 : b;
    v.position = static_cast<double>(params.startTime) * params.clip->sampleRate;
    v.active = true;
    v.history.assign(kRing, 0.0f);
    m_voices.push_back(std::move(v));
    return m_voices.back().id;
}

void AudioMixer::stop(VoiceId id) {
    for (Voice& v : m_voices) {
        if (v.id == id) v.active = false;
    }
}

bool AudioMixer::isPlaying(VoiceId id) const {
    for (const Voice& v : m_voices) {
        if (v.id == id) return v.active;
    }
    return false;
}

float AudioMixer::voiceTime(VoiceId id) const {
    for (const Voice& v : m_voices) {
        if (v.id == id) return static_cast<float>(v.position / v.params.clip->sampleRate);
    }
    return 0.0f;
}

void AudioMixer::setVoicePosition(VoiceId id, const glm::vec3& p) {
    for (Voice& v : m_voices) {
        if (v.id == id) v.params.position = p;
    }
}

void AudioMixer::setVoiceGain(VoiceId id, float g) {
    for (Voice& v : m_voices) {
        if (v.id == id) v.params.gain = g;
    }
}

void AudioMixer::setVoicePath(VoiceId id, const AudioPath& path) {
    for (Voice& v : m_voices) {
        if (v.id == id) {
            v.path = path;
            v.hasPath = true;
        }
    }
}

void AudioMixer::setVoiceBus(VoiceId id, const std::string& bus) {
    const int b = busIndex(bus);
    for (Voice& v : m_voices) {
        if (v.id == id && b >= 0) v.bus = b;
    }
}

uint32_t AudioMixer::voiceCount() const {
    uint32_t n = 0;
    for (const Voice& v : m_voices) n += v.active ? 1u : 0u;
    return n;
}

void AudioMixer::update(const ParamBus* params) {
    if (!params) return;
    for (const Binding& b : m_bindings) {
        if (!params->has(b.param)) continue;
        const float x = std::clamp(params->get(b.param), 0.0f, 1.0f);
        m_buses[b.bus].gain = b.gainAtZero + (b.gainAtOne - b.gainAtZero) * x;
    }
}

void AudioMixer::mixVoice(Voice& v, uint32_t frames, std::vector<float>& reverbIn) {
    const AudioClip& clip = *v.params.clip;
    Bus& bus = m_buses[v.bus];
    float gainL = 1.0f;
    float gainR = 1.0f;
    float send = 0.0f;
    float lowpassHz = 20000.0f;
    float x = 0.0f;
    float backness = 0.0f;
    float level = v.params.gain;
    if (v.params.spatial) {
        glm::vec3 dir;
        float atten;
        if (v.hasPath) {
            dir = v.path.arrivalDirection;
            atten = v.path.attenuation;
            lowpassHz = v.path.lowpassHz;
            send = v.path.reverbSend * v.params.reverbSend;
        } else {
            const glm::vec3 d = v.params.position - m_listenerPos;
            const float dist = glm::length(d);
            dir = dist > 1.0e-6f ? d / dist : m_forward;
            const float minD = std::max(v.params.minDistance, 0.01f);
            const float fade = std::clamp((v.params.maxDistance - dist) / (v.params.maxDistance * 0.2f), 0.0f, 1.0f);
            atten = minD / std::max(dist, minD) * fade;
            send = m_reverbEstimate.wet * std::min(1.0f, atten * 4.0f) * v.params.reverbSend;
        }
        x = std::clamp(glm::dot(dir, m_right), -1.0f, 1.0f);
        const float z = glm::dot(dir, m_forward);
        backness = std::max(0.0f, -z);
        const float theta = (x + 1.0f) * kPi * 0.25f;
        gainL = std::cos(theta);
        gainR = std::sin(theta);
        level *= atten;
        lowpassHz = std::min(lowpassHz, 20000.0f + (6000.0f - 20000.0f) * backness);
    }
    gainL *= level;
    gainR *= level;
    send *= level;
    if (v.lastGainL < 0.0f) {
        v.lastGainL = gainL;
        v.lastGainR = gainR;
        v.lastSend = send;
    }
    const float a = onePole(lowpassHz, m_rate);
    const float shadowA = onePole(20000.0f + (3000.0f - 20000.0f) * std::fabs(x), m_rate);
    const float itdSamples = v.params.spatial ? std::fabs(x) * 0.00066f * static_cast<float>(m_rate) : 0.0f;
    const bool rightNear = x >= 0.0f;
    const double step = static_cast<double>(v.params.pitch) * clip.sampleRate / m_rate;
    const size_t count = clip.samples.size();
    struct Tap {
        uint32_t delay;
        float gl;
        float gr;
    };
    Tap taps[4];
    int tapCount = 0;
    if (v.hasPath && v.params.spatial) {
        for (const AudioReflection& r : v.path.reflections) {
            if (tapCount >= 4) break;
            const uint32_t ds = static_cast<uint32_t>(r.delay * static_cast<float>(m_rate));
            if (ds == 0 || ds >= kRing - 2) continue;
            const float rx = std::clamp(glm::dot(r.direction, m_right), -1.0f, 1.0f);
            const float th = (rx + 1.0f) * kPi * 0.25f;
            const float g = r.gain * v.params.gain;
            taps[tapCount++] = {ds, std::cos(th) * g, std::sin(th) * g};
        }
    }
    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(frames);
        const float gl = v.lastGainL + (gainL - v.lastGainL) * t;
        const float gr = v.lastGainR + (gainR - v.lastGainR) * t;
        const float sd = v.lastSend + (send - v.lastSend) * t;
        float s = 0.0f;
        if (v.active) {
            size_t i0 = static_cast<size_t>(v.position);
            if (i0 >= count) {
                if (v.params.loop) {
                    v.position = std::fmod(v.position, static_cast<double>(count));
                    i0 = static_cast<size_t>(v.position);
                } else {
                    v.active = false;
                }
            }
            if (v.active) {
                const float f = static_cast<float>(v.position - static_cast<double>(i0));
                const size_t i1 = i0 + 1 < count ? i0 + 1 : (v.params.loop ? 0 : i0);
                s = clip.samples[i0] * (1.0f - f) + clip.samples[i1] * f;
                v.position += step;
            }
        }
        v.lpState += a * (s - v.lpState);
        const float m = v.lpState;
        v.history[v.historyPos & (kRing - 1)] = m;
        float dl;
        float dr;
        if (itdSamples > 0.0f) {
            const uint32_t di = static_cast<uint32_t>(itdSamples);
            const float df = itdSamples - static_cast<float>(di);
            const float s0 = v.history[(v.historyPos - di) & (kRing - 1)];
            const float s1 = v.history[(v.historyPos - di - 1) & (kRing - 1)];
            const float far = s0 * (1.0f - df) + s1 * df;
            if (rightNear) {
                v.lpNearL += shadowA * (far - v.lpNearL);
                dl = v.lpNearL;
                dr = m;
            } else {
                v.lpNearR += shadowA * (far - v.lpNearR);
                dr = v.lpNearR;
                dl = m;
            }
        } else {
            dl = m;
            dr = m;
        }
        float outL = dl * gl;
        float outR = dr * gr;
        for (int k = 0; k < tapCount; ++k) {
            const float ts = v.history[(v.historyPos - taps[k].delay) & (kRing - 1)];
            outL += ts * taps[k].gl;
            outR += ts * taps[k].gr;
        }
        ++v.historyPos;
        bus.left[i] += outL;
        bus.right[i] += outR;
        reverbIn[i] += m * sd;
    }
    v.lastGainL = gainL;
    v.lastGainR = gainR;
    v.lastSend = send;
}

void AudioMixer::processReverb(const std::vector<float>& in, std::vector<float>& outL, std::vector<float>& outR, uint32_t frames) {
    for (int c = 0; c < 2; ++c) {
        std::vector<float>& out = c ? outR : outL;
        for (uint32_t i = 0; i < frames; ++i) {
            const float x = in[i] * 0.15f;
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k) {
                std::vector<float>& line = m_reverb.comb[c][k];
                uint32_t& pos = m_reverb.combPos[c][k];
                const float y = line[pos];
                m_reverb.combLp[c][k] = y * 0.8f + m_reverb.combLp[c][k] * 0.2f;
                line[pos] = x + m_reverb.combLp[c][k] * m_reverb.feedback[k];
                pos = (pos + 1) % static_cast<uint32_t>(line.size());
                acc += y;
            }
            for (int k = 0; k < 2; ++k) {
                std::vector<float>& line = m_reverb.allpass[c][k];
                uint32_t& pos = m_reverb.allpassPos[c][k];
                const float buf = line[pos];
                const float y = -acc + buf;
                line[pos] = acc + buf * 0.5f;
                pos = (pos + 1) % static_cast<uint32_t>(line.size());
                acc = y;
            }
            out[i] += acc;
        }
    }
}

void AudioMixer::renderBlock(uint32_t frames, float* out) {
    for (Bus& b : m_buses) {
        b.left.assign(frames, 0.0f);
        b.right.assign(frames, 0.0f);
    }
    std::vector<float> reverbIn(frames, 0.0f);
    for (Voice& v : m_voices) {
        if (v.active) mixVoice(v, frames, reverbIn);
    }
    std::vector<float> rl(frames, 0.0f);
    std::vector<float> rr(frames, 0.0f);
    processReverb(reverbIn, rl, rr, frames);
    for (uint32_t i = 0; i < frames; ++i) {
        m_buses[0].left[i] += rl[i] * m_reverbReturn;
        m_buses[0].right[i] += rr[i] * m_reverbReturn;
    }
    for (Bus& b : m_buses) b.duckGoal = 1.0f;
    for (size_t i = m_buses.size(); i-- > 0;) {
        Bus& b = m_buses[i];
        const float g = (b.mute ? 0.0f : b.gain) * b.duck;
        double sum = 0.0;
        for (uint32_t k = 0; k < frames; ++k) {
            b.left[k] *= g;
            b.right[k] *= g;
            sum += 0.5 * (static_cast<double>(b.left[k]) * b.left[k] + static_cast<double>(b.right[k]) * b.right[k]);
        }
        b.level = frames ? static_cast<float>(std::sqrt(sum / frames)) : 0.0f;
        if (i > 0) {
            Bus& p = m_buses[b.parent];
            for (uint32_t k = 0; k < frames; ++k) {
                p.left[k] += b.left[k];
                p.right[k] += b.right[k];
            }
        }
    }
    for (const Duck& d : m_ducks) {
        if (m_buses[d.trigger].level > d.threshold && d.amount < m_buses[d.target].duckGoal) {
            m_buses[d.target].duckGoal = d.amount;
            m_buses[d.target].duckAttack = d.attack;
            m_buses[d.target].duckRelease = d.release;
        }
    }
    const float dt = static_cast<float>(frames) / static_cast<float>(m_rate);
    for (Bus& b : m_buses) {
        const float tau = b.duckGoal < b.duck ? b.duckAttack : b.duckRelease;
        const float k = tau > 1.0e-6f ? 1.0f - std::exp(-dt / tau) : 1.0f;
        b.duck += (b.duckGoal - b.duck) * k;
    }
    float peak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        out[i * 2] = m_buses[0].left[i];
        out[i * 2 + 1] = m_buses[0].right[i];
        peak = std::max({peak, std::fabs(out[i * 2]), std::fabs(out[i * 2 + 1])});
    }
    m_stats.peak = peak;
    m_stats.framesRendered += frames;
    std::erase_if(m_voices, [](const Voice& v) { return !v.active; });
    m_stats.activeVoices = static_cast<uint32_t>(m_voices.size());
}

void AudioMixer::render(float* out, uint32_t frames) {
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t n = std::min<uint32_t>(256, frames - done);
        renderBlock(n, out + static_cast<size_t>(done) * 2);
        done += n;
    }
}

float VoiceScheduler::clipTime() const { return m_active ? m_mixer.voiceTime(m_voice) : m_lastTime; }

float VoiceScheduler::energy() const {
    if (!m_active || !m_line.clip) return 0.0f;
    return std::clamp(m_line.clip->rms(clipTime(), 0.03f) * 3.0f, 0.0f, 1.0f);
}

void VoiceScheduler::log(LineEvent type, const std::string& id, float t) { m_events.push_back({type, id, t, m_update}); }

void VoiceScheduler::start(VoiceLine line, float offset, bool resumed) {
    VoiceParams p;
    p.clip = line.clip;
    p.bus = line.bus;
    p.gain = line.gain;
    p.spatial = line.positional;
    p.position = line.position;
    p.maxDistance = std::max(line.maxDistance * 1.5f, 5.0f);
    p.startTime = offset;
    m_voice = m_mixer.play(p);
    m_line = std::move(line);
    m_active = m_voice != 0;
    m_lastTime = offset;
    log(resumed ? LineEvent::Resumed : LineEvent::Started, m_line.id, offset);
    if (!m_active) log(LineEvent::Dropped, m_line.id, offset);
}

void VoiceScheduler::interruptCurrent(bool) {
    const float t = clipTime();
    m_mixer.stop(m_voice);
    m_active = false;
    m_lastTime = t;
    log(LineEvent::Interrupted, m_line.id, t);
    if (m_line.resumable && m_line.clip && t < m_line.clip->duration() - 0.05f) {
        m_pending.push_back({m_line, std::max(0.0f, t - m_line.rewindSeconds), m_activeSeq, true});
    } else {
        log(LineEvent::Dropped, m_line.id, t);
    }
}

bool VoiceScheduler::eligible(const Pending& p) const {
    if (!p.line.positional) return true;
    const float d = glm::length(p.line.position - m_mixer.listenerPosition());
    return d <= (p.resumed ? p.line.resumeDistance : p.line.maxDistance);
}

void VoiceScheduler::startNext() {
    int best = -1;
    for (size_t i = 0; i < m_pending.size(); ++i) {
        if (!eligible(m_pending[i])) continue;
        if (best < 0 || m_pending[i].line.priority > m_pending[best].line.priority || (m_pending[i].line.priority == m_pending[best].line.priority && m_pending[i].seq < m_pending[best].seq)) best = static_cast<int>(i);
    }
    if (best < 0) return;
    Pending p = std::move(m_pending[best]);
    m_pending.erase(m_pending.begin() + best);
    m_activeSeq = p.seq;
    start(std::move(p.line), p.offset, p.resumed);
}

void VoiceScheduler::enqueue(VoiceLine line) {
    const uint64_t seq = m_seq++;
    if (m_active && line.priority > m_line.priority && m_line.interruptible) interruptCurrent(false);
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
    std::erase_if(m_pending, [&](const Pending& p) { return p.line.id == id; });
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
            interruptCurrent(true);
        } else if (m_line.interruptible) {
            for (const Pending& p : m_pending) {
                if (p.line.priority > m_line.priority && eligible(p)) {
                    interruptCurrent(false);
                    break;
                }
            }
        }
    }
    if (!m_active) startNext();
}
