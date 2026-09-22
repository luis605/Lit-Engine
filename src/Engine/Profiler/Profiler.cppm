module;

#include <chrono>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

export module Engine.Profiler;

export struct ProfileRecord {
    std::string name;
    uint32_t depth = 0;
    double startMs = 0.0;
    double durationMs = 0.0;
    uint64_t frame = 0;
};

export struct ProfileSummary {
    std::string name;
    double totalMs = 0.0;
    uint32_t calls = 0;
};

export class Profiler {
  public:
    explicit Profiler(size_t historyFrames = 120);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool enabled() const { return m_enabled; }

    void beginFrame();
    void endFrame();

    int64_t begin(std::string_view name);
    void end(int64_t token);

    [[nodiscard]] const std::vector<ProfileRecord>& currentFrame() const { return m_current; }
    [[nodiscard]] const std::deque<std::vector<ProfileRecord>>& history() const { return m_history; }
    [[nodiscard]] std::vector<ProfileSummary> topScopes(size_t count) const;
    [[nodiscard]] double lastFrameMs() const { return m_lastFrameMs; }
    [[nodiscard]] uint64_t frameIndex() const { return m_frame; }

    void writeChromeTrace(std::ostream& out) const;
    bool writeChromeTrace(const std::string& path) const;
    void clear();

  private:
    [[nodiscard]] double nowMs() const;

    bool m_enabled = false;
    size_t m_capacity;
    uint64_t m_frame = 0;
    uint32_t m_depth = 0;
    double m_lastFrameMs = 0.0;
    double m_frameStartMs = 0.0;
    std::chrono::steady_clock::time_point m_origin;
    std::vector<ProfileRecord> m_current;
    std::vector<ProfileRecord> m_previous;
    std::deque<std::vector<ProfileRecord>> m_history;
};

export class ProfileScope {
  public:
    ProfileScope(Profiler& profiler, std::string_view name) : m_profiler(profiler), m_token(profiler.begin(name)) {}
    ~ProfileScope() { m_profiler.end(m_token); }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

  private:
    Profiler& m_profiler;
    int64_t m_token;
};
