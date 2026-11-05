module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

export module Editor.profilerpanel;

import Editor.context;
import Engine.engine;
import Engine.Profiler;
import Engine.Json;

export struct ProfilerSnapshot {
    std::vector<ProfileRecord> records;
    std::vector<std::pair<std::string, double>> gpuPasses;
    std::vector<std::pair<std::string, double>> systems;
    double cpuMs = 0.0;
    double gpuMs = 0.0;
    uint64_t frame = 0;
};

export class ProfilerPanel {
  public:
    void toggle(Engine& engine);
    void update(Engine& engine, float frameMs);
    void draw(EditorContext& ctx);
    void setPaused(bool paused) { m_paused = paused; }
    [[nodiscard]] bool open() const { return m_open; }
    [[nodiscard]] bool paused() const { return m_paused; }
    [[nodiscard]] Json toJson() const;
    bool exportJson(const std::string& path) const;

  private:
    void capture(Engine& engine);
    void drawHistory();
    void drawFlame();
    void drawGpu();
    void drawSystems();

    bool m_open = false;
    bool m_paused = false;
    bool m_enabledProfiler = false;
    std::deque<float> m_frameHistory;
    std::vector<float> m_plot;
    ProfilerSnapshot m_snapshot;
    int m_selected = -1;
    float m_zoom = 1.0f;
};
