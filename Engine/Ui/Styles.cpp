#include "../../include_all.h"

void SetStyleHighContrast(ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    colors[ImGuiCol_Text] = ImVec4(0.0f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_FrameBg] = rgbaToImguiColor(181, 154, 0, 0.8);
    colors[ImGuiCol_Button] = rgbaToImguiColor(181, 154, 0, 0.8);


}


void SetStyleGray(ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    style->FrameRounding = 2.0f;
    style->FrameBorderSize = 1.0f;
    
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);


}