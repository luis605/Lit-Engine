module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Editor.palette;

import Editor.context;
import Editor.commands;
import Engine.engine;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr size_t ENTITY_SCAN_BUDGET = 400000;
constexpr size_t MAX_ENTITY_RESULTS = 60;
constexpr size_t ENTITY_RESULT_OVERFLOW_FACTOR = 4;
constexpr size_t MAX_RESULTS = 200;
constexpr float PALETTE_MAX_WIDTH = 680.0f;
constexpr float VISIBLE_ROWS = 11.0f;
constexpr int PAGE_STEP = 8;
constexpr float CALCULATOR_SCORE = 1000.0f;
constexpr float DISABLED_SCORE_FACTOR = 0.3f;
constexpr uint32_t MAX_USE_BONUS_COUNT = 20;
constexpr float USE_BONUS_PER_COUNT = 0.25f;
constexpr float RECENT_BONUS = 50.0f;
constexpr const char* FORCE_REBUILD_MARKER = "\x01";

enum class QueryMode { All, Commands, Entities, Calc, Help };

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

ImU32 kindColor(int kind) {
    switch (kind) {
        case 1: return IM_COL32(110, 200, 255, 255);
        case 2: return IM_COL32(140, 230, 140, 255);
        case 3: return IM_COL32(255, 200, 110, 255);
        default: return IM_COL32(255, 176, 66, 255);
    }
}

QueryMode modeFor(const std::string& text) {
    if (text.empty()) return QueryMode::All;
    switch (text[0]) {
        case '>': return QueryMode::Commands;
        case '@': return QueryMode::Entities;
        case '=': return QueryMode::Calc;
        case '?': return QueryMode::Help;
        default: return QueryMode::All;
    }
}

template <typename ResultList>
void sortByScore(ResultList& results, typename ResultList::iterator begin) {
    std::stable_sort(begin, results.end(), [](const auto& left, const auto& right) { return left.score > right.score; });
}

}

void Palette::open(EditorContext&, std::string seed) {
    m_open = true;
    m_focus = true;
    m_promptMode = false;
    m_text = std::move(seed);
    m_builtFor = FORCE_REBUILD_MARKER;
    m_cursor = 0;
}

void Palette::prompt(PromptRequest request) {
    m_prompt = std::move(request);
    m_promptMode = true;
    m_open = true;
    m_focus = true;
    m_text = m_prompt.initial;
    m_builtFor = FORCE_REBUILD_MARKER;
    m_cursor = 0;
}

void Palette::scanEntities(EditorContext& ctx) {
    if (m_scanDone) return;
    World& world = ctx.world;
    if (!m_scanPattern.empty() && m_scanPattern[0] == '#') {
        const long long wanted = std::atoll(m_scanPattern.c_str() + 1);
        const bool inRange = wanted >= 0 && wanted <= static_cast<long long>(std::numeric_limits<Entity>::max());
        const EntityHandle entity = inRange ? world.handleOf(static_cast<Entity>(wanted)) : NULL_ENTITY;
        if (!entity.isNull()) m_entityResults.push_back({Result::Kind::Entity, displayName(world, entity), meshLabel(ctx.engine, world.getMesh(entity)), "", "", entity, 100.0f});
        m_scanDone = true;
        return;
    }
    const size_t slots = world.database().transforms.size();
    size_t budget = ENTITY_SCAN_BUDGET;
    while (m_scanCursor < slots && budget-- > 0) {
        const EntityHandle entity = world.handleOf(m_scanCursor++);
        if (entity.isNull()) continue;
        const std::string& name = world.getName(entity);
        if (name.empty()) continue;
        const float score = fuzzyScore(m_scanPattern, name);
        if (score <= 0.0f) continue;
        const std::string& tag = world.getTag(entity);
        m_entityResults.push_back({Result::Kind::Entity, name, meshLabel(ctx.engine, world.getMesh(entity)) + (tag.empty() ? "" : "  #" + tag), "", "", entity, score * 0.9f});
        if (m_entityResults.size() > MAX_ENTITY_RESULTS * ENTITY_RESULT_OVERFLOW_FACTOR) {
            std::partial_sort(m_entityResults.begin(), m_entityResults.begin() + MAX_ENTITY_RESULTS, m_entityResults.end(), [](const Result& left, const Result& right) { return left.score > right.score; });
            m_entityResults.resize(MAX_ENTITY_RESULTS);
        }
    }
    if (m_scanCursor >= slots) m_scanDone = true;
}

void Palette::buildPromptResults(const std::string& text) {
    m_results.push_back({Result::Kind::Suggestion, text.empty() ? m_prompt.hint : text, "Enter to accept", "", "", NULL_ENTITY, CALCULATOR_SCORE});
    for (const std::string& suggestion : m_prompt.suggestions) {
        const float score = text.empty() ? 1.0f : fuzzyScore(text, suggestion);
        if (score > 0.0f && suggestion != text) m_results.push_back({Result::Kind::Suggestion, suggestion, "", "", "", NULL_ENTITY, score});
    }
    sortByScore(m_results, m_results.begin() + 1);
}

void Palette::appendCommandResults(CommandRegistry& commands, const std::string& pattern) {
    for (const Command& command : commands.all()) {
        if (!command.palette) continue;
        const std::string haystack = command.category + " " + command.title;
        float score = pattern.empty() ? 1.0f : std::max(fuzzyScore(pattern, command.title) * 1.2f, fuzzyScore(pattern, haystack));
        if (score <= 0.0f) continue;
        const uint64_t lastUse = commands.lastUse(command.id);
        if (lastUse > 0) score += 2.0f + static_cast<float>(std::min<uint32_t>(commands.uses(command.id), MAX_USE_BONUS_COUNT)) * USE_BONUS_PER_COUNT;
        if (pattern.empty() && lastUse > 0) score += static_cast<float>(lastUse) * 1e-3f + RECENT_BONUS;
        const bool enabled = !command.enabled || command.enabled();
        std::string title = command.title;
        if (command.checked) title += command.checked() ? "  (on)" : "  (off)";
        m_results.push_back({Result::Kind::Command, std::move(title), command.category, command.chords.empty() ? "" : chordLabel(command.chords.front()), command.id, NULL_ENTITY, enabled ? score : score * DISABLED_SCORE_FACTOR, enabled});
    }
}

void Palette::rebuild(EditorContext& ctx, CommandRegistry& commands) {
    const bool changed = m_builtFor != m_text;
    if (!changed && m_scanDone && m_resultsFresh) return;
    const std::string text = trim(m_text);
    if (changed) {
        m_builtFor = m_text;
        m_cursor = 0;
        m_entityResults.clear();
        m_scanCursor = 0;
        m_scanDone = true;
    }
    m_resultsFresh = true;
    m_results.clear();

    if (m_promptMode) {
        buildPromptResults(text);
        return;
    }

    const QueryMode mode = modeFor(text);
    const std::string pattern = mode == QueryMode::All ? text : trim(text.substr(std::min<size_t>(1, text.size())));

    if (mode == QueryMode::Calc || mode == QueryMode::All) {
        const std::optional<double> value = evaluateExpression(pattern);
        if (value && (mode == QueryMode::Calc || pattern.find_first_of("+-*/^(") != std::string::npos)) {
            m_results.push_back({Result::Kind::Calculator, std::format("= {:.6g}", *value), "Enter copies the result", "", "", NULL_ENTITY, CALCULATOR_SCORE});
        }
        if (mode == QueryMode::Calc) return;
    }
    if (mode == QueryMode::Help) {
        const char* tips[] = {"Type to fuzzy-search every command, e.g. 'dup' finds Duplicate", "> filters commands only, @ finds entities by name or #index", "= evaluates math: '=2*pi*3'", "Hold Space for the radial menu, tap for this palette",
                              "G/R/S then X/Y/Z then a number: exact transforms without the mouse", "Ctrl+1..9 stores a selection, 1..9 recalls it", "Ctrl+Alt+1..9 stores a camera view, Alt+1..9 recalls it", "[ and ] walk siblings, Shift+[ parent, Shift+] child"};
        for (const char* tip : tips) m_results.push_back({Result::Kind::Help, tip, "", "", "", NULL_ENTITY, 1.0f});
        return;
    }
    if (mode == QueryMode::All || mode == QueryMode::Commands) appendCommandResults(commands, pattern);
    if ((mode == QueryMode::All && pattern.size() >= 2) || mode == QueryMode::Entities) {
        if (changed) {
            m_scanPattern = pattern;
            m_scanDone = pattern.empty();
        }
        scanEntities(ctx);
        m_results.insert(m_results.end(), m_entityResults.begin(), m_entityResults.end());
    }
    sortByScore(m_results, m_results.begin());
    if (m_results.size() > MAX_RESULTS) m_results.resize(MAX_RESULTS);
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
            const glm::vec4 bounds = ctx.world.getWorldBounds(result.entity);
            flyTo(ctx, bounds.w > 0.0f ? bounds : glm::vec4(ctx.world.getWorldPosition(result.entity), 1.0f));
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
            const auto accept = m_prompt.accept;
            m_promptMode = false;
            if (accept && !value.empty()) accept(value);
            break;
        }
        case Result::Kind::Help:
            m_text = result.title.find("> ") != std::string::npos ? ">" : "";
            break;
    }
}

void Palette::handleNavigation() {
    const int count = static_cast<int>(m_results.size());
    const bool shift = ImGui::GetIO().KeyShift;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || (ImGui::IsKeyPressed(ImGuiKey_Tab) && !shift)) {
        m_cursor = count > 0 ? (m_cursor + 1) % count : 0;
        m_scrollToCursor = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || (ImGui::IsKeyPressed(ImGuiKey_Tab) && shift)) {
        m_cursor = count > 0 ? (m_cursor + count - 1) % count : 0;
        m_scrollToCursor = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) m_cursor = std::min(m_cursor + PAGE_STEP, std::max(count - 1, 0));
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) m_cursor = std::max(m_cursor - PAGE_STEP, 0);
    m_cursor = std::clamp(m_cursor, 0, std::max(count - 1, 0));
}

void Palette::drawResultRow(const Result& result, int index, float rowHeight, int& clicked) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGui::PushID(index);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float innerHeight = rowHeight - ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::Selectable("##item", index == m_cursor, 0, ImVec2(width, innerHeight))) clicked = index;
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x != 0.0f) m_cursor = index;
    if (index == m_cursor && m_scrollToCursor) {
        ImGui::SetScrollHereY(0.5f);
        m_scrollToCursor = false;
    }
    const float textY = start.y + (innerHeight - ImGui::GetFontSize()) * 0.5f;
    drawList->AddCircleFilled(ImVec2(start.x + 10.0f, textY + ImGui::GetFontSize() * 0.5f), 3.5f, kindColor(static_cast<int>(result.kind)));
    drawList->AddText(ImVec2(start.x + 22.0f, textY), result.enabled ? IM_COL32(236, 238, 242, 255) : IM_COL32(120, 124, 132, 255), result.title.c_str());
    float right = start.x + width - 8.0f;
    if (!result.shortcut.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(result.shortcut.c_str());
        const ImVec2 low(right - size.x - 12.0f, textY - 2.0f);
        const ImVec2 high(right, textY + size.y + 2.0f);
        drawList->AddRectFilled(low, high, IM_COL32(255, 255, 255, 18), 5.0f);
        drawList->AddRect(low, high, IM_COL32(255, 255, 255, 30), 5.0f);
        drawList->AddText(ImVec2(low.x + 6.0f, textY), IM_COL32(210, 214, 222, 255), result.shortcut.c_str());
        right = low.x - 10.0f;
    }
    if (!result.detail.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(result.detail.c_str());
        const float titleEnd = start.x + 22.0f + ImGui::CalcTextSize(result.title.c_str()).x + 16.0f;
        if (right - size.x > titleEnd) drawList->AddText(ImVec2(right - size.x, textY), IM_COL32(120, 128, 140, 255), result.detail.c_str());
    }
    ImGui::PopID();
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
    const float width = std::min(PALETTE_MAX_WIDTH, viewport->WorkSize.x - 40.0f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - width) * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.14f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##palette", nullptr, flags);
    ImGui::SetWindowFocus();
    if (m_promptMode) ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.3f, 1.0f), "%s", m_prompt.title.c_str());
    if (m_focus || !ImGui::IsAnyItemActive()) {
        ImGui::SetKeyboardFocusHere();
        m_focus = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##query", m_promptMode ? m_prompt.hint.c_str() : "Search commands and entities     >commands  @entities  =math  ?tips", &m_text);
    ImGui::PopStyleVar();

    handleNavigation();
    const int count = static_cast<int>(m_results.size());
    const float rowHeight = ImGui::GetTextLineHeight() * 1.9f;
    const float listHeight = std::min(static_cast<float>(std::max(count, 1)) * rowHeight, rowHeight * VISIBLE_ROWS);
    ImGui::BeginChild("results", ImVec2(0.0f, listHeight + 4.0f));
    int clicked = -1;
    for (int index = 0; index < count; ++index) drawResultRow(m_results[index], index, rowHeight, clicked);
    if (count == 0) ImGui::TextDisabled(m_scanDone ? "  No matches" : "  Searching...");
    ImGui::EndChild();
    if (!m_promptMode) ImGui::TextDisabled("Enter run   Up/Down move   Esc close   %s", m_scanDone ? "" : "scanning entities...");
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
