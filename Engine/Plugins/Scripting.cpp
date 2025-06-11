/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Scripting.hpp"
#include <Engine/Core/Events.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <functional>
#include <imgui.h>
#include <raylib.h>
#include <string>

PYBIND11_EMBEDDED_MODULE(pluginScriptingModule, m) {
    m.def("initWindow", &initWindow, "Initialize a window with given parameters",
          pybind11::arg("windowName"), pybind11::arg("width"), pybind11::arg("height"), pybind11::arg("resizable"));

    m.def("closeWindow", &closeWindow, "Close the current window");

    m.def("drawText", &drawText, "Draw text on the screen",
          pybind11::arg("text"),
          pybind11::arg("x") = -1,
          pybind11::arg("y") = -1,
          pybind11::arg("color") = LitVector3(255.0f, 255.0f, 255.0f));

    m.def("drawButton", &drawButton, "Render a button on the screen",
          pybind11::arg("text"),
          pybind11::arg("x") = -1,
          pybind11::arg("y") = -1,
          pybind11::arg("width") = -1,
          pybind11::arg("height") = -1,
          pybind11::arg("buttonColor") = LitVector3(-1, -1, -1),
          pybind11::arg("textColor") = LitVector3(-1, -1, -1));

      m.def("setSkybox", [](const std::string& skyboxPath) {
            py::object sys = py::module_::import("sys");
            py::object os = py::module_::import("os");

            if (sys.attr("argv").is_none()) {
            TraceLog(LOG_ERROR, "Plugin Error: skybox path is not set");
            }
            py::object file = py::module_::import("main").attr("__file__");
            std::string relativePath = fs::path(file.cast<std::string>()).parent_path().string();
            std::string fullPath = relativePath + "/" + skyboxPath;

            setSkybox(fullPath);
      }, "Set the skybox of the scene", py::arg("skyboxPath"));

      m.def("onEntityCreation", &onEntityCreation,
            "Register a callback for entity creation events",
            py::arg("listenerName"),
            py::arg("callback"));

      m.def("onEntityDestruction", &onEntityDestruction,
            "Register a callback for entity destruction events",
            py::arg("listenerName"),
            py::arg("callback"));

      m.def("onSceneSave", &onSceneSave,
            "Register a callback for scene saving events",
            py::arg("listenerName"),
            py::arg("callback"));

      m.def("onSceneLoad", &onSceneLoad,
            "Register a callback for scene loading events",
            py::arg("listenerName"),
            py::arg("callback"));

      m.def("onScenePlay", &onScenePlay,
            "Register a callback for scene play events",
            py::arg("listenerName"),
            py::arg("callback"));

      m.def("onSceneStop", &onSceneStop,
            "Register a callback for scene stop events",
            py::arg("listenerName"),
            py::arg("callback"));

      m.def("createEvent", &createEvent,
          "Create a custom event",
          py::arg("eventName"));

      m.def("onCustomEvent", &onCustomEvent,
          "Register a callback for a custom event",
          py::arg("eventName"),
          py::arg("callback"));

      m.def("triggerCustomEvent", &triggerCustomEvent,
          "Trigger a custom event",
          py::arg("eventName"));

}

void initWindow(std::string& windowName, int width, int height,
                bool resizable) {
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Once);
    ImGui::Begin(windowName.c_str());
}

void closeWindow() { ImGui::End(); }

void drawText(std::string& text, int x, int y, LitVector3 color) {
    if (x >= 0 && y >= 0) ImGui::SetCursorPos(ImVec2(x, y));

    ImGui::TextColored(
        ImVec4(color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, 1.0f),
        "%s", text.c_str());
}

bool drawButton(std::string& text, int x, int y, int width, int height,
                LitVector3 buttonColor, LitVector3 textColor) {
    if (x >= 0 && y >= 0)
        ImGui::SetCursorPos(ImVec2(x, y));

    auto applyColor = [](ImGuiCol colorType, const LitVector3& color) {
        if (color.x >= 0 && color.y >= 0 && color.z >= 0) {
            ImGui::PushStyleColor(colorType,
                                  ImVec4(color.x / 255.0f, color.y / 255.0f,
                                         color.z / 255.0f, 1.0f));
            return true;
        }
        return false;
    };

    bool hasButtonColor = applyColor(ImGuiCol_Button, buttonColor);
    bool hasTextColor = applyColor(ImGuiCol_Text, textColor);

    bool clicked = (width >= 0 && height >= 0)
                       ? ImGui::Button(text.c_str(), ImVec2(width, height))
                       : ImGui::Button(text.c_str());

    if (hasButtonColor)
        ImGui::PopStyleColor();
    if (hasTextColor)
        ImGui::PopStyleColor();

    return clicked;
}

void setSkybox(const std::string& skyboxPath) {
    skybox.loadSkybox(fs::path(skyboxPath));
}

void onSceneSave(const std::string& listenerName,
                 const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onSceneSave.addListener(listener);
}

void onSceneLoad(const std::string& listenerName,
                 const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onSceneLoad.addListener(listener);
}

void onScenePlay(const std::string& listenerName,
                 const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onScenePlay.addListener(listener);
}

void onSceneStop(const std::string& listenerName,
                 const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onSceneStop.addListener(listener);
}