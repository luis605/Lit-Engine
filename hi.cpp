#include <pybind11/embed.h>

namespace py = pybind11;

bool IsMouseButtonPressed(int button) {
    // implementation
}

bool IsKeyDown(int key) {
    // implementation
}

PYBIND11_EMBEDDED_MODULE(input_module, m) {
    m.def("IsMouseButtonPressed", &IsMouseButtonPressed);
    m.def("IsKeyDown", &IsKeyDown);
}

int main() {
    py::scoped_interpreter guard{}; // start the interpreter
    py::module input_module = py::module::import("input_module");
    // rest of your code that uses the input_module
    return 0;
}