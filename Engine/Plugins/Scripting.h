#ifndef PLUGINS_SCRIPTING_H
#define PLUGINS_SCRIPTING_H

void initWindow(std::string& windowName, int width, int height, bool resizable);
void closeWindow();
void drawText(std::string& text, int x = -1, int y = -1, LitVector3 color = LitVector3(255.0f, 255.0f, 255.0f));

PYBIND11_EMBEDDED_MODULE(pluginScriptingModule, m) {
    m.def("initWindow", &initWindow, "Initialize a window with given parameters",
          pybind11::arg("windowName"), pybind11::arg("width"), pybind11::arg("height"), pybind11::arg("resizable"));
    m.def("closeWindow", &closeWindow, "Close the current window");
    m.def("drawText", &drawText, "Draw text on the screen",
          pybind11::arg("text"),
          pybind11::arg("x") = -1,
          pybind11::arg("y") = -1,
          pybind11::arg("color") = LitVector3(255.0f, 255.0f, 255.0f));
}


#endif // PLUGINS_SCRIPTING_H
