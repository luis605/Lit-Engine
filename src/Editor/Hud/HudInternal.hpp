#pragma once

#include <imgui.h>

#include <string>

constexpr ImU32 DEFAULT_KEY_TEXT_COLOR = IM_COL32(230, 232, 238, 255);

inline float drawKeyCap(ImDrawList* drawList, ImVec2 at, const std::string& keys, ImU32 textColor) {
    const ImVec2 size = ImGui::CalcTextSize(keys.c_str());
    const ImVec2 low(at.x, at.y - 1.0f);
    const ImVec2 high(at.x + size.x + 10.0f, at.y + size.y + 1.0f);
    drawList->AddRectFilled(low, high, IM_COL32(255, 255, 255, 20), 4.0f);
    drawList->AddRect(low, high, IM_COL32(255, 255, 255, 36), 4.0f);
    drawList->AddText(ImVec2(at.x + 5.0f, at.y), textColor, keys.c_str());
    return high.x - at.x;
}
