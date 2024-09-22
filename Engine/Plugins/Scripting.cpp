#include "Scripting.h"

void initWindow(std::string& windowName, int width, int height, bool resizable) {
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Once);
    ImGui::Begin(windowName.c_str());
}

void closeWindow() {
    ImGui::End();
}

void drawText(std::string& text, int x, int y, LitVector3 color) {
    if (x >= 0 && y >= 0) ImGui::SetCursorPos(ImVec2(x, y));

    ImGui::TextColored(ImVec4(color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, 1.0f), text.c_str());
}
