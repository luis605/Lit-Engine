module;

#include <imgui.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

module Editor.hud;

import Editor.context;
import Editor.commands;
import Engine.glm;

namespace {

constexpr float PI = 3.14159265f;
constexpr float TWO_PI = 6.2831853f;
constexpr double HOLD_THRESHOLD_SECONDS = 0.22;
constexpr float PIE_RADIUS = 118.0f;
constexpr float PIE_DEAD_ZONE = 26.0f;
constexpr float PIE_HUB_RADIUS = 24.0f;
constexpr float PIE_POINTER_LENGTH = 60.0f;

float sliceAngle(size_t index, size_t count) { return static_cast<float>(index) / static_cast<float>(count) * TWO_PI - PI * 0.5f; }

int hoveredSlice(const glm::vec2& offset, size_t count) {
    if (glm::length(offset) <= PIE_DEAD_ZONE || count == 0) return -1;
    float angle = std::atan2(offset.y, offset.x) + PI * 0.5f;
    if (angle < 0.0f) angle += TWO_PI;
    const float slice = TWO_PI / static_cast<float>(count);
    return static_cast<int>(std::floor((angle + slice * 0.5f) / slice)) % static_cast<int>(count);
}

void drawPieItem(ImDrawList* drawList, const ImVec2& center, size_t index, size_t count, bool hot, const PieItem& item, const std::string& shortcut) {
    const float angle = sliceAngle(index, count);
    const ImVec2 point(center.x + std::cos(angle) * PIE_RADIUS, center.y + std::sin(angle) * PIE_RADIUS);
    const ImVec2 size = ImGui::CalcTextSize(item.label.c_str());
    const ImVec2 low(point.x - size.x * 0.5f - 12.0f, point.y - size.y * 0.5f - 7.0f);
    const ImVec2 high(point.x + size.x * 0.5f + 12.0f, point.y + size.y * 0.5f + 7.0f);
    drawList->AddRectFilled(low, high, hot ? IM_COL32(255, 176, 66, 240) : IM_COL32(26, 28, 34, 235), 10.0f);
    drawList->AddRect(low, high, IM_COL32(255, 255, 255, hot ? 0 : 30), 10.0f);
    drawList->AddText(ImVec2(point.x - size.x * 0.5f, point.y - size.y * 0.5f), hot ? IM_COL32(20, 20, 24, 255) : IM_COL32(236, 238, 242, 255), item.label.c_str());
    if (shortcut.empty()) return;
    const ImVec2 shortcutSize = ImGui::CalcTextSize(shortcut.c_str());
    drawList->AddText(ImVec2(point.x - shortcutSize.x * 0.5f, high.y + 2.0f), IM_COL32(150, 156, 168, 220), shortcut.c_str());
}

}

std::string Hud::pieLabels() const {
    std::string labels;
    for (const PieItem& item : m_pieItems) labels += (labels.empty() ? "" : ",") + item.label;
    return labels;
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
        if (now - m_piePressTime <= HOLD_THRESHOLD_SECONDS) return true;
        m_pieOpen = true;
        m_pieCenter = ctx.mouse;
        m_pieItems = ctx.selection.empty() ? m_emptyItems : m_selectedItems;
    }
    const size_t count = m_pieItems.size();
    m_pieHover = hoveredSlice(ctx.mouse - m_pieCenter, count);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 center(m_pieCenter.x, m_pieCenter.y);
    drawList->AddCircleFilled(center, PIE_HUB_RADIUS, IM_COL32(20, 22, 28, 220), 32);
    drawList->AddCircle(center, PIE_HUB_RADIUS, IM_COL32(255, 176, 66, 160), 32, 2.0f);
    for (size_t index = 0; index < count; ++index) {
        drawPieItem(drawList, center, index, count, static_cast<int>(index) == m_pieHover, m_pieItems[index], commands.shortcut(m_pieItems[index].commandId));
    }
    if (m_pieHover >= 0) {
        const float angle = sliceAngle(static_cast<size_t>(m_pieHover), count);
        drawList->AddLine(center, ImVec2(center.x + std::cos(angle) * PIE_POINTER_LENGTH, center.y + std::sin(angle) * PIE_POINTER_LENGTH), IM_COL32(255, 176, 66, 200), 3.0f);
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
