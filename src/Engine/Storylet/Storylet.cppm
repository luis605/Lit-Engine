module;

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Engine.Storylet;

import Engine.Events;
import Engine.Json;
import Engine.Rng;
import Engine.State;

export struct StoryletCondition {
    enum class Op { All, Any, Not, Compare, Flag, Member, Played };
    enum class Cmp { Ge, Le, Gt, Lt, Eq, Ne };
    Op op = Op::All;
    Cmp cmp = Cmp::Ge;
    std::string name;
    std::string member;
    double number = 0.0;
    std::vector<StoryletCondition> children;
};

export struct StoryletEffect {
    enum class Op { Set, Add, Remember, AddMember, RemoveMember };
    Op op = Op::Set;
    std::string name;
    std::string member;
    double number = 0.0;
};

export struct StoryletLine {
    std::string speaker;
    std::string textId;
    std::string voiceId;
    float seconds = 3.0f;
};

export struct Storylet {
    std::string id;
    StoryletCondition when;
    int priority = 0;
    double cooldownSeconds = 0.0;
    bool once = false;
    bool interruptible = true;
    std::string delivery;
    std::vector<StoryletLine> lines;
    std::vector<StoryletEffect> effects;
};

export struct StoryletStarted { std::string id; bool resumed = false; };
export struct StoryletLineStarted { std::string id; uint32_t index = 0; std::string speaker, textId, voiceId; };
export struct StoryletLineEnded { std::string id; uint32_t index = 0; bool interrupted = false; };
export struct StoryletCompleted { std::string id; };
export struct StoryletInterrupted { std::string id; std::string reason; uint32_t resumeIndex = 0; };

export struct StoryletPlayback {
    std::string id;
    uint32_t line = 0;
    float elapsed = 0.0f;
};

export class StoryletEngine {
  public:
    StoryletEngine(StateStore& state, RandomService& random, EventBus* bus = nullptr, std::string memorySet = "memory");

    bool loadText(std::string_view text, std::string* error = nullptr);
    bool loadFile(const std::string& path, std::string* error = nullptr);
    void clear();
    [[nodiscard]] const Storylet* find(const std::string& id) const;
    [[nodiscard]] size_t count() const { return m_storylets.size(); }

    [[nodiscard]] bool evaluate(const StoryletCondition& c) const;
    [[nodiscard]] bool eligible(const std::string& id) const;
    [[nodiscard]] std::vector<std::string> eligibleIds() const;
    [[nodiscard]] bool played(const std::string& id) const { return m_played.contains(id); }
    [[nodiscard]] uint32_t playCount(const std::string& id) const;

    void update(float dt);
    bool interrupt(const std::string& reason = "interrupted");
    [[nodiscard]] const std::optional<StoryletPlayback>& current() const { return m_current; }
    [[nodiscard]] double time() const { return m_time; }

    [[nodiscard]] Json toJson() const;
    bool fromJson(const Json& doc, std::string* error = nullptr);
    void resetHistory();

  private:
    struct Record {
        uint32_t count = 0;
        double last = 0.0;
    };

    void start(const Storylet& s);
    void beginLine(const Storylet& s);
    void complete(const Storylet& s);
    void applyEffects(const Storylet& s);
    void select();
    [[nodiscard]] bool available(const Storylet& s) const;

    StateStore& m_state;
    RandomService& m_random;
    EventBus* m_bus;
    std::string m_memorySet;
    std::vector<Storylet> m_storylets;
    std::map<std::string, Record> m_played;
    std::map<std::string, uint32_t> m_resume;
    std::map<std::string, double> m_blocked;
    std::optional<StoryletPlayback> m_current;
    double m_time = 0.0;
    float m_retryDelay = 1.0f;
};
