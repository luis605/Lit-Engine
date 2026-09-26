module;

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

module Editor.commands;

std::string chordLabel(const Chord& chord) {
    std::string out;
    if (chord.ctrl) out += "Ctrl+";
    if (chord.shift) out += "Shift+";
    if (chord.alt) out += "Alt+";
    const char* name = ImGui::GetKeyName(chord.key);
    std::string keyName = name ? name : "?";
    if (keyName == "Space") keyName = "Space";
    if (keyName == "LeftBracket") keyName = "[";
    if (keyName == "RightBracket") keyName = "]";
    if (keyName == "Period") keyName = ".";
    if (keyName == "Comma") keyName = ",";
    if (keyName == "Slash") keyName = "/";
    if (keyName == "GraveAccent") keyName = "`";
    if (keyName == "Minus") keyName = "-";
    if (keyName == "Equal") keyName = "=";
    if (keyName == "Delete") keyName = "Del";
    if (keyName == "Escape") keyName = "Esc";
    if (keyName == "UpArrow") keyName = "Up";
    if (keyName == "DownArrow") keyName = "Down";
    if (keyName == "LeftArrow") keyName = "Left";
    if (keyName == "RightArrow") keyName = "Right";
    if (keyName == "PageUp") keyName = "PgUp";
    if (keyName == "PageDown") keyName = "PgDn";
    return out + keyName;
}

Command& CommandRegistry::add(Command command) {
    const auto it = m_index.find(command.id);
    if (it != m_index.end()) {
        m_commands[it->second] = std::move(command);
        return m_commands[it->second];
    }
    m_index.emplace(command.id, m_commands.size());
    m_commands.push_back(std::move(command));
    return m_commands.back();
}

const Command* CommandRegistry::find(const std::string& id) const {
    const auto it = m_index.find(id);
    return it == m_index.end() ? nullptr : &m_commands[it->second];
}

bool CommandRegistry::run(const std::string& id) {
    const auto it = m_index.find(id);
    if (it == m_index.end()) return false;
    Command& command = m_commands[it->second];
    if (command.enabled && !command.enabled()) return false;
    if (!command.run) return false;
    ++m_uses[id];
    m_lastUse[id] = ++m_clock;
    const auto fn = command.run;
    fn();
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
    const auto it = m_uses.find(id);
    return it == m_uses.end() ? 0 : it->second;
}

uint64_t CommandRegistry::lastUse(const std::string& id) const {
    const auto it = m_lastUse.find(id);
    return it == m_lastUse.end() ? 0 : it->second;
}
