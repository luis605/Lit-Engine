#ifndef PLUGINS_SCRIPTING_H
#define PLUGINS_SCRIPTING_H

void initWindow(std::string& windowName, int width, int height, bool resizable);
void closeWindow();
void drawText(std::string& text, int x = -1, int y = -1, LitVector3 color = LitVector3(255.0f, 255.0f, 255.0f));
bool drawButton(std::string& text, int x = -1, int y = -1, int width = -1, int height = -1, LitVector3 buttonColor = LitVector3(-1, -1, -1), LitVector3 textColor = LitVector3(-1, -1, -1));
void setSkybox(const std::string& skyboxPath);
void onEntityCreation(const std::string& listenerName, const std::function<void()>& callback);
void onEntityDestruction(const std::string& listenerName, const std::function<void()>& callback);
void onSceneSave(const std::string& listenerName, const std::function<void()>& callback);
void onSceneLoad(const std::string& listenerName, const std::function<void()>& callback);
void onScenePlay(const std::string& listenerName, const std::function<void()>& callback);
void onSceneStop(const std::string& listenerName, const std::function<void()>& callback);
void createEvent(const std::string& eventName);
void onCustomEvent(const std::string& eventName, const std::function<void()>& callback);
void triggerCustomEvent(const std::string& eventName);

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


#endif // PLUGINS_SCRIPTING_H
