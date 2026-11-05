module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Log/Log.hpp"

module Editor.autopilot;

import Editor.context;
import Editor.commands;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.settings;
import Engine.Render.custom;
import Engine.Params;
import Engine.Render.entity;
import Engine.Render.component;

namespace {

constexpr int kMouseButtonCount = 3;
constexpr float kMillisecondsPerSecond = 1000.0f;
constexpr size_t kPercentile95 = 95;
constexpr float kDefaultWaitSeconds = 0.1f;
constexpr float kDefaultMeasureFrames = 120.0f;
constexpr int kFramesAfterAction = 2;
constexpr float kBooleanThreshold = 0.5f;
constexpr float kPercentToFraction = 0.01f;

ImGuiKey keyByName(const std::string& name) {
    for (int keyIndex = ImGuiKey_NamedKey_BEGIN; keyIndex < ImGuiKey_NamedKey_END; ++keyIndex) {
        const char* keyName = ImGui::GetKeyName(static_cast<ImGuiKey>(keyIndex));
        if (keyName && name == keyName) return static_cast<ImGuiKey>(keyIndex);
    }
    if (name == "[") return ImGuiKey_LeftBracket;
    if (name == "]") return ImGuiKey_RightBracket;
    if (name == "`") return ImGuiKey_GraveAccent;
    if (name == "=") return ImGuiKey_Equal;
    if (name == "/") return ImGuiKey_Slash;
    if (name == "Esc") return ImGuiKey_Escape;
    if (name == "Del") return ImGuiKey_Delete;
    return ImGuiKey_None;
}

int mouseButton(const std::string& name) {
    if (name == "right") return ImGuiMouseButton_Right;
    if (name == "middle") return ImGuiMouseButton_Middle;
    return ImGuiMouseButton_Left;
}

void applyModifiers(ImGuiIO& io, const std::vector<std::string>& args, size_t firstModifier) {
    for (size_t index = firstModifier; index < args.size(); ++index) {
        if (args[index] == "ctrl") io.AddKeyEvent(ImGuiMod_Ctrl, true);
        if (args[index] == "shift") io.AddKeyEvent(ImGuiMod_Shift, true);
        if (args[index] == "alt") io.AddKeyEvent(ImGuiMod_Alt, true);
    }
}

class ArgumentReader {
  public:
    explicit ArgumentReader(const std::vector<std::string>& args) : m_args(args) {}

    [[nodiscard]] size_t size() const { return m_args.size(); }

    [[nodiscard]] std::string text(size_t index, const std::string& fallback = {}) const {
        return index < m_args.size() ? m_args[index] : fallback;
    }

    [[nodiscard]] float number(size_t index, float fallback = 0.0f) const {
        return index < m_args.size() ? static_cast<float>(std::atof(m_args[index].c_str())) : fallback;
    }

    [[nodiscard]] float coordinate(size_t index, bool vertical, const ImVec2& displaySize) const {
        const float value = number(index);
        if (index < m_args.size() && !m_args[index].empty() && m_args[index].back() == '%') {
            return value * kPercentToFraction * (vertical ? displaySize.y : displaySize.x);
        }
        return value;
    }

  private:
    const std::vector<std::string>& m_args;
};

std::string joinFrom(const std::vector<std::string>& args, size_t first, size_t last) {
    std::string joined;
    for (size_t index = first; index < last; ++index) {
        if (index > first) joined += " ";
        joined += args[index];
    }
    return joined;
}

}

bool Autopilot::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        Lit::Log::Error("Autopilot script '{}' not found", path);
        return false;
    }
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const size_t hash = line.find('#');
        if (hash != std::string::npos && line.rfind("text", 0) != 0) line.resize(hash);
        std::istringstream words(line);
        Step step;
        step.line = number;
        if (!(words >> step.verb)) continue;
        if (step.verb == "text" || step.verb == "log") {
            std::string rest;
            std::getline(words, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
            step.args.push_back(rest);
        } else {
            std::string argument;
            while (words >> argument) step.args.push_back(argument);
        }
        m_steps.push_back(std::move(step));
    }
    m_active = true;
    Lit::Log::Info("Autopilot loaded {} steps from {}", m_steps.size(), path);
    return true;
}

void Autopilot::pinMouse() {
    if (!m_mouseSet) return;
    if (GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window)) {
        ImGui_ImplGlfw_CursorEnterCallback(glfwWindow, GLFW_TRUE);
        ImGui_ImplGlfw_CursorPosCallback(glfwWindow, m_mouse[0], m_mouse[1]);
    } else {
        ImGui::GetIO().AddMousePosEvent(m_mouse[0], m_mouse[1]);
    }
}

void Autopilot::setMouse(float x, float y) {
    m_mouseSet = true;
    m_mouse[0] = x;
    m_mouse[1] = y;
    pinMouse();
}

void Autopilot::captureFrames(EditorContext& ctx) {
    if (m_captureLeft <= 0) return;
    if (m_captureTick++ % m_captureEvery != 0) return;
    ctx.engine.requestScreenshot(std::format("{}_{:04}.png", m_capturePrefix, m_captureIndex++));
    --m_captureLeft;
}

bool Autopilot::recordMeasurement(float deltaSeconds) {
    m_measureSamples.push_back(deltaSeconds * kMillisecondsPerSecond);
    if (--m_measureFrames > 0) return true;
    std::vector<float> sorted = m_measureSamples;
    std::ranges::sort(sorted);
    const float median = sorted[sorted.size() / 2];
    const float percentile95 = sorted[std::min(sorted.size() - 1, sorted.size() * kPercentile95 / 100)];
    Lit::Log::Info("[autopilot] measure {}: {} frames, median {:.2f} ms ({:.1f} fps), p95 {:.2f} ms, max {:.2f} ms", m_measureLabel, sorted.size(), median, kMillisecondsPerSecond / std::max(median, 1e-6f), percentile95, sorted.back());
    if (m_measureBudget > 0.0f) {
        ++m_checks;
        if (median > m_measureBudget) {
            Lit::Log::Error("[autopilot] FAIL {}: median {:.2f} ms exceeds budget {:.2f} ms", m_measureLabel, median, m_measureBudget);
            ++m_failures;
        }
    }
    m_measureSamples.clear();
    return false;
}

void Autopilot::releaseHeldInput() {
    ImGuiIO& io = ImGui::GetIO();
    for (int heldKey : m_releaseKeys) io.AddKeyEvent(static_cast<ImGuiKey>(heldKey), false);
    m_releaseKeys.clear();
    for (int button = 0; button < kMouseButtonCount; ++button) {
        if (m_releaseMouse[button]) io.AddMouseButtonEvent(button, false);
        m_releaseMouse[button] = false;
    }
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    io.AddKeyEvent(ImGuiMod_Alt, false);
}

void Autopilot::finish() {
    m_finished = true;
    m_active = false;
    Lit::Log::Info("[autopilot] finished: {} checks, {} failures", m_checks, m_failures);
}

void Autopilot::fail(int line, const std::string& message) {
    Lit::Log::Error("Autopilot line {}: {}", line, message);
    ++m_failures;
}

void Autopilot::beforeFrame(EditorContext& ctx, CommandRegistry& commands, float dt) {
    if (!m_active) return;
    pinMouse();
    captureFrames(ctx);
    if (m_measureFrames > 0 && recordMeasurement(dt)) return;
    if (m_holdFrames > 0) {
        --m_holdFrames;
        return;
    }
    releaseHeldInput();
    if (m_waitFrames > 0) {
        --m_waitFrames;
        return;
    }
    if (m_wait > 0.0f) {
        m_wait -= dt;
        return;
    }
    while (m_next < m_steps.size()) {
        const Step& step = m_steps[m_next++];
        if (execute(step, ctx, commands) == Flow::Yield) return;
    }
    finish();
}

Autopilot::Flow Autopilot::execute(const Step& step, EditorContext& ctx, CommandRegistry& commands) {
    if (step.verb == "quit") {
        finish();
        ctx.requestQuit = true;
        return Flow::Yield;
    }
    if (step.verb == "expect") return executeExpect(step, ctx);
    if (const std::optional<Flow> flow = executeTiming(step)) return *flow;
    if (const std::optional<Flow> flow = executeInput(step)) return *flow;
    if (step.verb == "run") {
        const ArgumentReader reader(step.args);
        if (!commands.run(reader.text(0))) fail(step.line, std::format("command '{}' did not run", reader.text(0)));
        return Flow::Yield;
    }
    if (const std::optional<Flow> flow = executeEngine(step, ctx)) return *flow;
    if (const std::optional<Flow> flow = executeScene(step, ctx)) return *flow;
    fail(step.line, std::format("unknown verb '{}'", step.verb));
    return Flow::Continue;
}

std::optional<Autopilot::Flow> Autopilot::executeTiming(const Step& step) {
    const ArgumentReader reader(step.args);
    if (step.verb == "wait") {
        m_wait = reader.number(0, kDefaultWaitSeconds);
        return Flow::Yield;
    }
    if (step.verb == "measure") {
        m_measureFrames = std::max(1, static_cast<int>(reader.number(0, kDefaultMeasureFrames)));
        m_measureLabel = reader.text(1, "frames");
        m_measureBudget = reader.number(2, 0.0f);
        m_measureSamples.clear();
        return Flow::Yield;
    }
    if (step.verb == "frames") {
        m_waitFrames = static_cast<int>(reader.number(0, 1.0f));
        return Flow::Yield;
    }
    return std::nullopt;
}

std::optional<Autopilot::Flow> Autopilot::executeInput(const Step& step) {
    ImGuiIO& io = ImGui::GetIO();
    const ArgumentReader reader(step.args);
    if (step.verb == "key" || step.verb == "down") {
        const ImGuiKey pressedKey = keyByName(reader.text(0));
        if (pressedKey == ImGuiKey_None) {
            fail(step.line, std::format("unknown key '{}'", reader.text(0)));
            return Flow::Continue;
        }
        applyModifiers(io, step.args, 1);
        io.AddKeyEvent(pressedKey, true);
        if (step.verb == "key") {
            m_releaseKeys.push_back(pressedKey);
            m_holdFrames = 1;
        }
        return Flow::Yield;
    }
    if (step.verb == "up") {
        io.AddKeyEvent(keyByName(reader.text(0)), false);
        return Flow::Yield;
    }
    if (step.verb == "text") {
        io.AddInputCharactersUTF8(reader.text(0).c_str());
        return Flow::Yield;
    }
    if (step.verb == "move") {
        setMouse(reader.coordinate(0, false, io.DisplaySize), reader.coordinate(1, true, io.DisplaySize));
        return Flow::Yield;
    }
    if (step.verb == "click") {
        const int button = mouseButton(reader.text(0, "left"));
        if (reader.size() >= 3) setMouse(reader.coordinate(1, false, io.DisplaySize), reader.coordinate(2, true, io.DisplaySize));
        io.AddMouseButtonEvent(button, true);
        m_releaseMouse[button] = true;
        m_holdFrames = 1;
        return Flow::Yield;
    }
    if (step.verb == "press") {
        applyModifiers(io, step.args, 1);
        io.AddMouseButtonEvent(mouseButton(reader.text(0, "left")), true);
        return Flow::Yield;
    }
    if (step.verb == "release") {
        io.AddMouseButtonEvent(mouseButton(reader.text(0, "left")), false);
        return Flow::Yield;
    }
    if (step.verb == "wheel") {
        io.AddMouseWheelEvent(0.0f, reader.number(0, 1.0f));
        return Flow::Yield;
    }
    return std::nullopt;
}

std::optional<Autopilot::Flow> Autopilot::executeEngine(const Step& step, EditorContext& ctx) {
    const ArgumentReader reader(step.args);
    if (step.verb == "set") {
        static const std::pair<std::string_view, std::string_view> aliases[] = {{"time", "timeOfDay"}, {"ev", "manualEV100"}, {"compensation", "exposureCompensation"}, {"debug", "debugView"}, {"cascades", "shadowCascades"}};
        std::string settingName = reader.text(0);
        for (const auto& [alias, canonical] : aliases) {
            if (settingName == alias) settingName = canonical;
        }
        const std::string value = joinFrom(step.args, 1, step.args.size());
        if (!setRenderSetting(ctx.engine.renderSettings(), settingName, value)) fail(step.line, std::format("unknown setting '{}'", settingName));
        return Flow::Continue;
    }
    if (step.verb == "pass") {
        bool found = false;
        const std::string name = joinFrom(step.args, 0, step.args.empty() ? 0 : step.args.size() - 1);
        for (const PassInfo& pass : ctx.engine.customPasses()) {
            if (pass.name != name) continue;
            ctx.engine.setPassEnabled(pass.kind, pass.id, reader.number(step.args.size() - 1) > kBooleanThreshold);
            found = true;
        }
        if (!found) fail(step.line, std::format("no pass named '{}'", name));
        return Flow::Continue;
    }
    if (step.verb == "material") {
        if (!editMaterial || !editMaterial(reader.text(0), reader.number(1))) fail(step.line, std::format("cannot set material field '{}'", reader.text(0)));
        return Flow::Continue;
    }
    if (step.verb == "shot") {
        ctx.engine.requestScreenshot(reader.text(0, "resources/autopilot.png"));
        m_waitFrames = kFramesAfterAction;
        return Flow::Yield;
    }
    if (step.verb == "capture") {
        m_capturePrefix = reader.text(0, "resources/capture");
        m_captureLeft = std::max(1, static_cast<int>(reader.number(1, 1.0f)));
        m_captureEvery = std::max(1, static_cast<int>(reader.number(2, 1.0f)));
        m_captureTick = 0;
        m_captureIndex = 0;
        m_waitFrames = m_captureLeft * m_captureEvery + kFramesAfterAction;
        return Flow::Yield;
    }
    if (step.verb == "dump") {
        if (!ctx.engine.writeStateDump(reader.text(0, "resources/state.json"))) fail(step.line, std::format("cannot write state dump '{}'", reader.text(0)));
        return Flow::Continue;
    }
    if (step.verb == "perf") {
        if (reader.text(0) == "off") ctx.engine.disablePerfLog();
        else ctx.engine.enablePerfLog(reader.text(0, "resources/perf.csv"));
        return Flow::Continue;
    }
    if (step.verb == "record") {
        if (reader.text(0) == "off") ctx.engine.stopRecording();
        else ctx.engine.startRecording(reader.text(0, "resources/session.replay"));
        return Flow::Continue;
    }
    if (step.verb == "replay") {
        if (!ctx.engine.startReplay(reader.text(0, "resources/session.replay"))) fail(step.line, std::format("cannot replay '{}'", reader.text(0)));
        return Flow::Continue;
    }
    if (step.verb == "param") {
        ctx.engine.params().set(reader.text(0), reader.number(1));
        return Flow::Continue;
    }
    if (step.verb == "log") {
        Lit::Log::Info("[autopilot] {}", reader.text(0));
        return Flow::Continue;
    }
    return std::nullopt;
}

std::optional<Autopilot::Flow> Autopilot::executeScene(const Step& step, EditorContext& ctx) {
    const ArgumentReader reader(step.args);
    if (step.verb == "save" || step.verb == "load") {
        const auto& action = step.verb == "save" ? saveScene : loadScene;
        if (action) action(reader.text(0, "resources/autopilot.litscene"));
        m_waitFrames = kFramesAfterAction;
        return Flow::Yield;
    }
    if (step.verb == "mask") {
        const std::string name = reader.text(0);
        const bool masked = reader.number(1, 1.0f) > kBooleanThreshold;
        size_t count = 0;
        ctx.world.forEach([&](EntityHandle entity) {
            if (ctx.world.getName(entity) != name) return;
            ctx.world.setRenderFlags(entity, RENDER_MASKED, masked);
            ++count;
        });
        if (count == 0) fail(step.line, std::format("no entity named '{}'", name));
        return Flow::Continue;
    }
    if (step.verb == "testscene") {
        if (!loadTestScene || !loadTestScene(reader.text(0))) fail(step.line, std::format("unknown test scene '{}'", reader.text(0)));
        m_waitFrames = kFramesAfterAction;
        return Flow::Yield;
    }
    if (step.verb == "overlay") {
        if (showOverlay) showOverlay(static_cast<int>(reader.number(0, 0.0f)));
        return Flow::Continue;
    }
    return std::nullopt;
}

Autopilot::Flow Autopilot::executeExpect(const Step& step, EditorContext& ctx) {
    ++m_checks;
    const ArgumentReader reader(step.args);
    const std::string what = reader.text(0);
    std::string actual;
    if (what == "selection") actual = std::to_string(ctx.selection.size());
    if (what == "entities") actual = std::to_string(ctx.world.aliveCount());
    if (what == "undo") actual = std::to_string(ctx.history->undoCount());
    if (what == "mode") actual = ctx.modeLabel;
    if (what == "playing") actual = ctx.playing ? "1" : "0";
    if (what == "name") actual = ctx.world.isAlive(ctx.primary()) ? ctx.world.getName(ctx.primary()) : "";
    if (what.rfind("param:", 0) == 0) actual = std::format("{:.3f}", ctx.engine.params().get(what.substr(6)));
    if (what.rfind("setting:", 0) == 0) actual = renderSettingValue(ctx.engine.renderSettings(), what.substr(8));
    if (actual.empty() && query) actual = query(what);
    std::string expected = reader.text(1);
    for (size_t index = 2; index < step.args.size(); ++index) expected += " " + step.args[index];
    if (actual == expected) {
        Lit::Log::Info("[autopilot] PASS {} == {}", what, expected);
    } else {
        Lit::Log::Error("[autopilot] FAIL line {}: {} is '{}', expected '{}'", step.line, what, actual, expected);
        ++m_failures;
    }
    return Flow::Continue;
}
