module;

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Engine.Face;

import Engine.glm;
import Engine.Json;
import Engine.Rng;

export inline constexpr size_t kBlendshapeCount = 52;

export extern const std::array<const char*, kBlendshapeCount> kArkitBlendshapes;

export [[nodiscard]] int blendshapeIndex(std::string_view name);

export using BlendshapeWeights = std::array<float, kBlendshapeCount>;

export struct FloatKey {
    float time = 0.0f;
    float value = 0.0f;
};

export struct BlendshapeCurve {
    int channel = 0;
    std::vector<FloatKey> keys;
};

export struct BlendshapeClip {
    std::string name;
    float duration = 0.0f;
    bool loop = false;
    std::vector<BlendshapeCurve> curves;

    bool loadJson(const Json& doc, std::string* error = nullptr);
    bool loadText(std::string_view text, std::string* error = nullptr);
    [[nodiscard]] Json toJson() const;
    void sample(float time, BlendshapeWeights& out) const;
};

export class BlendshapeLibrary {
  public:
    uint32_t add(BlendshapeClip clip);
    [[nodiscard]] const BlendshapeClip* get(uint32_t id) const { return id < m_clips.size() ? &m_clips[id] : nullptr; }
    [[nodiscard]] int find(const std::string& name) const;

  private:
    std::vector<BlendshapeClip> m_clips;
};

export class BlendshapePlayer {
  public:
    void play(uint32_t clip, float speed = 1.0f);
    void stop() { m_playing = false; }
    void update(float dt, const BlendshapeLibrary& library, BlendshapeWeights& out);
    [[nodiscard]] bool playing() const { return m_playing; }
    [[nodiscard]] float time() const { return m_time; }

  private:
    uint32_t m_clip = 0;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_playing = false;
};

export void blendshapeMax(BlendshapeWeights& base, const BlendshapeWeights& layer);
export void blendshapeAdd(BlendshapeWeights& base, const BlendshapeWeights& layer, float weight = 1.0f);
export [[nodiscard]] BlendshapeWeights blendshapeZero();

export struct EyeSettings {
    float maxYawDegrees = 35.0f;
    float maxPitchDegrees = 25.0f;
    float saccadeIntervalMin = 0.4f;
    float saccadeIntervalMax = 2.2f;
    float saccadeAmplitudeDegrees = 1.5f;
    float saccadeSpeedDegrees = 600.0f;
    float gazeSpeedDegrees = 700.0f;
    float blinkIntervalMin = 2.0f;
    float blinkIntervalMax = 6.0f;
    float blinkDuration = 0.18f;
    float doubleBlinkChance = 0.1f;
    float shiftBlinkDegrees = 25.0f;
    float shiftBlinkChance = 0.6f;
};

export class EyeSystem {
  public:
    explicit EyeSystem(const EyeSettings& settings = {}) : m_settings(settings) {}

    void setSettings(const EyeSettings& settings) { m_settings = settings; }
    void setTarget(const glm::vec3& worldPoint) { m_target = worldPoint; m_hasTarget = true; }
    void clearTarget() { m_hasTarget = false; }
    void setHead(const glm::vec3& eyeMidWorld, const glm::quat& headRotation) {
        m_eye = eyeMidWorld;
        m_head = headRotation;
    }
    void requestBlink() { m_blinkTime = 0.0f; m_blinking = true; }
    void update(float dt, Pcg32& rng);

    [[nodiscard]] float yawDegrees() const { return m_yaw + m_saccadeYaw; }
    [[nodiscard]] float pitchDegrees() const { return m_pitch + m_saccadePitch; }
    [[nodiscard]] float blink() const { return m_blinkValue; }
    [[nodiscard]] uint32_t blinkCount() const { return m_blinkCount; }
    [[nodiscard]] uint32_t saccadeCount() const { return m_saccadeCount; }
    void applyTo(BlendshapeWeights& weights) const;

  private:
    EyeSettings m_settings;
    glm::vec3 m_target{0.0f, 0.0f, 10.0f};
    glm::vec3 m_eye{0.0f};
    glm::quat m_head{1.0f, 0.0f, 0.0f, 0.0f};
    bool m_hasTarget = false;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_saccadeYaw = 0.0f;
    float m_saccadePitch = 0.0f;
    float m_saccadeGoalYaw = 0.0f;
    float m_saccadeGoalPitch = 0.0f;
    float m_saccadeTimer = -1.0f;
    float m_blinkTimer = -1.0f;
    float m_blinkTime = 0.0f;
    bool m_blinking = false;
    float m_blinkValue = 0.0f;
    uint32_t m_blinkCount = 0;
    uint32_t m_saccadeCount = 0;
};
