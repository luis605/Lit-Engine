#include "Scripting.h"
#include "../Lighting/skybox.h"

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

bool drawButton(std::string& text, int x, int y, int width, int height, LitVector3 buttonColor, LitVector3 textColor) {
    if (x >= 0 && y >= 0) ImGui::SetCursorPos(ImVec2(x, y));

    auto applyColor = [](ImGuiCol colorType, const LitVector3& color) {
        if (color.x >= 0 && color.y >= 0 && color.z >= 0) {
            ImGui::PushStyleColor(colorType, ImVec4(color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, 1.0f));
            return true;
        }
        return false;
    };

    bool hasButtonColor = applyColor(ImGuiCol_Button, buttonColor);
    bool hasTextColor = applyColor(ImGuiCol_Text, textColor);

    bool clicked = (width >= 0 && height >= 0)
                    ? ImGui::Button(text.c_str(), ImVec2(width, height))
                    : ImGui::Button(text.c_str());

    if (hasButtonColor) ImGui::PopStyleColor();
    if (hasTextColor) ImGui::PopStyleColor();

    return clicked;
}

void setSkybox(const std::string& skyboxPath) {
    skybox.loadSkybox(fs::path(skyboxPath));
}

void onEntityCreation(const std::string& listenerName, const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onEntityCreation.addListener(listener);
}

void onEntityDestruction(const std::string& listenerName, const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onEntityDestruction.addListener(listener);
}

void onSceneSave(const std::string& listenerName, const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onSceneSave.addListener(listener);
}

void onSceneLoad(const std::string& listenerName, const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onSceneLoad.addListener(listener);
}

void onScenePlay(const std::string& listenerName, const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onScenePlay.addListener(listener);
}

void onSceneStop(const std::string& listenerName, const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onSceneStop.addListener(listener);
}