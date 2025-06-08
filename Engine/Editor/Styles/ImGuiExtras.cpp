/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <Engine/Editor/Styles/ImGuiExtras.hpp>
#include <extras/IconsFontAwesome6.h>

namespace ImGui {
    void DrawMessageBox(const char* message, MessageBoxType type) {
        static int lastFrame = -1;
        static int messageBoxID = 0;
        int currentFrame = ImGui::GetFrameCount();

        if (currentFrame != lastFrame) {
            lastFrame = currentFrame;
            messageBoxID = 0;
        }

        messageBoxID++;
        ImVec4 backgroundColor;
        ImVec4 textColor;
        const char* icon;

        if (type == MessageBoxType::Warning) {
            backgroundColor = ImVec4(0.25f, 0.20f, 0.05f, 1.0f); // Muted yellow
            textColor       = ImVec4(1.0f, 0.85f, 0.4f, 1.0f);   // Bright yellow
            icon            = ICON_FA_TRIANGLE_EXCLAMATION;
        } else { // Info
            backgroundColor = ImVec4(0.1f, 0.12f, 0.15f, 1.0f);  // Muted blue/gray
            textColor       = ImVec4(0.7f, 0.8f, 1.0f, 1.0f);    // Soft blue
            icon            = ICON_FA_CIRCLE_INFO;
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, backgroundColor);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);

        ImGui::PushID(messageBoxID);
        ImGui::BeginChild("MessageBox", ImVec2(0, 50), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiChildFlags_AutoResizeY);

        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::Text("%s", icon);
        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", message);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        ImGui::EndChild();
        ImGui::PopID();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    bool ToggleButton(const char* str_id, bool& v) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const float height = ImGui::GetFrameHeight();
        const float width = height * 1.55f;
        const float radius = height * 0.5f;
        const float ANIM_SPEED = 0.08f;
        bool clicked = false;

        ImGuiContext& g = *GImGui;

        ImGui::InvisibleButton(str_id, ImVec2(width, height));
        if (ImGui::IsItemClicked()) {
            v = !v;
            clicked = true;
        }

        float t = v ? 1.0f : 0.0f;
        if (g.LastActiveId == g.CurrentWindow->GetID(str_id)) {
            float t_anim = ImSaturate(g.LastActiveIdTimer / ANIM_SPEED);
            t = v ? t_anim : (1.0f - t_anim);
        }

        ImU32 col_bg;
        if (ImGui::IsItemHovered()) {
            col_bg = ImGui::GetColorU32(ImLerp(
                ImVec4(0.78f, 0.78f, 0.78f, 1.0f),
                ImVec4(0.34f, 0.73f, 0.34f, 1.0f), t));
        } else {
            col_bg = ImGui::GetColorU32(ImLerp(
                ImVec4(0.85f, 0.85f, 0.85f, 1.0f),
                ImVec4(0.26f, 0.83f, 0.26f, 1.0f), t));
        }

        draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), col_bg, radius);
        draw_list->AddCircleFilled(
            ImVec2(pos.x + radius + t * (width - 2 * radius), pos.y + radius),
            radius - 1.5f,
            IM_COL32(255, 255, 255, 255));

        return clicked;
    }


    void CenteredText(const char* label, const ImVec2& sizeArg) {
        ImGuiWindow* window = GetCurrentWindow();

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 labelSize = CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size =
            CalcItemSize(sizeArg, labelSize.x + style.FramePadding.x * 2.0f,
                        labelSize.y + style.FramePadding.y * 2.0f);

        const ImVec2 pos2 = ImVec2((pos.x + size.x), (pos.y + size.y));
        const ImRect bb(pos, pos2);

        ItemSize(size, style.FramePadding.y);

        const ImVec2 posMin = ImVec2((bb.Min.x + style.FramePadding.x),
                                    (bb.Min.y + style.FramePadding.y));
        const ImVec2 posMax = ImVec2((bb.Max.x - style.FramePadding.x),
                                    (bb.Max.y - style.FramePadding.y));

        RenderTextClipped(posMin, posMax, label, NULL, &labelSize,
                        style.ButtonTextAlign, &bb);
    }
} // namespace ImGui