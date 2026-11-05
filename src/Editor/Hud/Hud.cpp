module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "HudInternal.hpp"

module Editor.hud;

import Editor.context;
import Editor.commands;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.glm;

namespace {

constexpr float STATUS_BAR_PADDING = 8.0f;
constexpr float STATUS_BAR_ALPHA = 0.88f;
constexpr float FRAME_AVERAGE_SMOOTHING = 0.05f;
constexpr float STATS_RESERVED_WIDTH = 560.0f;
constexpr float HINT_EXTRA_WIDTH = 30.0f;
constexpr size_t MAX_HISTORY_LABEL = 28;
constexpr size_t TRUNCATED_HISTORY_LABEL = 26;
constexpr float TOAST_LIFETIME = 2.8f;
constexpr float TOAST_FADE_IN_RATE = 8.0f;
constexpr float TOAST_FADE_OUT_RATE = 3.0f;
constexpr float TOAST_SLIDE_RATE = 6.0f;
constexpr float TOAST_SLIDE_DISTANCE = 30.0f;

ImU32 toastColor(ToastKind kind) {
    switch (kind) {
        case ToastKind::Success: return IM_COL32(90, 220, 130, 255);
        case ToastKind::Warning: return IM_COL32(255, 196, 70, 255);
        case ToastKind::Error: return IM_COL32(255, 90, 90, 255);
        default: return IM_COL32(120, 180, 255, 255);
    }
}

ImU32 modeColorFor(const EditorContext& ctx) {
    if (ctx.modalActive) return IM_COL32(120, 190, 255, 255);
    if (ctx.playing) return ctx.paused ? IM_COL32(255, 210, 80, 255) : IM_COL32(90, 220, 130, 255);
    return IM_COL32(255, 176, 66, 255);
}

void addDefaultHints(EditorContext& ctx, CommandRegistry& commands) {
    if (!ctx.hints.empty()) return;
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

std::string buildStats(EditorContext& ctx, float frameAverage) {
    const double gpu = ctx.engine.gpuTotalMilliseconds();
    const std::string gpuText = gpu > 0.0 ? std::format("  gpu {:.1f}", gpu) : std::string();
    return std::format("{} sel  ·  {} ent  ·  {:.1f} ms{}{}", ctx.selection.size(), ctx.world.aliveCount(), frameAverage, gpuText, ctx.dirty() ? "  ·  unsaved" : "");
}

void drawHistoryButton(EditorContext& ctx, float statsX, float originY) {
    const size_t undo = ctx.history->undoCount();
    std::string label = undo > 0 ? std::format("Undo: {}", ctx.history->undoLabel(undo - 1)) : std::string("History");
    if (label.size() > MAX_HISTORY_LABEL) label = label.substr(0, TRUNCATED_HISTORY_LABEL) + "..";
    const std::string visibleText = std::format("{}  ({})", label, undo);
    const std::string buttonId = visibleText + "###timeline";
    const float buttonWidth = ImGui::CalcTextSize(visibleText.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(statsX - buttonWidth - 16.0f, originY + 2.0f));
    if (ImGui::SmallButton(buttonId.c_str())) ctx.showTimeline = !ctx.showTimeline;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("History timeline (Ctrl+H). Click a step to travel there.");
}

}

void Hud::drawStatusBar(EditorContext& ctx, CommandRegistry& commands, float frameMs) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight() + STATUS_BAR_PADDING;
    m_frameAverage += (frameMs - m_frameAverage) * FRAME_AVERAGE_SMOOTHING;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
    ImGui::SetNextWindowBgAlpha(STATUS_BAR_ALPHA);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##status", nullptr, flags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float textY = origin.y + (ImGui::GetFrameHeight() - ImGui::GetFontSize()) * 0.5f;

    std::string mode = ctx.modeLabel;
    if (ctx.playing && mode == "EDIT") mode = ctx.paused ? "PAUSED" : "PLAYING";
    const ImVec2 modeSize = ImGui::CalcTextSize(mode.c_str());
    drawList->AddRectFilled(ImVec2(origin.x, textY - 2.0f), ImVec2(origin.x + modeSize.x + 16.0f, textY + modeSize.y + 2.0f), modeColorFor(ctx), 5.0f);
    drawList->AddText(ImVec2(origin.x + 8.0f, textY), IM_COL32(20, 20, 24, 255), mode.c_str());
    float cursorX = origin.x + modeSize.x + 28.0f;

    addDefaultHints(ctx, commands);
    const float hintLimit = origin.x + ImGui::GetContentRegionAvail().x - STATS_RESERVED_WIDTH;
    for (const auto& [keys, action] : ctx.hints) {
        const float actionWidth = ImGui::CalcTextSize(action.c_str()).x;
        const float needed = ImGui::CalcTextSize(keys.c_str()).x + actionWidth + HINT_EXTRA_WIDTH;
        if (cursorX + needed > hintLimit) break;
        cursorX += drawKeyCap(drawList, ImVec2(cursorX, textY), keys, DEFAULT_KEY_TEXT_COLOR) + 5.0f;
        drawList->AddText(ImVec2(cursorX, textY), IM_COL32(150, 156, 168, 255), action.c_str());
        cursorX += actionWidth + 16.0f;
    }

    const std::string stats = buildStats(ctx, m_frameAverage);
    const float statsX = origin.x + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(stats.c_str()).x;
    drawHistoryButton(ctx, statsX, origin.y);
    drawList->AddText(ImVec2(statsX, textY), IM_COL32(130, 136, 148, 255), stats.c_str());
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void Hud::drawToasts(EditorContext& ctx, float dt) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    float y = viewport->Pos.y + viewport->Size.y - ImGui::GetFrameHeight() - 22.0f;
    for (size_t index = ctx.toasts.size(); index-- > 0;) {
        Toast& toast = ctx.toasts[index];
        toast.age += dt;
        const float alpha = std::clamp(std::min(toast.age * TOAST_FADE_IN_RATE, (TOAST_LIFETIME - toast.age) * TOAST_FADE_OUT_RATE), 0.0f, 1.0f);
        const ImVec2 size = ImGui::CalcTextSize(toast.text.c_str());
        const float x = viewport->Pos.x + viewport->Size.x - size.x - 34.0f;
        y -= size.y + 16.0f;
        const float slide = (1.0f - std::min(toast.age * TOAST_SLIDE_RATE, 1.0f)) * TOAST_SLIDE_DISTANCE;
        const ImVec2 low(x + slide, y);
        const ImVec2 high(x + size.x + 22.0f + slide, y + size.y + 12.0f);
        drawList->AddRectFilled(low, high, IM_COL32(22, 24, 30, static_cast<int>(235 * alpha)), 8.0f);
        const ImU32 accent = (toastColor(toast.kind) & 0x00FFFFFF) | (static_cast<ImU32>(255 * alpha) << 24);
        drawList->AddRectFilled(low, ImVec2(low.x + 4.0f, high.y), accent, 8.0f, ImDrawFlags_RoundCornersLeft);
        drawList->AddText(ImVec2(low.x + 13.0f, low.y + 6.0f), IM_COL32(236, 238, 242, static_cast<int>(255 * alpha)), toast.text.c_str());
        y -= 6.0f;
    }
    std::erase_if(ctx.toasts, [](const Toast& toast) { return toast.age > TOAST_LIFETIME; });
}
