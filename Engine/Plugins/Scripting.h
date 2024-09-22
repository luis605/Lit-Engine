#ifndef PLUGINS_SCRIPTING_H
#define PLUGINS_SCRIPTING_H

void initWindow(std::string& windowName, int width, int height, bool resizable);
void closeWindow();

PYBIND11_EMBEDDED_MODULE(pluginScriptingModule, m) {
    m.def("initWindow", &initWindow, "Initialize a window with given parameters",
          pybind11::arg("windowName"), pybind11::arg("width"), pybind11::arg("height"), pybind11::arg("resizable"));
    m.def("closeWindow", &closeWindow, "Close the current window");
}

#endif PLUGINS_SCRIPTING_H