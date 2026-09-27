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

namespace {

ImGuiKey keyByName(const std::string& name) {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const char* keyName = ImGui::GetKeyName(static_cast<ImGuiKey>(k));
        if (keyName && name == keyName) return static_cast<ImGuiKey>(k);
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

}  // namespace

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
        if (hash != std::string::npos && line.rfind("text", 0) != 0) line = line.substr(0, hash);
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
            std::string arg;
            while (words >> arg) step.args.push_back(arg);
        }
        m_steps.push_back(std::move(step));
    }
    m_active = true;
    Lit::Log::Info("Autopilot loaded {} steps from {}", m_steps.size(), path);
    return true;
}

void Autopilot::beforeFrame(EditorContext& ctx, CommandRegistry& commands, float dt) {
    if (!m_active) return;
    ImGuiIO& io = ImGui::GetIO();
    auto pinMouse = [&] {
        if (!m_mouseSet) return;
        if (GLFWwindow* w = static_cast<GLFWwindow*>(window)) {
            ImGui_ImplGlfw_CursorEnterCallback(w, GLFW_TRUE);
            ImGui_ImplGlfw_CursorPosCallback(w, m_mouse[0], m_mouse[1]);
        } else {
            io.AddMousePosEvent(m_mouse[0], m_mouse[1]);
        }
    };
    auto setMouse = [&](float x, float y) {
        m_mouseSet = true;
        m_mouse[0] = x;
        m_mouse[1] = y;
        pinMouse();
    };
    pinMouse();
    if (m_measureFrames > 0) {
        m_measureSamples.push_back(dt * 1000.0f);
        if (--m_measureFrames > 0) return;
        std::vector<float> sorted = m_measureSamples;
        std::sort(sorted.begin(), sorted.end());
        const float median = sorted[sorted.size() / 2];
        const float p95 = sorted[std::min(sorted.size() - 1, sorted.size() * 95 / 100)];
        Lit::Log::Info("[autopilot] measure {}: {} frames, median {:.2f} ms ({:.1f} fps), p95 {:.2f} ms, max {:.2f} ms", m_measureLabel, sorted.size(), median, 1000.0f / median, p95, sorted.back());
        if (m_measureBudget > 0.0f) {
            ++m_checks;
            if (median > m_measureBudget) {
                Lit::Log::Error("[autopilot] FAIL {}: median {:.2f} ms exceeds budget {:.2f} ms", m_measureLabel, median, m_measureBudget);
                ++m_failures;
            }
        }
        m_measureSamples.clear();
    }
    if (m_holdFrames > 0) {
        --m_holdFrames;
        return;
    }
    for (int key : m_releaseKeys) io.AddKeyEvent(static_cast<ImGuiKey>(key), false);
    m_releaseKeys.clear();
    for (int b = 0; b < 3; ++b) {
        if (m_releaseMouse[b]) io.AddMouseButtonEvent(b, false);
        m_releaseMouse[b] = false;
    }
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    io.AddKeyEvent(ImGuiMod_Alt, false);

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
        const auto& a = step.args;
        auto arg = [&](size_t i, const std::string& fallback = {}) { return i < a.size() ? a[i] : fallback; };
        auto num = [&](size_t i, float fallback = 0.0f) { return i < a.size() ? static_cast<float>(std::atof(a[i].c_str())) : fallback; };
        auto coord = [&](size_t i, bool vertical) {
            const float value = num(i);
            if (i < a.size() && !a[i].empty() && a[i].back() == '%') return value * 0.01f * (vertical ? io.DisplaySize.y : io.DisplaySize.x);
            return value;
        };
        if (step.verb == "wait") {
            m_wait = num(0, 0.1f);
            return;
        }
        if (step.verb == "measure") {
            m_measureFrames = std::max(1, static_cast<int>(num(0, 120.0f)));
            m_measureLabel = arg(1, "frames");
            m_measureBudget = num(2, 0.0f);
            m_measureSamples.clear();
            return;
        }
        if (step.verb == "frames") {
            m_waitFrames = static_cast<int>(num(0, 1.0f));
            return;
        }
        if (step.verb == "key" || step.verb == "down") {
            const ImGuiKey key = keyByName(arg(0));
            if (key == ImGuiKey_None) {
                Lit::Log::Error("Autopilot line {}: unknown key '{}'", step.line, arg(0));
                ++m_failures;
                continue;
            }
            for (size_t i = 1; i < a.size(); ++i) {
                if (a[i] == "ctrl") io.AddKeyEvent(ImGuiMod_Ctrl, true);
                if (a[i] == "shift") io.AddKeyEvent(ImGuiMod_Shift, true);
                if (a[i] == "alt") io.AddKeyEvent(ImGuiMod_Alt, true);
            }
            io.AddKeyEvent(key, true);
            if (step.verb == "key") {
                m_releaseKeys.push_back(key);
                m_holdFrames = 1;
            }
            return;
        }
        if (step.verb == "up") {
            io.AddKeyEvent(keyByName(arg(0)), false);
            return;
        }
        if (step.verb == "text") {
            io.AddInputCharactersUTF8(arg(0).c_str());
            return;
        }
        if (step.verb == "move") {
            setMouse(coord(0, false), coord(1, true));
            return;
        }
        if (step.verb == "click") {
            const int button = mouseButton(arg(0, "left"));
            if (a.size() >= 3) setMouse(coord(1, false), coord(2, true));
            io.AddMouseButtonEvent(button, true);
            m_releaseMouse[button] = true;
            m_holdFrames = 1;
            return;
        }
        if (step.verb == "press") {
            for (size_t i = 1; i < a.size(); ++i) {
                if (a[i] == "ctrl") io.AddKeyEvent(ImGuiMod_Ctrl, true);
                if (a[i] == "shift") io.AddKeyEvent(ImGuiMod_Shift, true);
                if (a[i] == "alt") io.AddKeyEvent(ImGuiMod_Alt, true);
            }
            io.AddMouseButtonEvent(mouseButton(arg(0, "left")), true);
            return;
        }
        if (step.verb == "release") {
            io.AddMouseButtonEvent(mouseButton(arg(0, "left")), false);
            return;
        }
        if (step.verb == "wheel") {
            io.AddMouseWheelEvent(0.0f, num(0, 1.0f));
            return;
        }
        if (step.verb == "run") {
            if (!commands.run(arg(0))) {
                Lit::Log::Error("Autopilot line {}: command '{}' did not run", step.line, arg(0));
                ++m_failures;
            }
            return;
        }
        if (step.verb == "set") {
            static const std::pair<std::string_view, std::string_view> aliases[] = {{"time", "timeOfDay"}, {"ev", "manualEV100"}, {"compensation", "exposureCompensation"}, {"debug", "debugView"}, {"cascades", "shadowCascades"}};
            std::string key = arg(0);
            for (const auto& [from, to] : aliases) {
                if (key == from) key = to;
            }
            std::string value;
            for (size_t i = 1; i < a.size(); ++i) value += (i > 1 ? " " : "") + a[i];
            if (!setRenderSetting(ctx.engine.renderSettings(), key, value)) {
                Lit::Log::Error("Autopilot line {}: unknown setting '{}'", step.line, key);
                ++m_failures;
            }
            continue;
        }
        if (step.verb == "pass") {
            bool found = false;
            std::string name;
            for (size_t i = 0; i + 1 < a.size(); ++i) name += (i > 0 ? " " : "") + a[i];
            for (const PassInfo& pass : ctx.engine.customPasses()) {
                if (pass.name != name) continue;
                ctx.engine.setPassEnabled(pass.kind, pass.id, num(a.size() - 1) > 0.5f);
                found = true;
            }
            if (!found) {
                Lit::Log::Error("Autopilot line {}: no pass named '{}'", step.line, name);
                ++m_failures;
            }
            continue;
        }
        if (step.verb == "material") {
            if (!editMaterial || !editMaterial(arg(0), num(1))) {
                Lit::Log::Error("Autopilot line {}: cannot set material field '{}'", step.line, arg(0));
                ++m_failures;
            }
            continue;
        }
        if (step.verb == "save" || step.verb == "load") {
            const auto& action = step.verb == "save" ? saveScene : loadScene;
            if (action) action(arg(0, "resources/autopilot.litscene"));
            m_waitFrames = 2;
            return;
        }
        if (step.verb == "shot") {
            ctx.engine.requestScreenshot(arg(0, "resources/autopilot.png"));
            m_waitFrames = 2;
            return;
        }
        if (step.verb == "log") {
            Lit::Log::Info("[autopilot] {}", arg(0));
            continue;
        }
        if (step.verb == "expect") {
            ++m_checks;
            const std::string what = arg(0);
            std::string actual;
            if (what == "selection") actual = std::to_string(ctx.selection.size());
            if (what == "entities") actual = std::to_string(ctx.world.aliveCount());
            if (what == "undo") actual = std::to_string(ctx.history->undoCount());
            if (what == "mode") actual = ctx.modeLabel;
            if (what == "playing") actual = ctx.playing ? "1" : "0";
            if (what == "name") actual = ctx.world.isAlive(ctx.primary()) ? ctx.world.getName(ctx.primary()) : "";
            if (what.rfind("setting:", 0) == 0) actual = renderSettingValue(ctx.engine.renderSettings(), what.substr(8));
            if (actual.empty() && query) actual = query(what);
            std::string expected = arg(1);
            for (size_t i = 2; i < a.size(); ++i) expected += " " + a[i];
            if (actual == expected) {
                Lit::Log::Info("[autopilot] PASS {} == {}", what, expected);
            } else {
                Lit::Log::Error("[autopilot] FAIL line {}: {} is '{}', expected '{}'", step.line, what, actual, expected);
                ++m_failures;
            }
            continue;
        }
        if (step.verb == "quit") {
            m_finished = true;
            m_active = false;
            Lit::Log::Info("[autopilot] finished: {} checks, {} failures", m_checks, m_failures);
            ctx.requestQuit = true;
            return;
        }
        Lit::Log::Error("Autopilot line {}: unknown verb '{}'", step.line, step.verb);
        ++m_failures;
    }
    m_finished = true;
    m_active = false;
    Lit::Log::Info("[autopilot] finished: {} checks, {} failures", m_checks, m_failures);
}
