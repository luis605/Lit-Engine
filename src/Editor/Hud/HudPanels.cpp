module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "HudInternal.hpp"

module Editor.hud;

import Editor.context;
import Editor.commands;
import Engine.World;
import Engine.History;
import Engine.glm;

namespace {

constexpr float STEP_SPACING = 34.0f;
constexpr float TRACK_HEIGHT = 74.0f;
constexpr float STEP_BUTTON_SIZE = 18.0f;
constexpr float HELP_COLUMN_WIDTH = 330.0f;
constexpr float HELP_MAX_KEY_WIDTH = 150.0f;

using HelpRows = std::vector<std::pair<std::string, std::string>>;

struct HelpBlock {
    std::string title;
    HelpRows rows;
};

std::string stepLabel(History& history, size_t step, size_t undo, size_t total) {
    if (step == 0) return "start";
    return step <= undo ? history.undoLabel(step - 1) : history.redoLabel(total - step);
}

void travelToStep(EditorContext& ctx, size_t target) {
    History& history = *ctx.history;
    while (history.undoCount() > target && history.undo()) {
    }
    while (history.undoCount() < target && history.redo()) {
    }
    ctx.selection.prune(ctx.world);
}

int drawTimelineTrack(History& history, size_t undo, size_t total) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float y = start.y + 18.0f;
    drawList->AddLine(ImVec2(start.x + 10.0f, y), ImVec2(start.x + 10.0f + STEP_SPACING * static_cast<float>(total), y), IM_COL32(255, 255, 255, 40), 2.0f);
    int target = -1;
    for (size_t step = 0; step <= total; ++step) {
        const float centerX = start.x + 10.0f + STEP_SPACING * static_cast<float>(step);
        const bool current = step == undo;
        const bool future = step > undo;
        ImGui::SetCursorScreenPos(ImVec2(centerX - 9.0f, y - 9.0f));
        ImGui::PushID(static_cast<int>(step));
        if (ImGui::InvisibleButton("##step", ImVec2(STEP_BUTTON_SIZE, STEP_BUTTON_SIZE))) target = static_cast<int>(step);
        const bool hovered = ImGui::IsItemHovered();
        const std::string label = (current || hovered) ? stepLabel(history, step, undo, total) : std::string();
        if (hovered) ImGui::SetTooltip("%s", label.c_str());
        ImGui::PopID();
        const ImU32 color = current ? IM_COL32(255, 176, 66, 255) : future ? IM_COL32(120, 126, 140, 180) : IM_COL32(200, 205, 215, 255);
        drawList->AddCircleFilled(ImVec2(centerX, y), current ? 7.0f : hovered ? 6.0f : 4.5f, color, 16);
        if (current || hovered) {
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            drawList->AddText(ImVec2(centerX - size.x * 0.5f, y + 12.0f), color, label.c_str());
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 60.0f));
    ImGui::Dummy(ImVec2(STEP_SPACING * static_cast<float>(total) + 20.0f, 1.0f));
    return target;
}

std::vector<HelpBlock> collectHelpBlocks(const CommandRegistry& commands, const std::string& filter) {
    std::map<std::string, HelpRows> byCategory;
    for (const Command& command : commands.all()) {
        if (command.chords.empty() || !command.help) continue;
        if (!filter.empty() && fuzzyScore(filter, command.category + " " + command.title + " " + chordLabel(command.chords.front())) <= 0.0f) continue;
        std::string keys = command.keysLabel;
        for (size_t chordIndex = 0; keys.empty() && chordIndex < command.chords.size(); ++chordIndex) keys += (chordIndex ? "  " : "") + chordLabel(command.chords[chordIndex]);
        byCategory[command.category].emplace_back(keys, command.title);
    }
    std::vector<HelpBlock> blocks;
    blocks.reserve(byCategory.size() + 2);
    for (auto& [category, rows] : byCategory) blocks.push_back({category, std::move(rows)});
    if (filter.empty()) {
        blocks.push_back({"Mouse", {{"RMB + WASD/QE", "fly, wheel sets speed"}, {"Alt+LMB", "orbit"}, {"MMB", "pan"}, {"Wheel", "dolly to cursor"}, {"Click", "select; Shift adds, Ctrl toggles"}, {"Drag", "box select"}, {"Double-click", "fly to object"}, {"Hold Space", "radial menu"}}});
        blocks.push_back({"While transforming (G / R / S)", {{"X Y Z", "lock axis, again for local"}, {"Shift+X/Y/Z", "lock to plane"}, {"2*pi  -0.5", "type an exact value"}, {"Ctrl", "invert snapping"}, {"Enter / Click", "apply"}, {"Esc / RMB", "cancel"}}});
    }
    return blocks;
}

void drawHelpBlock(const HelpBlock& block) {
    ImGui::SeparatorText(block.title.c_str());
    float keyWidth = 0.0f;
    for (const auto& row : block.rows) keyWidth = std::max(keyWidth, ImGui::CalcTextSize(row.first.c_str()).x);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const auto& [keys, title] : block.rows) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float capWidth = drawKeyCap(drawList, at, keys, IM_COL32(255, 200, 110, 255));
        ImGui::SetCursorScreenPos(ImVec2(at.x + std::max(std::min(keyWidth, HELP_MAX_KEY_WIDTH) + 18.0f, capWidth + 10.0f), at.y));
        ImGui::TextUnformatted(title.c_str());
    }
    ImGui::Spacing();
}

void drawHelpColumns(const std::vector<HelpBlock>& blocks) {
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / HELP_COLUMN_WIDTH));
    std::vector<std::vector<const HelpBlock*>> layout(static_cast<size_t>(columns));
    std::vector<float> heights(static_cast<size_t>(columns), 0.0f);
    for (const HelpBlock& block : blocks) {
        const size_t shortest = static_cast<size_t>(std::min_element(heights.begin(), heights.end()) - heights.begin());
        layout[shortest].push_back(&block);
        heights[shortest] += static_cast<float>(block.rows.size()) + 2.0f;
    }
    if (!ImGui::BeginTable("keys", columns, ImGuiTableFlags_SizingStretchSame)) return;
    ImGui::TableNextRow();
    for (int column = 0; column < columns; ++column) {
        ImGui::TableSetColumnIndex(column);
        for (const HelpBlock* block : layout[static_cast<size_t>(column)]) drawHelpBlock(*block);
    }
    ImGui::EndTable();
}

}

void Hud::drawTimeline(EditorContext& ctx) {
    if (!ctx.showTimeline || ctx.zen) return;
    History& history = *ctx.history;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float barHeight = ImGui::GetFrameHeight() + 8.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y - barHeight - 6.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f), ImVec2(viewport->Size.x * 0.9f, 200.0f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("History", &ctx.showTimeline, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    const size_t undo = history.undoCount();
    const size_t total = undo + history.redoCount();
    if (total == 0) {
        ImGui::TextDisabled("Nothing to undo yet");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%zu steps  ·  click a step to travel  ·  Ctrl+Z / Ctrl+Shift+Z", total);
    const float trackWidth = std::min(static_cast<float>(total + 1) * STEP_SPACING + 40.0f, viewport->Size.x * 0.85f);
    ImGui::BeginChild("track", ImVec2(trackWidth, TRACK_HEIGHT), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const int target = drawTimelineTrack(history, undo, total);
    if (target >= 0) travelToStep(ctx, static_cast<size_t>(target));
    ImGui::EndChild();
    ImGui::End();
}

void Hud::drawHelp(EditorContext& ctx, CommandRegistry& commands) {
    if (!ctx.showHelp) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(1180.0f, viewport->Size.x - 60.0f), std::min(760.0f, viewport->Size.y - 80.0f)));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), IM_COL32(0, 0, 0, 110));
    if (!ImGui::Begin("Keyboard map   ·   F1 or Esc closes   ·   every entry is also in the Space palette", &ctx.showHelp, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter shortcuts", &m_helpFilter);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ctx.showHelp = false;
    drawHelpColumns(collectHelpBlocks(commands, m_helpFilter));
    ImGui::End();
}
