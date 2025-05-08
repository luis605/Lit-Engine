/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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