module;

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <string>

export module Engine.Overlay;

import Engine.engine;
import Engine.Observation;
import Engine.AI;

export class DebugOverlay {
  public:
    void attachObservation(ObservationSystem* observation) { m_observation = observation; }
    void attachTrace(const DecisionTrace* trace) { m_trace = trace; }
    void setVisible(bool visible) { m_visible = visible; }
    [[nodiscard]] bool visible() const { return m_visible; }
    void toggle() { m_visible = !m_visible; }
    void setTab(int tab) { m_requestedTab = tab; }
    void sample(Engine& engine);
    void draw(Engine& engine);

    [[nodiscard]] const std::deque<float>& history(const std::string& param) const;
    [[nodiscard]] size_t sampleCount() const { return m_frameMs.size(); }

  private:
    void drawPortals(Engine& engine);
    void drawParams(Engine& engine);
    void drawObservation(Engine& engine);
    void drawTrace(Engine& engine);
    void drawPerf(Engine& engine);

    bool m_visible = false;
    int m_requestedTab = -1;
    std::string m_selectedParam;
    ObservationSystem* m_observation = nullptr;
    const DecisionTrace* m_trace = nullptr;
    std::deque<float> m_frameMs;
    std::deque<float> m_gpuMs;
    std::map<std::string, std::deque<float>> m_paramHistory;
    std::chrono::steady_clock::time_point m_lastSample{};
    bool m_sampled = false;
};
