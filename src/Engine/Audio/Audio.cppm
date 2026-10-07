module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

export module Engine.Audio;

import Engine.glm;
import Engine.Params;
import Engine.AudioTrace;

export struct AudioClip {
    std::string name;
    uint32_t sampleRate = 48000;
    std::vector<float> samples;

    [[nodiscard]] float duration() const { return sampleRate ? static_cast<float>(samples.size()) / static_cast<float>(sampleRate) : 0.0f; }
    [[nodiscard]] float rms(float timeSeconds, float windowSeconds) const;
    static std::shared_ptr<AudioClip> sine(const std::string& name, float frequency, float seconds, float amplitude = 0.5f, uint32_t rate = 48000);
    static std::shared_ptr<AudioClip> noise(const std::string& name, float seconds, uint32_t seed, float amplitude = 0.5f, uint32_t rate = 48000);
    static std::shared_ptr<AudioClip> loadWav(const std::string& path);
    bool saveWav(const std::string& path) const;
};

export using VoiceId = uint32_t;

export struct VoiceParams {
    std::shared_ptr<const AudioClip> clip;
    std::string bus = "master";
    bool loop = false;
    float gain = 1.0f;
    float pitch = 1.0f;
    bool spatial = true;
    glm::vec3 position{0.0f};
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    float reverbSend = 1.0f;
    float startTime = 0.0f;
};

export struct AudioMixerStats {
    uint64_t framesRendered = 0;
    uint32_t activeVoices = 0;
    float peak = 0.0f;
};

export class AudioMixer {
  public:
    explicit AudioMixer(uint32_t sampleRate = 48000);

    int addBus(const std::string& name, const std::string& parent = "master");
    [[nodiscard]] int busIndex(const std::string& name) const;
    void setBusGain(const std::string& name, float linear);
    void setBusGainDb(const std::string& name, float db);
    void setBusMute(const std::string& name, bool mute);
    [[nodiscard]] float busGain(const std::string& name) const;
    [[nodiscard]] float busLevel(const std::string& name) const;
    [[nodiscard]] float busDuck(const std::string& name) const;
    void bindBusParam(const std::string& bus, const std::string& param, float gainAtZero = 0.0f, float gainAtOne = 1.0f);
    void addDuck(const std::string& trigger, const std::string& target, float amountDb, float threshold = 0.02f, float attackSeconds = 0.05f, float releaseSeconds = 0.4f);

    void setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
    void setReverb(const ReverbEstimate& estimate);
    void setReverbReturn(float gain) { m_reverbReturn = gain; }

    VoiceId play(const VoiceParams& params);
    void stop(VoiceId id);
    [[nodiscard]] bool isPlaying(VoiceId id) const;
    [[nodiscard]] float voiceTime(VoiceId id) const;
    void setVoicePosition(VoiceId id, const glm::vec3& position);
    void setVoiceGain(VoiceId id, float gain);
    void setVoicePath(VoiceId id, const AudioPath& path);
    void setVoiceBus(VoiceId id, const std::string& bus);
    [[nodiscard]] const glm::vec3& listenerPosition() const { return m_listenerPos; }

    void update(const ParamBus* params = nullptr);
    void render(float* interleavedStereo, uint32_t frames);
    [[nodiscard]] uint32_t sampleRate() const { return m_rate; }
    [[nodiscard]] const AudioMixerStats& stats() const { return m_stats; }
    [[nodiscard]] uint32_t voiceCount() const;

  private:
    struct Bus {
        std::string name;
        int parent = -1;
        float gain = 1.0f;
        bool mute = false;
        float duck = 1.0f;
        float duckGoal = 1.0f;
        float duckAttack = 0.05f;
        float duckRelease = 0.4f;
        float level = 0.0f;
        std::vector<float> left;
        std::vector<float> right;
    };
    struct Binding {
        int bus = 0;
        std::string param;
        float gainAtZero = 0.0f;
        float gainAtOne = 1.0f;
    };
    struct Duck {
        int trigger = 0;
        int target = 0;
        float amount = 0.25f;
        float threshold = 0.02f;
        float attack = 0.05f;
        float release = 0.4f;
    };
    struct Voice {
        VoiceId id = 0;
        VoiceParams params;
        int bus = 0;
        double position = 0.0;
        bool active = false;
        bool hasPath = false;
        AudioPath path;
        float lpState = 0.0f;
        float lpNearL = 0.0f;
        float lpNearR = 0.0f;
        std::vector<float> history;
        uint32_t historyPos = 0;
        float lastGainL = -1.0f;
        float lastGainR = 0.0f;
        float lastSend = 0.0f;
    };
    struct Reverb {
        std::vector<float> comb[2][4];
        uint32_t combPos[2][4]{};
        float combLp[2][4]{};
        std::vector<float> allpass[2][2];
        uint32_t allpassPos[2][2]{};
        float feedback[4]{};
    };

    void renderBlock(uint32_t frames, float* out);
    void mixVoice(Voice& v, uint32_t frames, std::vector<float>& reverbIn);
    void processReverb(const std::vector<float>& in, std::vector<float>& outL, std::vector<float>& outR, uint32_t frames);

    uint32_t m_rate;
    std::vector<Bus> m_buses;
    std::vector<Binding> m_bindings;
    std::vector<Duck> m_ducks;
    std::vector<Voice> m_voices;
    VoiceId m_nextVoice = 1;
    glm::vec3 m_listenerPos{0.0f};
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_right{-1.0f, 0.0f, 0.0f};
    Reverb m_reverb;
    ReverbEstimate m_reverbEstimate;
    float m_reverbReturn = 1.0f;
    AudioMixerStats m_stats;
};

export class AudioBackend {
  public:
    virtual ~AudioBackend() = default;
    virtual bool open(uint32_t sampleRate) = 0;
    virtual void submit(const float* interleavedStereo, uint32_t frames) = 0;
    virtual void close() {}
};

export class NullAudioBackend : public AudioBackend {
  public:
    bool open(uint32_t sampleRate) override {
        m_rate = sampleRate;
        return true;
    }
    void submit(const float* data, uint32_t frames) override;
    [[nodiscard]] uint64_t frames() const { return m_frames; }
    [[nodiscard]] float peak() const { return m_peak; }
    [[nodiscard]] uint64_t hash() const { return m_hash; }

  private:
    uint32_t m_rate = 0;
    uint64_t m_frames = 0;
    float m_peak = 0.0f;
    uint64_t m_hash = 0xcbf29ce484222325ULL;
};

export class WavAudioBackend : public AudioBackend {
  public:
    explicit WavAudioBackend(std::string path) : m_path(std::move(path)) {}
    bool open(uint32_t sampleRate) override {
        m_rate = sampleRate;
        m_data.clear();
        return true;
    }
    void submit(const float* data, uint32_t frames) override { m_data.insert(m_data.end(), data, data + static_cast<size_t>(frames) * 2); }
    void close() override;
    [[nodiscard]] const std::vector<float>& data() const { return m_data; }

  private:
    std::string m_path;
    uint32_t m_rate = 48000;
    std::vector<float> m_data;
};

export void renderAudio(AudioMixer& mixer, AudioBackend& backend, float seconds, uint32_t block = 512);

export enum class LineEvent { Started, Interrupted, Resumed, Finished, Dropped };

export struct LineEventRecord {
    LineEvent type = LineEvent::Started;
    std::string id;
    float clipTime = 0.0f;
    uint64_t update = 0;
};

export struct VoiceLine {
    std::string id;
    std::shared_ptr<const AudioClip> clip;
    int priority = 0;
    bool interruptible = true;
    bool resumable = true;
    bool positional = true;
    glm::vec3 position{0.0f};
    float maxDistance = 15.0f;
    float resumeDistance = 12.0f;
    float rewindSeconds = 0.25f;
    std::string bus = "master";
    float gain = 1.0f;
};

export class VoiceScheduler {
  public:
    explicit VoiceScheduler(AudioMixer& mixer) : m_mixer(mixer) {}

    void enqueue(VoiceLine line);
    void stop(const std::string& id);
    void stopAll();
    void setSpeakerPosition(const glm::vec3& position);
    void update(float dt);
    [[nodiscard]] const VoiceLine* current() const { return m_active ? &m_line : nullptr; }
    [[nodiscard]] size_t pending() const { return m_pending.size(); }
    [[nodiscard]] const std::vector<LineEventRecord>& events() const { return m_events; }
    [[nodiscard]] float clipTime() const;
    [[nodiscard]] float energy() const;
    [[nodiscard]] VoiceId voice() const { return m_voice; }

  private:
    struct Pending {
        VoiceLine line;
        float offset = 0.0f;
        uint64_t seq = 0;
        bool resumed = false;
    };
    void start(VoiceLine line, float offset, bool resumed);
    void interruptCurrent(bool dueToDistance);
    void startNext();
    bool eligible(const Pending& p) const;
    void log(LineEvent type, const std::string& id, float t);

    AudioMixer& m_mixer;
    bool m_active = false;
    VoiceLine m_line;
    VoiceId m_voice = 0;
    std::vector<Pending> m_pending;
    std::vector<LineEventRecord> m_events;
    uint64_t m_seq = 0;
    uint64_t m_activeSeq = 0;
    uint64_t m_update = 0;
    float m_lastTime = 0.0f;
};
