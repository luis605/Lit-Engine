module;

#include <bitset>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module Engine.Replay;

import Engine.glm;

export inline constexpr int kInputKeyCount = 512;
export inline constexpr int kInputMouseButtons = 8;

export struct InputState {
    std::bitset<kInputKeyCount> keys;
    std::bitset<kInputMouseButtons> buttons;
    glm::vec2 mouse{0.0f};
    glm::vec2 mouseDelta{0.0f};
    glm::vec2 scroll{0.0f};

    bool operator==(const InputState&) const = default;
};

export struct SimInput {
    InputState current;
    InputState previous;

    [[nodiscard]] bool held(int key) const { return key >= 0 && key < kInputKeyCount && current.keys[static_cast<size_t>(key)]; }
    [[nodiscard]] bool pressed(int key) const { return held(key) && !previous.keys[static_cast<size_t>(key)]; }
    [[nodiscard]] bool released(int key) const { return key >= 0 && key < kInputKeyCount && !current.keys[static_cast<size_t>(key)] && previous.keys[static_cast<size_t>(key)]; }
    [[nodiscard]] bool buttonHeld(int button) const { return button >= 0 && button < kInputMouseButtons && current.buttons[static_cast<size_t>(button)]; }
    [[nodiscard]] bool buttonPressed(int button) const { return buttonHeld(button) && !previous.buttons[static_cast<size_t>(button)]; }
    [[nodiscard]] bool buttonReleased(int button) const { return button >= 0 && button < kInputMouseButtons && !current.buttons[static_cast<size_t>(button)] && previous.buttons[static_cast<size_t>(button)]; }
    void advance(const InputState& next) {
        previous = current;
        current = next;
    }
};

export struct ReplayTick {
    float dt = 0.0f;
    InputState input;
};

export struct ReplayCheckpoint {
    uint64_t tick = 0;
    uint64_t hash = 0;
    bool operator==(const ReplayCheckpoint&) const = default;
};

export struct ReplayData {
    uint64_t seed = 0;
    std::vector<ReplayTick> ticks;
    uint64_t hashInterval = 0;
    std::vector<ReplayCheckpoint> checkpoints;
};

export struct ReplayMismatch {
    uint64_t tick = 0;
    uint64_t expected = 0;
    uint64_t actual = 0;
};

export class ReplayVerifier {
  public:
    void begin(const ReplayData& data);
    [[nodiscard]] bool due(uint64_t tick) const;
    void check(uint64_t tick, uint64_t hash);
    [[nodiscard]] bool ok() const { return m_mismatches.empty(); }
    [[nodiscard]] size_t checked() const { return m_checked; }
    [[nodiscard]] size_t expectedCount() const { return m_expected.size(); }
    [[nodiscard]] const std::vector<ReplayMismatch>& mismatches() const { return m_mismatches; }

  private:
    std::vector<ReplayCheckpoint> m_expected;
    std::vector<ReplayMismatch> m_mismatches;
    size_t m_checked = 0;
};

export class InputRecorder {
  public:
    void begin(uint64_t seed);
    void record(float dt, const InputState& input);
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] const ReplayData& data() const { return m_data; }
    bool save(const std::string& path) const;
    void stop() { m_active = false; }
    void setHashInterval(uint64_t interval) { m_data.hashInterval = interval; }
    void addCheckpoint(uint64_t tick, uint64_t hash) { m_data.checkpoints.push_back({tick, hash}); }

  private:
    bool m_active = false;
    ReplayData m_data;
};

export class InputPlayer {
  public:
    void load(ReplayData data) {
        m_data = std::move(data);
        m_next = 0;
    }
    [[nodiscard]] bool active() const { return m_next < m_data.ticks.size(); }
    [[nodiscard]] bool loaded() const { return !m_data.ticks.empty(); }
    [[nodiscard]] uint64_t seed() const { return m_data.seed; }
    [[nodiscard]] size_t length() const { return m_data.ticks.size(); }
    [[nodiscard]] size_t position() const { return m_next; }
    const ReplayTick& next() { return m_data.ticks[m_next++]; }
    void rewind() { m_next = 0; }

  private:
    ReplayData m_data;
    size_t m_next = 0;
};

export std::string serializeReplay(const ReplayData& data);
export bool parseReplay(const std::string& text, ReplayData& out, std::string* error = nullptr);
export bool loadReplay(const std::string& path, ReplayData& out, std::string* error = nullptr);
