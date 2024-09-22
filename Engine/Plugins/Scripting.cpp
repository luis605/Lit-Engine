#include "Scripting.h"

void initWindow(std::string& windowName, int width, int height, bool resizable) {
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Once);
    ImGui::Begin(windowName.c_str());
}

void closeWindow() {
    ImGui::End();
}