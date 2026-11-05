module;

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

module Editor.commands;

namespace {

struct KeyAlias {
    const char* imguiName;
    const char* label;
};

constexpr KeyAlias KEY_ALIASES[] = {
    {"LeftBracket", "["}, {"RightBracket", "]"}, {"Period", "."}, {"Comma", ","}, {"Slash", "/"}, {"GraveAccent", "`"}, {"Minus", "-"}, {"Equal", "="},
    {"Delete", "Del"}, {"Escape", "Esc"}, {"UpArrow", "Up"}, {"DownArrow", "Down"}, {"LeftArrow", "Left"}, {"RightArrow", "Right"}, {"PageUp", "PgUp"}, {"PageDown", "PgDn"},
};

}

std::string chordLabel(const Chord& chord) {
    std::string label;
    if (chord.ctrl) label += "Ctrl+";
    if (chord.shift) label += "Shift+";
    if (chord.alt) label += "Alt+";
    const char* name = ImGui::GetKeyName(chord.key);
    std::string keyName = name ? name : "?";
    for (const KeyAlias& alias : KEY_ALIASES) {
        if (keyName == alias.imguiName) {
            keyName = alias.label;
            break;
        }
    }
    return label + keyName;
}

Command& CommandRegistry::add(Command command) {
    const auto existing = m_index.find(command.id);
    if (existing != m_index.end()) {
        m_commands[existing->second] = std::move(command);
        return m_commands[existing->second];
    }
    m_index.emplace(command.id, m_commands.size());
    m_commands.push_back(std::move(command));
    return m_commands.back();
}

const Command* CommandRegistry::find(const std::string& id) const {
    const auto found = m_index.find(id);
    return found == m_index.end() ? nullptr : &m_commands[found->second];
}

bool CommandRegistry::run(const std::string& id) {
    const auto found = m_index.find(id);
    if (found == m_index.end()) return false;
    const Command& command = m_commands[found->second];
    if (command.enabled && !command.enabled()) return false;
    if (!command.run) return false;
    ++m_uses[id];
    m_lastUse[id] = ++m_clock;
    const std::function<void()> action = command.run;
    action();
    return true;
}

void CommandRegistry::processShortcuts(bool viewportKeys) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrlDown = io.KeyCtrl;
    const bool shiftDown = io.KeyShift;
    const bool altDown = io.KeyAlt;
    std::vector<std::string> fired;
    for (const Command& command : m_commands) {
        if (command.scope == CommandScope::Viewport && !viewportKeys) continue;
        for (const Chord& chord : command.chords) {
            if (chord.key == ImGuiKey_None) continue;
            if (chord.ctrl != ctrlDown || chord.shift != shiftDown || chord.alt != altDown) continue;
            if (!ImGui::IsKeyPressed(chord.key, command.repeat)) continue;
            fired.push_back(command.id);
            break;
        }
    }
    for (const std::string& id : fired) run(id);
}

std::string CommandRegistry::shortcut(const std::string& id) const {
    const Command* command = find(id);
    if (!command || command->chords.empty()) return {};
    return chordLabel(command->chords.front());
}

uint32_t CommandRegistry::uses(const std::string& id) const {
    const auto found = m_uses.find(id);
    return found == m_uses.end() ? 0 : found->second;
}

uint64_t CommandRegistry::lastUse(const std::string& id) const {
    const auto found = m_lastUse.find(id);
    return found == m_lastUse.end() ? 0 : found->second;
}
