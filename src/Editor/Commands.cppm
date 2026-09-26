module;

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

export module Editor.commands;

export struct Chord {
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

export Chord key(ImGuiKey k) { return {k, false, false, false}; }
export Chord ctrl(ImGuiKey k) { return {k, true, false, false}; }
export Chord shift(ImGuiKey k) { return {k, false, true, false}; }
export Chord alt(ImGuiKey k) { return {k, false, false, true}; }
export Chord ctrlShift(ImGuiKey k) { return {k, true, true, false}; }
export Chord ctrlAlt(ImGuiKey k) { return {k, true, false, true}; }

export enum class CommandScope { Global, Viewport };

export struct Command {
    std::string id;
    std::string title;
    std::string category;
    std::vector<Chord> chords;
    std::function<void()> run;
    std::function<bool()> enabled;
    std::function<bool()> checked;
    CommandScope scope = CommandScope::Viewport;
    bool repeat = false;
    bool palette = true;
    bool help = true;
    std::string keysLabel;
    std::string description;
};

export std::string chordLabel(const Chord& chord);

export class CommandRegistry {
  public:
    Command& add(Command command);
    [[nodiscard]] const Command* find(const std::string& id) const;
    bool run(const std::string& id);
    void processShortcuts(bool viewportKeys);
    [[nodiscard]] const std::vector<Command>& all() const { return m_commands; }
    [[nodiscard]] std::string shortcut(const std::string& id) const;
    [[nodiscard]] uint32_t uses(const std::string& id) const;
    [[nodiscard]] uint64_t lastUse(const std::string& id) const;

  private:
    std::vector<Command> m_commands;
    std::unordered_map<std::string, size_t> m_index;
    std::unordered_map<std::string, uint32_t> m_uses;
    std::unordered_map<std::string, uint64_t> m_lastUse;
    uint64_t m_clock = 0;
};
