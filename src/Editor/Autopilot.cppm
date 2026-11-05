module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module Editor.autopilot;

import Editor.context;
import Editor.commands;

export class Autopilot {
  public:
    bool load(const std::string& path);
    [[nodiscard]] bool active() const { return m_active; }
    void beforeFrame(EditorContext& ctx, CommandRegistry& commands, float dt);
    [[nodiscard]] int failures() const { return m_failures; }
    [[nodiscard]] bool finished() const { return m_finished; }
    std::function<std::string(const std::string&)> query;
    std::function<void(const std::string&)> saveScene;
    std::function<void(const std::string&)> loadScene;
    std::function<bool(const std::string&, float)> editMaterial;
    std::function<bool(const std::string&)> loadTestScene;
    std::function<void(int)> showOverlay;
    void* window = nullptr;

  private:
    struct Step {
        std::string verb;
        std::vector<std::string> args;
        int line = 0;
    };

    enum class Flow { Continue, Yield };

    void pinMouse();
    void setMouse(float x, float y);
    void captureFrames(EditorContext& ctx);
    bool recordMeasurement(float deltaSeconds);
    void releaseHeldInput();
    void finish();
    void fail(int line, const std::string& message);
    Flow execute(const Step& step, EditorContext& ctx, CommandRegistry& commands);
    std::optional<Flow> executeTiming(const Step& step);
    std::optional<Flow> executeInput(const Step& step);
    std::optional<Flow> executeEngine(const Step& step, EditorContext& ctx);
    std::optional<Flow> executeScene(const Step& step, EditorContext& ctx);
    Flow executeExpect(const Step& step, EditorContext& ctx);

    bool m_active = false;
    bool m_finished = false;
    std::vector<Step> m_steps;
    size_t m_next = 0;
    float m_wait = 0.0f;
    int m_waitFrames = 0;
    int m_failures = 0;
    int m_checks = 0;
    std::vector<int> m_releaseKeys;
    bool m_releaseMouse[3] = {false, false, false};
    int m_holdFrames = 0;
    bool m_mouseSet = false;
    float m_mouse[2] = {0.0f, 0.0f};
    int m_measureFrames = 0;
    float m_measureBudget = 0.0f;
    std::string m_measureLabel;
    std::vector<float> m_measureSamples;
    int m_captureLeft = 0;
    int m_captureEvery = 1;
    int m_captureTick = 0;
    int m_captureIndex = 0;
    std::string m_capturePrefix;
};
