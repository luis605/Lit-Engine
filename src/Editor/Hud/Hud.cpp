module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

module Editor.hud;

import Editor.context;
import Editor.commands;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.glm;

namespace {

float drawKeyCap(ImDrawList* draw, ImVec2 at, const std::string& keys, ImU32 text = IM_COL32(230, 232, 238, 255)) {
    const ImVec2 size = ImGui::CalcTextSize(keys.c_str());
    const ImVec2 lo(at.x, at.y - 1.0f);
    const ImVec2 hi(at.x + size.x + 10.0f, at.y + size.y + 1.0f);
    draw->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 20), 4.0f);
    draw->AddRect(lo, hi, IM_COL32(255, 255, 255, 36), 4.0f);
    draw->AddText(ImVec2(at.x + 5.0f, at.y), text, keys.c_str());
    return hi.x - at.x;
}

ImU32 toastColor(ToastKind kind) {
    switch (kind) {
        case ToastKind::Success: return IM_COL32(90, 220, 130, 255);
        case ToastKind::Warning: return IM_COL32(255, 196, 70, 255);
        case ToastKind::Error: return IM_COL32(255, 90, 90, 255);
        default: return IM_COL32(120, 180, 255, 255);
    }
}

}  // namespace

void Hud::drawStatusBar(EditorContext& ctx, CommandRegistry& commands, float frameMs) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight() + 8.0f;
    m_frameAverage += (frameMs - m_frameAverage) * 0.05f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##status", nullptr, flags);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float textY = origin.y + (ImGui::GetFrameHeight() - ImGui::GetFontSize()) * 0.5f;

    ImU32 modeColor = IM_COL32(255, 176, 66, 255);
    if (ctx.playing) modeColor = ctx.paused ? IM_COL32(255, 210, 80, 255) : IM_COL32(90, 220, 130, 255);
    if (ctx.modalActive) modeColor = IM_COL32(120, 190, 255, 255);
    std::string mode = ctx.modeLabel;
    if (ctx.playing && mode == "EDIT") mode = ctx.paused ? "PAUSED" : "PLAYING";
    const ImVec2 modeSize = ImGui::CalcTextSize(mode.c_str());
    draw->AddRectFilled(ImVec2(origin.x, textY - 2.0f), ImVec2(origin.x + modeSize.x + 16.0f, textY + modeSize.y + 2.0f), modeColor, 5.0f);
    draw->AddText(ImVec2(origin.x + 8.0f, textY), IM_COL32(20, 20, 24, 255), mode.c_str());
    float x = origin.x + modeSize.x + 28.0f;

    if (ctx.hints.empty()) {
        if (!ctx.selection.empty()) {
            ctx.hint("G R S", "transform");
            ctx.hint(commands.shortcut("edit.duplicate"), "duplicate");
            ctx.hint("F", "focus");
            ctx.hint("H", "hide");
            ctx.hint("Del", "delete");
            ctx.hint("Arrows", "nudge");
        } else {
            ctx.hint("Click", "select");
            ctx.hint("Shift+A", "add");
            ctx.hint("RMB", "fly");
            ctx.hint("Alt+LMB", "orbit");
        }
        ctx.hint("Space", "commands");
        ctx.hint("F1", "all keys");
    }
    const float statsWidth = 560.0f;
    const float hintLimit = origin.x + ImGui::GetContentRegionAvail().x - statsWidth;
    for (const auto& [keys, action] : ctx.hints) {
        const float needed = ImGui::CalcTextSize(keys.c_str()).x + ImGui::CalcTextSize(action.c_str()).x + 30.0f;
        if (x + needed > hintLimit) break;
        x += drawKeyCap(draw, ImVec2(x, textY), keys) + 5.0f;
        draw->AddText(ImVec2(x, textY), IM_COL32(150, 156, 168, 255), action.c_str());
        x += ImGui::CalcTextSize(action.c_str()).x + 16.0f;
    }

    const double gpu = ctx.engine.gpuTotalMilliseconds();
    const std::string stats = std::format("{} sel  ·  {} ent  ·  {:.1f} ms{}{}", ctx.selection.size(), ctx.world.aliveCount(), m_frameAverage, gpu > 0.0 ? std::format("  gpu {:.1f}", gpu) : std::string(), ctx.dirty() ? "  ·  unsaved" : "");
    const float statsX = origin.x + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(stats.c_str()).x;
    const size_t undo = ctx.history->undoCount();
    std::string historyLabel = undo > 0 ? std::format("Undo: {}", ctx.history->undoLabel(undo - 1)) : std::string("History");
    if (historyLabel.size() > 28) historyLabel = historyLabel.substr(0, 26) + "..";
    const std::string historyButton = std::format("{}  ({})###timeline", historyLabel, undo);
    const float buttonWidth = ImGui::CalcTextSize(historyButton.substr(0, historyButton.find("###")).c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(statsX - buttonWidth - 16.0f, origin.y + 2.0f));
    if (ImGui::SmallButton(historyButton.c_str())) ctx.showTimeline = !ctx.showTimeline;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("History timeline (Ctrl+H). Click a step to travel there.");
    draw->AddText(ImVec2(statsX, textY), IM_COL32(130, 136, 148, 255), stats.c_str());
    ImGui::End();
    ImGui::PopStyleVar(3);
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
    const size_t redo = history.redoCount();
    const size_t total = undo + redo;
    if (total == 0) {
        ImGui::TextDisabled("Nothing to undo yet");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%zu steps  ·  click a step to travel  ·  Ctrl+Z / Ctrl+Shift+Z", total);
    ImGui::BeginChild("track", ImVec2(std::min(static_cast<float>(total + 1) * 34.0f + 40.0f, viewport->Size.x * 0.85f), 74.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float spacing = 34.0f;
    const float y = start.y + 18.0f;
    draw->AddLine(ImVec2(start.x + 10.0f, y), ImVec2(start.x + 10.0f + spacing * static_cast<float>(total), y), IM_COL32(255, 255, 255, 40), 2.0f);
    int target = -1;
    for (size_t i = 0; i <= total; ++i) {
        const float cx = start.x + 10.0f + spacing * static_cast<float>(i);
        const bool current = i == undo;
        const bool future = i > undo;
        ImGui::SetCursorScreenPos(ImVec2(cx - 9.0f, y - 9.0f));
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##step", ImVec2(18.0f, 18.0f))) target = static_cast<int>(i);
        const bool hovered = ImGui::IsItemHovered();
        const std::string label = i == 0 ? std::string("start") : i <= undo ? history.undoLabel(i - 1) : history.redoLabel(total - i);
        if (hovered) ImGui::SetTooltip("%s", label.c_str());
        ImGui::PopID();
        const ImU32 color = current ? IM_COL32(255, 176, 66, 255) : future ? IM_COL32(120, 126, 140, 180) : IM_COL32(200, 205, 215, 255);
        draw->AddCircleFilled(ImVec2(cx, y), current ? 7.0f : hovered ? 6.0f : 4.5f, color, 16);
        if (current || hovered || i + 1 == total + 1 || (i % 4 == 0)) {
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            if (current || hovered) draw->AddText(ImVec2(cx - size.x * 0.5f, y + 12.0f), color, label.c_str());
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 60.0f));
    ImGui::Dummy(ImVec2(spacing * static_cast<float>(total) + 20.0f, 1.0f));
    if (target >= 0) {
        while (history.undoCount() > static_cast<size_t>(target) && history.undo()) {
        }
        while (history.undoCount() < static_cast<size_t>(target) && history.redo()) {
        }
        ctx.selection.prune(ctx.world);
    }
    ImGui::EndChild();
    ImGui::End();
}

void Hud::drawToasts(EditorContext& ctx, float dt) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float y = viewport->Pos.y + viewport->Size.y - ImGui::GetFrameHeight() - 22.0f;
    for (size_t i = ctx.toasts.size(); i-- > 0;) {
        Toast& t = ctx.toasts[i];
        t.age += dt;
        const float life = 2.8f;
        const float alpha = std::clamp(std::min(t.age * 8.0f, (life - t.age) * 3.0f), 0.0f, 1.0f);
        const ImVec2 size = ImGui::CalcTextSize(t.text.c_str());
        const float x = viewport->Pos.x + viewport->Size.x - size.x - 34.0f;
        y -= size.y + 16.0f;
        const float slide = (1.0f - std::min(t.age * 6.0f, 1.0f)) * 30.0f;
        const ImVec2 lo(x + slide, y);
        const ImVec2 hi(x + size.x + 22.0f + slide, y + size.y + 12.0f);
        draw->AddRectFilled(lo, hi, IM_COL32(22, 24, 30, static_cast<int>(235 * alpha)), 8.0f);
        const ImU32 accent = toastColor(t.kind);
        draw->AddRectFilled(lo, ImVec2(lo.x + 4.0f, hi.y), (accent & 0x00FFFFFF) | (static_cast<ImU32>(255 * alpha) << 24), 8.0f, ImDrawFlags_RoundCornersLeft);
        draw->AddText(ImVec2(lo.x + 13.0f, lo.y + 6.0f), IM_COL32(236, 238, 242, static_cast<int>(255 * alpha)), t.text.c_str());
        y -= 6.0f;
    }
    std::erase_if(ctx.toasts, [](const Toast& t) { return t.age > 2.8f; });
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

    std::map<std::string, std::vector<const Command*>> byCategory;
    for (const Command& command : commands.all()) {
        if (command.chords.empty() || !command.help) continue;
        if (!m_helpFilter.empty() && fuzzyScore(m_helpFilter, command.category + " " + command.title + " " + chordLabel(command.chords.front())) <= 0.0f) continue;
        byCategory[command.category].push_back(&command);
    }
    const float columnWidth = 360.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / columnWidth));
    if (ImGui::BeginTable("keys", columns, ImGuiTableFlags_SizingStretchSame)) {
        int column = 0;
        for (const auto& [category, list] : byCategory) {
            if (column == 0) ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(column);
            ImGui::SeparatorText(category.c_str());
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (const Command* command : list) {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                std::string keys = command->keysLabel;
                for (size_t i = 0; keys.empty() && i < command->chords.size(); ++i) keys += (i ? "  " : "") + chordLabel(command->chords[i]);
                if (keys.empty()) keys = command->keysLabel;
                drawKeyCap(draw, at, keys, IM_COL32(255, 200, 110, 255));
                ImGui::SetCursorScreenPos(ImVec2(at.x + ImGui::CalcTextSize(keys.c_str()).x + 18.0f, at.y));
                ImGui::TextUnformatted(command->title.c_str());
            }
            column = (column + 1) % columns;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SeparatorText("Mouse");
        ImGui::TextUnformatted("RMB hold + WASD/QE   fly (wheel = speed)");
        ImGui::TextUnformatted("Alt+LMB   orbit     MMB   pan     Wheel   dolly to cursor");
        ImGui::TextUnformatted("Click select, Shift add, Ctrl toggle, drag to box select");
        ImGui::TextUnformatted("Double-click an object to fly to it");
        ImGui::TextUnformatted("Hold Space: radial menu, release over an action");
        if (columns > 1) {
            ImGui::TableSetColumnIndex(1);
            ImGui::SeparatorText("While transforming (G / R / S)");
            ImGui::TextUnformatted("X Y Z  lock axis, press again for local");
            ImGui::TextUnformatted("Shift+X/Y/Z  lock to plane");
            ImGui::TextUnformatted("Type numbers or math: 2*pi, -0.5");
            ImGui::TextUnformatted("Ctrl  toggles snapping, Enter/Click apply, Esc/RMB cancel");
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

bool Hud::updatePie(EditorContext& ctx, CommandRegistry& commands, bool allowStart) {
    const ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    if (!m_pieArmed && !m_pieOpen) {
        if (allowStart && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            m_pieArmed = true;
            m_piePressTime = now;
            m_pieCenter = ctx.mouse;
        }
        return false;
    }
    if (m_pieArmed && !m_pieOpen) {
        if (!ImGui::IsKeyDown(ImGuiKey_Space)) {
            m_pieArmed = false;
            ctx.openPalette = true;
            return true;
        }
        if (now - m_piePressTime > 0.22) {
            m_pieOpen = true;
            m_pieCenter = ctx.mouse;
        } else {
            return true;
        }
    }
    const size_t count = m_pieItems.size();
    const float radius = 118.0f;
    const glm::vec2 offset = ctx.mouse - m_pieCenter;
    m_pieHover = -1;
    if (glm::length(offset) > 26.0f && count > 0) {
        float angle = std::atan2(offset.y, offset.x) + 3.14159265f * 0.5f;
        if (angle < 0.0f) angle += 6.2831853f;
        const float slice = 6.2831853f / static_cast<float>(count);
        m_pieHover = static_cast<int>(std::floor((angle + slice * 0.5f) / slice)) % static_cast<int>(count);
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 c(m_pieCenter.x, m_pieCenter.y);
    draw->AddCircleFilled(c, 24.0f, IM_COL32(20, 22, 28, 220), 32);
    draw->AddCircle(c, 24.0f, IM_COL32(255, 176, 66, 160), 32, 2.0f);
    for (size_t i = 0; i < count; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(count) * 6.2831853f - 3.14159265f * 0.5f;
        const ImVec2 p(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius);
        const bool hot = static_cast<int>(i) == m_pieHover;
        const std::string& label = m_pieItems[i].label;
        const std::string shortcut = commands.shortcut(m_pieItems[i].commandId);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        const ImVec2 lo(p.x - size.x * 0.5f - 12.0f, p.y - size.y * 0.5f - 7.0f);
        const ImVec2 hi(p.x + size.x * 0.5f + 12.0f, p.y + size.y * 0.5f + 7.0f);
        draw->AddRectFilled(lo, hi, hot ? IM_COL32(255, 176, 66, 240) : IM_COL32(26, 28, 34, 235), 10.0f);
        draw->AddRect(lo, hi, IM_COL32(255, 255, 255, hot ? 0 : 30), 10.0f);
        draw->AddText(ImVec2(p.x - size.x * 0.5f, p.y - size.y * 0.5f), hot ? IM_COL32(20, 20, 24, 255) : IM_COL32(236, 238, 242, 255), label.c_str());
        if (!shortcut.empty()) {
            const ImVec2 ss = ImGui::CalcTextSize(shortcut.c_str());
            draw->AddText(ImVec2(p.x - ss.x * 0.5f, hi.y + 2.0f), IM_COL32(150, 156, 168, 220), shortcut.c_str());
        }
    }
    if (m_pieHover >= 0) {
        const float a = static_cast<float>(m_pieHover) / static_cast<float>(count) * 6.2831853f - 3.14159265f * 0.5f;
        draw->AddLine(c, ImVec2(c.x + std::cos(a) * 60.0f, c.y + std::sin(a) * 60.0f), IM_COL32(255, 176, 66, 200), 3.0f);
    }
    ctx.modeLabel = "RADIAL";
    ctx.hints.clear();
    ctx.hint("Release", "run highlighted action");
    if (!ImGui::IsKeyDown(ImGuiKey_Space)) {
        m_pieOpen = false;
        m_pieArmed = false;
        if (m_pieHover >= 0) commands.run(m_pieItems[static_cast<size_t>(m_pieHover)].commandId);
    }
    return true;
}
