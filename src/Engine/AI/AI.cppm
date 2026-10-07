module;

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Engine.AI;

import Engine.Json;
import Engine.Rng;
import Engine.Params;

export enum class Status { Success, Failure, Running };

export class Blackboard {
  public:
    void set(const std::string& key, Json value) { m_values[key] = std::move(value); }
    void setNumber(const std::string& key, double v) { m_values[key] = Json(v); }
    void setBool(const std::string& key, bool v) { m_values[key] = Json(v); }
    void setString(const std::string& key, std::string v) { m_values[key] = Json(std::move(v)); }
    [[nodiscard]] bool has(const std::string& key) const { return m_values.contains(key); }
    [[nodiscard]] const Json* get(const std::string& key) const;
    [[nodiscard]] double number(const std::string& key, double fallback = 0.0) const;
    [[nodiscard]] bool boolean(const std::string& key, bool fallback = false) const;
    [[nodiscard]] std::string string(const std::string& key, const std::string& fallback = {}) const;
    void importParams(const ParamBus& bus, const std::string& prefix = "param.");
    void erase(const std::string& key) { m_values.erase(key); }
    void clear() { m_values.clear(); }
    [[nodiscard]] const std::map<std::string, Json>& values() const { return m_values; }
    [[nodiscard]] Json toJson() const;

  private:
    std::map<std::string, Json> m_values;
};

export struct AiContext {
    Blackboard& blackboard;
    Pcg32& rng;
    float dt = 0.0f;
    uint64_t tick = 0;
    uint32_t agent = 0;
};

export using ActionFn = std::function<Status(AiContext&)>;

export class ActionRegistry {
  public:
    void add(const std::string& name, ActionFn fn) { m_actions[name] = std::move(fn); }
    [[nodiscard]] const ActionFn* find(const std::string& name) const {
        const auto it = m_actions.find(name);
        return it == m_actions.end() ? nullptr : &it->second;
    }

  private:
    std::map<std::string, ActionFn> m_actions;
};

export struct TraceConsideration {
    std::string input;
    double value = 0.0;
    double score = 0.0;
};

export struct TraceOption {
    std::string name;
    double score = 0.0;
    bool available = true;
    std::vector<TraceConsideration> considerations;
};

export struct TraceEvent {
    uint64_t tick = 0;
    uint32_t agent = 0;
    std::string node;
    std::string kind;
    std::vector<TraceOption> options;
    std::string winner;
    std::string reason;
};

export class DecisionTrace {
  public:
    void setEnabled(bool on) { m_enabled = on; }
    [[nodiscard]] bool enabled() const { return m_enabled; }
    void setCapacity(size_t capacity) { m_capacity = capacity; }
    void record(TraceEvent event);
    void clear() { m_events.clear(); }
    [[nodiscard]] const std::vector<TraceEvent>& events() const { return m_events; }
    [[nodiscard]] std::vector<const TraceEvent*> forTick(uint64_t tick) const;
    [[nodiscard]] std::string lastWinner(const std::string& node) const;
    [[nodiscard]] Json toJson() const;
    [[nodiscard]] std::string toText() const;
    bool writeFile(const std::string& path, bool asText = false) const;

  private:
    bool m_enabled = true;
    size_t m_capacity = 4096;
    std::vector<TraceEvent> m_events;
};

export class BehaviorGraph {
  public:
    BehaviorGraph();
    ~BehaviorGraph();
    BehaviorGraph(BehaviorGraph&&) noexcept;
    BehaviorGraph& operator=(BehaviorGraph&&) noexcept;

    bool load(const Json& doc, std::string* error = nullptr);
    bool loadText(std::string_view text, std::string* error = nullptr);
    bool loadFile(const std::string& path, std::string* error = nullptr);
    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] size_t nodeCount() const;
    [[nodiscard]] bool valid() const;

    struct Impl;
    [[nodiscard]] const Impl& impl() const { return *m_impl; }

  private:
    std::unique_ptr<Impl> m_impl;
};

export class BehaviorRunner {
  public:
    BehaviorRunner(const BehaviorGraph& graph, const ActionRegistry& actions, Pcg32& rng, uint32_t agent = 0);
    ~BehaviorRunner();
    BehaviorRunner(const BehaviorRunner&) = delete;
    BehaviorRunner& operator=(const BehaviorRunner&) = delete;

    Status tick(float dt);
    void reset();
    [[nodiscard]] Blackboard& blackboard() { return m_blackboard; }
    [[nodiscard]] const Blackboard& blackboard() const { return m_blackboard; }
    [[nodiscard]] DecisionTrace& trace() { return m_trace; }
    [[nodiscard]] const DecisionTrace& trace() const { return m_trace; }
    [[nodiscard]] uint64_t tickCount() const { return m_tick; }
    [[nodiscard]] std::vector<std::string> activePath() const;
    [[nodiscard]] std::string stateOf(const std::string& fsmName) const;
    [[nodiscard]] uint64_t stateHash() const;

    struct Impl;

  private:
    std::unique_ptr<Impl> m_impl;
    const BehaviorGraph& m_graph;
    const ActionRegistry& m_actions;
    Pcg32& m_rng;
    uint32_t m_agent;
    Blackboard m_blackboard;
    DecisionTrace m_trace;
    uint64_t m_tick = 0;
};
