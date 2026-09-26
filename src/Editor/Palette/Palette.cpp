module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

module Editor.palette;

import Editor.context;
import Editor.commands;
import Engine.engine;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr size_t kEntityBudget = 400000;
constexpr size_t kEntityResults = 60;

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

ImU32 kindColor(int kind) {
    switch (kind) {
        case 1: return IM_COL32(110, 200, 255, 255);
        case 2: return IM_COL32(140, 230, 140, 255);
        case 3: return IM_COL32(255, 200, 110, 255);
        default: return IM_COL32(255, 176, 66, 255);
    }
}

}  // namespace

void Palette::open(EditorContext&, std::string seed) {
    m_open = true;
    m_focus = true;
    m_promptMode = false;
    m_text = std::move(seed);
    m_builtFor = "\x01";
    m_cursor = 0;
}

void Palette::prompt(PromptRequest request) {
    m_prompt = std::move(request);
    m_promptMode = true;
    m_open = true;
    m_focus = true;
    m_text = m_prompt.initial;
    m_builtFor = "\x01";
    m_cursor = 0;
}

void Palette::scanEntities(EditorContext& ctx) {
    if (m_scanDone) return;
    World& world = ctx.world;
    const size_t slots = world.database().transforms.size();
    size_t budget = kEntityBudget;
    const bool numeric = !m_scanPattern.empty() && m_scanPattern[0] == '#';
    const long long wanted = numeric ? std::atoll(m_scanPattern.c_str() + 1) : -1;
    if (numeric) {
        const EntityHandle e = wanted >= 0 ? world.handleOf(static_cast<Entity>(wanted)) : NULL_ENTITY;
        if (!e.isNull()) m_entityResults.push_back({Result::Kind::Entity, displayName(world, e), meshLabel(ctx.engine, world.getMesh(e)), "", "", e, 100.0f});
        m_scanDone = true;
        return;
    }
    while (m_scanCursor < slots && budget-- > 0) {
        const EntityHandle e = world.handleOf(m_scanCursor++);
        if (e.isNull()) continue;
        const std::string& name = world.getName(e);
        if (name.empty()) continue;
        const float score = fuzzyScore(m_scanPattern, name);
        if (score <= 0.0f) continue;
        m_entityResults.push_back({Result::Kind::Entity, name, meshLabel(ctx.engine, world.getMesh(e)) + (world.getTag(e).empty() ? "" : "  #" + world.getTag(e)), "", "", e, score * 0.9f});
        if (m_entityResults.size() > kEntityResults * 4) {
            std::sort(m_entityResults.begin(), m_entityResults.end(), [](const Result& a, const Result& b) { return a.score > b.score; });
            m_entityResults.resize(kEntityResults);
        }
    }
    if (m_scanCursor >= slots) m_scanDone = true;
}

void Palette::rebuild(EditorContext& ctx, CommandRegistry& commands) {
    const bool changed = m_builtFor != m_text;
    std::string text = trim(m_text);
    if (changed) {
        m_builtFor = m_text;
        m_cursor = 0;
        m_entityResults.clear();
        m_scanCursor = 0;
        m_scanDone = true;
    }
    m_results.clear();

    if (m_promptMode) {
        m_results.push_back({Result::Kind::Suggestion, text.empty() ? m_prompt.hint : text, "Enter to accept", "", "", NULL_ENTITY, 1000.0f});
        for (const std::string& s : m_prompt.suggestions) {
            const float score = text.empty() ? 1.0f : fuzzyScore(text, s);
            if (score > 0.0f && s != text) m_results.push_back({Result::Kind::Suggestion, s, "", "", "", NULL_ENTITY, score});
        }
        std::stable_sort(m_results.begin() + 1, m_results.end(), [](const Result& a, const Result& b) { return a.score > b.score; });
        return;
    }

    enum class Mode { All, Commands, Entities, Calc, Help };
    Mode mode = Mode::All;
    if (!text.empty() && text[0] == '>') mode = Mode::Commands;
    if (!text.empty() && text[0] == '@') mode = Mode::Entities;
    if (!text.empty() && text[0] == '=') mode = Mode::Calc;
    if (!text.empty() && text[0] == '?') mode = Mode::Help;
    const std::string pattern = mode == Mode::All ? text : trim(text.substr(std::min<size_t>(1, text.size())));

    if (mode == Mode::Calc || mode == Mode::All) {
        if (const std::optional<double> value = evaluateExpression(pattern); value && (mode == Mode::Calc || pattern.find_first_of("+-*/^(") != std::string::npos)) {
            m_results.push_back({Result::Kind::Calculator, std::format("= {:.6g}", *value), "Enter copies the result", "", "", NULL_ENTITY, 1000.0f});
        }
        if (mode == Mode::Calc) return;
    }
    if (mode == Mode::Help) {
        const char* tips[] = {"Type to fuzzy-search every command, e.g. 'dup' finds Duplicate", "> filters commands only, @ finds entities by name or #index", "= evaluates math: '=2*pi*3'", "Hold Space for the radial menu, tap for this palette",
                              "G/R/S then X/Y/Z then a number: exact transforms without the mouse", "Ctrl+1..9 stores a selection, 1..9 recalls it", "Ctrl+Alt+1..9 stores a camera view, Alt+1..9 recalls it", "[ and ] walk siblings, Shift+[ parent, Shift+] child"};
        for (const char* tip : tips) m_results.push_back({Result::Kind::Help, tip, "", "", "", NULL_ENTITY, 1.0f});
        return;
    }
    if (mode == Mode::All || mode == Mode::Commands) {
        for (const Command& command : commands.all()) {
            if (!command.palette) continue;
            const std::string haystack = command.category + " " + command.title;
            float score = pattern.empty() ? 1.0f : std::max(fuzzyScore(pattern, command.title) * 1.2f, fuzzyScore(pattern, haystack));
            if (score <= 0.0f) continue;
            const uint64_t last = commands.lastUse(command.id);
            if (last > 0) score += 2.0f + static_cast<float>(std::min<uint32_t>(commands.uses(command.id), 20)) * 0.25f;
            if (pattern.empty() && last > 0) score += static_cast<float>(last) * 1e-3f + 50.0f;
            const bool enabled = !command.enabled || command.enabled();
            std::string title = command.title;
            if (command.checked) title += command.checked() ? "  (on)" : "  (off)";
            m_results.push_back({Result::Kind::Command, title, command.category, command.chords.empty() ? "" : chordLabel(command.chords.front()), command.id, NULL_ENTITY, enabled ? score : score * 0.3f, enabled});
        }
    }
    if ((mode == Mode::All && pattern.size() >= 2) || mode == Mode::Entities) {
        if (changed) {
            m_scanPattern = pattern;
            m_scanDone = pattern.empty();
        }
        scanEntities(ctx);
        for (const Result& r : m_entityResults) m_results.push_back(r);
    }
    std::stable_sort(m_results.begin(), m_results.end(), [](const Result& a, const Result& b) { return a.score > b.score; });
    if (m_results.size() > 200) m_results.resize(200);
}

void Palette::activate(EditorContext& ctx, CommandRegistry& commands, const Result& result) {
    switch (result.kind) {
        case Result::Kind::Command:
            m_open = false;
            if (!commands.run(result.commandId)) ctx.toast(std::format("'{}' is not available right now", result.title), ToastKind::Warning);
            break;
        case Result::Kind::Entity: {
            m_open = false;
            ctx.selection.set(result.entity);
            const glm::vec4 b = ctx.world.getWorldBounds(result.entity);
            flyTo(ctx, b.w > 0.0f ? b : glm::vec4(ctx.world.getWorldPosition(result.entity), 1.0f));
            break;
        }
        case Result::Kind::Calculator: {
            const std::string value = result.title.substr(2);
            ImGui::SetClipboardText(value.c_str());
            ctx.toast("Copied " + value, ToastKind::Success);
            m_open = false;
            break;
        }
        case Result::Kind::Suggestion: {
            m_open = false;
            const std::string value = &result == &m_results.front() ? trim(m_text) : result.title;
            if (m_prompt.accept && !value.empty()) m_prompt.accept(value);
            m_promptMode = false;
            break;
        }
        case Result::Kind::Help: m_text = result.title.find("> ") != std::string::npos ? ">" : ""; break;
    }
}

void Palette::draw(EditorContext& ctx, CommandRegistry& commands) {
    if (ctx.openPalette) {
        open(ctx, ctx.paletteSeed);
        ctx.openPalette = false;
        ctx.paletteSeed.clear();
    }
    if (!m_open) return;
    rebuild(ctx, commands);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(680.0f, viewport->WorkSize.x - 40.0f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - width) * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.14f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##palette", nullptr, flags);
    ImGui::SetWindowFocus();
    if (m_promptMode) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.3f, 1.0f), "%s", m_prompt.title.c_str());
    }
    if (m_focus || !ImGui::IsAnyItemActive()) {
        ImGui::SetKeyboardFocusHere();
        m_focus = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##query", m_promptMode ? m_prompt.hint.c_str() : "Search commands and entities     >commands  @entities  =math  ?tips", &m_text);
    ImGui::PopStyleVar();

    const int count = static_cast<int>(m_results.size());
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || (ImGui::IsKeyPressed(ImGuiKey_Tab) && !ImGui::GetIO().KeyShift)) {
        m_cursor = count > 0 ? (m_cursor + 1) % count : 0;
        m_scrollToCursor = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || (ImGui::IsKeyPressed(ImGuiKey_Tab) && ImGui::GetIO().KeyShift)) {
        m_cursor = count > 0 ? (m_cursor + count - 1) % count : 0;
        m_scrollToCursor = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) m_cursor = std::min(m_cursor + 8, std::max(count - 1, 0));
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) m_cursor = std::max(m_cursor - 8, 0);
    m_cursor = std::clamp(m_cursor, 0, std::max(count - 1, 0));

    const float rowHeight = ImGui::GetTextLineHeight() * 1.9f;
    const float listHeight = std::min(static_cast<float>(std::max(count, 1)) * rowHeight, rowHeight * 11.0f);
    ImGui::BeginChild("results", ImVec2(0.0f, listHeight + 4.0f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int clicked = -1;
    for (int i = 0; i < count; ++i) {
        const Result& r = m_results[i];
        ImGui::PushID(i);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##item", i == m_cursor, 0, ImVec2(w, rowHeight - ImGui::GetStyle().ItemSpacing.y))) clicked = i;
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x != 0.0f) m_cursor = i;
        if (i == m_cursor && m_scrollToCursor) {
            ImGui::SetScrollHereY(0.5f);
            m_scrollToCursor = false;
        }
        const float y = start.y + (rowHeight - ImGui::GetStyle().ItemSpacing.y - ImGui::GetFontSize()) * 0.5f;
        const int kind = static_cast<int>(r.kind);
        draw->AddCircleFilled(ImVec2(start.x + 10.0f, y + ImGui::GetFontSize() * 0.5f), 3.5f, kindColor(kind));
        draw->AddText(ImVec2(start.x + 22.0f, y), r.enabled ? IM_COL32(236, 238, 242, 255) : IM_COL32(120, 124, 132, 255), r.title.c_str());
        float right = start.x + w - 8.0f;
        if (!r.shortcut.empty()) {
            const ImVec2 size = ImGui::CalcTextSize(r.shortcut.c_str());
            const ImVec2 lo(right - size.x - 12.0f, y - 2.0f);
            draw->AddRectFilled(lo, ImVec2(right, y + size.y + 2.0f), IM_COL32(255, 255, 255, 18), 5.0f);
            draw->AddRect(lo, ImVec2(right, y + size.y + 2.0f), IM_COL32(255, 255, 255, 30), 5.0f);
            draw->AddText(ImVec2(lo.x + 6.0f, y), IM_COL32(210, 214, 222, 255), r.shortcut.c_str());
            right = lo.x - 10.0f;
        }
        if (!r.detail.empty()) {
            const ImVec2 size = ImGui::CalcTextSize(r.detail.c_str());
            const float titleEnd = start.x + 22.0f + ImGui::CalcTextSize(r.title.c_str()).x + 16.0f;
            if (right - size.x > titleEnd) draw->AddText(ImVec2(right - size.x, y), IM_COL32(120, 128, 140, 255), r.detail.c_str());
        }
        ImGui::PopID();
    }
    if (count == 0) ImGui::TextDisabled(m_scanDone ? "  No matches" : "  Searching...");
    ImGui::EndChild();
    if (!m_promptMode) {
        ImGui::TextDisabled("Enter run   Up/Down move   Esc close   %s", m_scanDone ? "" : "scanning entities...");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (clicked >= 0) {
        activate(ctx, commands, m_results[clicked]);
    } else if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && count > 0) {
        activate(ctx, commands, m_results[m_cursor]);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_open = false;
        m_promptMode = false;
    }
}
