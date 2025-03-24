#include "Core/Core.hpp"
#include <iostream>
#include <pybind11/embed.h>

namespace py = pybind11;

py::scoped_interpreter guard{};

int LitEngine() {
    Startup();
    std::cout << "Lit Engine Started\n\n";
    EngineMainLoop();
    CleanUp();

    return 0;
}

int main() {
    LitEngine();

    return 0;
}