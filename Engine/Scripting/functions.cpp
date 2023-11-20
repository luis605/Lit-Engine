#pragma once

PYBIND11_EMBEDDED_MODULE(time_module, m) {
    py::class_<Time>(m, "Time")
        .def(py::init<>())
        .def_readwrite("dt", &Time::dt);
}





PYBIND11_EMBEDDED_MODULE(math_module, m) {
    py::class_<LitVector3>(m, "Vector3")
        .def(py::init<float, float, float>())
        .def_readwrite("x", &LitVector3::x, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("y", &LitVector3::y, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("z", &LitVector3::z, py::call_guard<py::gil_scoped_release>())
        .def("normalize", &LitVector3::normalize, py::call_guard<py::gil_scoped_release>())
        .def("CrossProduct", &LitVector3::CrossProduct, py::call_guard<py::gil_scoped_release>())
        .def("pos", &LitVector3::pos, py::call_guard<py::gil_scoped_release>())
        .def("__sub__", [](const LitVector3 &a, const LitVector3 &b) {
            return LitVector3(a.x - b.x, a.y - b.y, a.z - b.z);
        }, py::call_guard<py::gil_scoped_release>())
        .def("__add__", [](const LitVector3 &a, const LitVector3 &b) {
            return LitVector3(a.x + b.x, a.y + b.y, a.z + b.z);
        }, py::call_guard<py::gil_scoped_release>())
        .def("__mul__", [](const LitVector3 &a, const LitVector3 &b) {
            return a * b;
        }, py::call_guard<py::gil_scoped_release>())
        .def("__mul__", [](const LitVector3 &a, float scalar) {
            return a * scalar;
        }, py::call_guard<py::gil_scoped_release>())
        .def("__mul__", [](const LitVector3 &a, int scalar) {
            return a * scalar;
        }, py::call_guard<py::gil_scoped_release>())
        .def("__truediv__", [](const LitVector3 &a, const LitVector3 &b) {
            return LitVector3(a.x / b.x, a.y / b.y, a.z / b.z);
        }, py::call_guard<py::gil_scoped_release>())
        .def("__str__", [](const LitVector3 &a) {
            return std::to_string(a.x) + " " + std::to_string(a.y) + " " + std::to_string(a.z);
        }, py::call_guard<py::gil_scoped_release>());




    py::class_<Vector2>(m, "Vector2")
        .def(py::init<float, float>())
        .def_readwrite("x", &Vector2::x, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("y", &Vector2::y, py::call_guard<py::gil_scoped_release>())
        .def("__str__", [](const Vector2 &a) {
            return std::to_string(a.x) + " " + std::to_string(a.y);
        }, py::call_guard<py::gil_scoped_release>());

    m.def("Vector3Scale", &Vector3Scale, py::call_guard<py::gil_scoped_release>());
    m.def("lerp", static_cast<float(*)(float, float, float)>(&lerp<float>), "Lerp function for float and double types");
    m.def("lerp", static_cast<int(*)(int, int, float)>(&lerp_int), "Lerp function for integer types");
    m.def("lerp", &lerp_Vector3, "Lerp function for Vector3 type");
}


struct LitCamera : Camera3D {
    LitVector3 front;
    LitVector3 right;
    LitVector3 left;
    LitVector3 back;

    LitVector3 pos = LitVector3{};
    LitVector3 look_at = LitVector3{};
    LitVector3 up_vector = LitVector3{};

    LitCamera(LitVector3 pos = LitVector3{}, LitVector3 look_at = LitVector3{},
              LitVector3 up_vector = LitVector3{ 0.0f, 1.0f, 0.0f }, float _fovy = 0.0f, int _projection = 0)
        : Camera3D{},
          front{Vector3Subtract(look_at, pos)},
          up_vector{up_vector}
    {
        update();
    }

    void update() {
        position = pos;
        target = look_at;
        up = up_vector;
        front = Vector3Subtract(look_at, pos);
        right = Vector3Normalize(Vector3CrossProduct(front, up));
        left = Vector3Negate(right);
        back = Vector3Negate(front);
    }
};



PYBIND11_EMBEDDED_MODULE(camera_module, m) {
    py::class_<LitCamera>(m, "LitCamera")
        .def(py::init<LitVector3, LitVector3, LitVector3, float, float>())
        .def_readwrite("position", &LitCamera::pos, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("look_at", &LitCamera::look_at, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("up", &LitCamera::up_vector, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("fovy", &LitCamera::fovy, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("projection", &LitCamera::projection, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("front", &LitCamera::front, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("back", &LitCamera::back, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("left", &LitCamera::left, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("right", &LitCamera::right, py::call_guard<py::gil_scoped_release>());


}





Vector2 mouseMove;
float last_frame_count = 0;

Vector2 GetMouseMovement()
{
    static Vector2 lastMousePosition = { 0 };

    if (time_instance.dt - last_frame_count != 0)
    {
        Vector2 mousePosition = GetMousePosition();
        mouseMove = { mousePosition.x - lastMousePosition.x, mousePosition.y - lastMousePosition.y };

        lastMousePosition = mousePosition;
        last_frame_count = time_instance.dt;
    }
        
    return mouseMove;
}


PYBIND11_EMBEDDED_MODULE(input_module, m) {
    m.def("IsMouseButtonPressed", &IsMouseButtonPressed, py::call_guard<py::gil_scoped_release>());
    m.def("IsKeyDown", &IsKeyDown, py::call_guard<py::gil_scoped_release>());
    m.def("IsKeyUp", &IsKeyUp, py::call_guard<py::gil_scoped_release>());
    m.def("GetMousePosition", &GetMousePosition, py::call_guard<py::gil_scoped_release>());
    m.def("GetMouseMovement", &GetMouseMovement, py::call_guard<py::gil_scoped_release>());

    py::enum_<MouseButton>(m, "MouseButton")
        .value("MOUSE_BUTTON_LEFT", MOUSE_BUTTON_LEFT)
        .value("MOUSE_BUTTON_RIGHT", MOUSE_BUTTON_RIGHT)
        .value("MOUSE_BUTTON_MIDDLE", MOUSE_BUTTON_MIDDLE)
        .value("MOUSE_BUTTON_SIDE", MOUSE_BUTTON_SIDE)
        .value("MOUSE_BUTTON_EXTRA", MOUSE_BUTTON_EXTRA)
        .value("MOUSE_BUTTON_FORWARD", MOUSE_BUTTON_FORWARD)
        .value("MOUSE_BUTTON_BACK", MOUSE_BUTTON_BACK);

    py::enum_<KeyboardKey>(m, "KeyboardKey")
        .value("KEY_NULL", KEY_NULL)
        .value("KEY_APOSTROPHE", KEY_APOSTROPHE)
        .value("KEY_COMMA", KEY_COMMA)
        .value("KEY_MINUS", KEY_MINUS)
        .value("KEY_PERIOD", KEY_PERIOD)
        .value("KEY_SLASH", KEY_SLASH)
        .value("KEY_ZERO", KEY_ZERO)
        .value("KEY_ONE", KEY_ONE)
        .value("KEY_TWO", KEY_TWO)
        .value("KEY_THREE", KEY_THREE)
        .value("KEY_FOUR", KEY_FOUR)
        .value("KEY_FIVE", KEY_FIVE)
        .value("KEY_SIX", KEY_SIX)
        .value("KEY_SEVEN", KEY_SEVEN)
        .value("KEY_EIGHT", KEY_EIGHT)
        .value("KEY_NINE", KEY_NINE)
        .value("KEY_SEMICOLON", KEY_SEMICOLON)
        .value("KEY_EQUAL", KEY_EQUAL)
        .value("KEY_A", KEY_A)
        .value("KEY_B", KEY_B)
        .value("KEY_C", KEY_C)
        .value("KEY_D", KEY_D)
        .value("KEY_E", KEY_E)
        .value("KEY_F", KEY_F)
        .value("KEY_G", KEY_G)
        .value("KEY_H", KEY_H)
        .value("KEY_I", KEY_I)
        .value("KEY_J", KEY_J)
        .value("KEY_K", KEY_K)
        .value("KEY_L", KEY_L)
        .value("KEY_M", KEY_M)
        .value("KEY_N", KEY_N)
        .value("KEY_O", KEY_O)
        .value("KEY_P", KEY_P)
        .value("KEY_Q", KEY_Q)
        .value("KEY_R", KEY_R)
        .value("KEY_S", KEY_S)
        .value("KEY_T", KEY_T)
        .value("KEY_U", KEY_U)
        .value("KEY_V", KEY_V)
        .value("KEY_W", KEY_W)
        .value("KEY_X", KEY_X)
        .value("KEY_Y", KEY_Y)
        .value("KEY_Z", KEY_Z)
        .value("KEY_LEFT_BRACKET", KEY_LEFT_BRACKET)
        .value("KEY_BACKSLASH", KEY_BACKSLASH)
        .value("KEY_RIGHT_BRACKET", KEY_RIGHT_BRACKET)
        .value("KEY_GRAVE", KEY_GRAVE)
        .value("KEY_ESCAPE", KEY_ESCAPE)
        .value("KEY_ENTER", KEY_ENTER)
        .value("KEY_TAB", KEY_TAB)
        .value("KEY_BACKSPACE", KEY_BACKSPACE)
        .value("KEY_INSERT", KEY_INSERT)
        .value("KEY_DELETE", KEY_DELETE)
        .value("KEY_RIGHT", KEY_RIGHT)
        .value("KEY_LEFT", KEY_LEFT)
        .value("KEY_DOWN", KEY_DOWN)
        .value("KEY_UP", KEY_UP)
        .value("KEY_PAGE_UP", KEY_PAGE_UP)
        .value("KEY_PAGE_DOWN", KEY_PAGE_DOWN)
        .value("KEY_HOME", KEY_HOME)
        .value("KEY_END", KEY_END)
        .value("KEY_CAPS_LOCK", KEY_CAPS_LOCK)
        .value("KEY_SCROLL_LOCK", KEY_SCROLL_LOCK)
        .value("KEY_NUM_LOCK", KEY_NUM_LOCK)
        .value("KEY_PRINT_SCREEN", KEY_PRINT_SCREEN)
        .value("KEY_PAUSE", KEY_PAUSE)
        .value("KEY_F1", KEY_F1)
        .value("KEY_F2", KEY_F2)
        .value("KEY_F3", KEY_F3)
        .value("KEY_F4", KEY_F4)
        .value("KEY_F5", KEY_F5)
        .value("KEY_F6", KEY_F6)
        .value("KEY_F7", KEY_F7)
        .value("KEY_F8", KEY_F8)
        .value("KEY_F9", KEY_F9)
        .value("KEY_F10", KEY_F10)
        .value("KEY_F11", KEY_F11)
        .value("KEY_F12", KEY_F12)
        .value("KEY_LEFT_SHIFT", KEY_LEFT_SHIFT)
        .value("KEY_LEFT_CONTROL", KEY_LEFT_CONTROL)
        .value("KEY_LEFT_ALT", KEY_LEFT_ALT)
        .value("KEY_LEFT_SUPER", KEY_LEFT_SUPER)
        .value("KEY_RIGHT_SHIFT", KEY_RIGHT_SHIFT)
        .value("KEY_RIGHT_CONTROL", KEY_RIGHT_CONTROL)
        .value("KEY_RIGHT_ALT", KEY_RIGHT_ALT)
        .value("KEY_RIGHT_SUPER", KEY_RIGHT_SUPER)
        .value("KEY_KB_MENU", KEY_KB_MENU)
        .value("KEY_SPACE", KEY_SPACE);
}








HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug=false, std::vector<Entity> ignore = {});


PYBIND11_EMBEDDED_MODULE(collisions_module, m) {
    py::class_<HitInfo>(m, "HitInfo")
        .def(py::init<>())
        .def_readwrite("hit", &HitInfo::hit)
        .def_readwrite("worldPoint", &HitInfo::worldPoint, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("relativePoint", &HitInfo::relativePoint, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("worldNormal", &HitInfo::worldNormal, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("distance", &HitInfo::distance)
        .def_readwrite("hitColor", &HitInfo::hitColor)
        .def_property("entity", 
            [](const HitInfo& info) { return info.entity; }, // Getter
            [](HitInfo& info, const std::shared_ptr<Entity>& entity) { info.entity = entity; } // Setter
        );
        
    m.def("raycast", &raycast, py::arg("origin"), py::arg("direction"), py::arg("debug") = false, py::arg("ignore") = std::vector<Entity>(), py::call_guard<py::gil_scoped_release>());
}


string colorToString(const Color& color) {
  stringstream ss;
  ss << "(" << (int)color.r << ", " << (int)color.g << ", " << (int)color.b << ", " << (int)color.a << ")";
  return ss.str();
}


void printColor(const Color& color) {
  string color_text = colorToString(color);
  std::cout << color_text.c_str() << std::endl;
}

PYBIND11_EMBEDDED_MODULE(color_module, m) {
    py::class_<Color>(m, "Color")
        .def(py::init<unsigned char, unsigned char, unsigned char, unsigned char>())
        .def_readwrite("r", &Color::r, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("g", &Color::g, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("b", &Color::b, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("a", &Color::a, py::call_guard<py::gil_scoped_release>())
        .def("print", [](const Color& color) {
            printColor(color);
        });
}
