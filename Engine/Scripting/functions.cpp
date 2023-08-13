// Custom implementation of clamp
template <typename T>
constexpr const T& custom_clamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : ((value > high) ? high : value);
}

// Lerp for floating-point types (float and double)
template <typename T>
T lerp(T start, T end, float t) {
    t = custom_clamp(t, 0.0f, 1.0f);
    return start + t * (end - start);
}

// Lerp for integer types (int)
int lerp_int(int start, int end, float t) {
    t = custom_clamp(t, 0.0f, 1.0f);
    return static_cast<int>(std::round(start + t * (end - start)));
}

// Lerp for Vector3
Vector3 lerp_Vector3(Vector3 start, Vector3 end, float t) {
    t = custom_clamp(t, 0.0f, 1.0f);
    return Vector3{
        lerp(start.x, end.x, t),
        lerp(start.y, end.y, t),
        lerp(start.z, end.z, t)
    };
}

PYBIND11_EMBEDDED_MODULE(math_module, m) {
    py::class_<Vector3>(m, "Vector3")
        .def(py::init<float, float, float>())
        .def_property("x",
            [](const Vector3 &v) { return static_cast<float>(v.x); },
            [](Vector3 &v, float value) { v.x = value; })
        .def_property("y",
            [](const Vector3 &v) { return static_cast<float>(v.y); },
            [](Vector3 &v, float value) { v.y = value; })
        .def_property("z",
            [](const Vector3 &v) { return static_cast<float>(v.z); },
            [](Vector3 &v, float value) { v.z = value; });


    
    py::class_<Vector2>(m, "Vector2")
        .def(py::init<float, float>())
        .def_readwrite("x", &Vector2::x, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("y", &Vector2::y, py::call_guard<py::gil_scoped_release>());

    m.def("Vector3Add", &Vector3Add, py::call_guard<py::gil_scoped_release>());
    m.def("Vector3Subtract", &Vector3Subtract, py::call_guard<py::gil_scoped_release>());
    m.def("Vector3Multiply", &Vector3Multiply, py::call_guard<py::gil_scoped_release>());
    m.def("Vector3Divide", &Vector3Divide, py::call_guard<py::gil_scoped_release>());
    m.def("Vector3Scale", &Vector3Scale, py::call_guard<py::gil_scoped_release>());
    m.def("Vector3CrossProduct", &Vector3CrossProduct, py::call_guard<py::gil_scoped_release>());
    m.def("lerp", static_cast<float(*)(float, float, float)>(&lerp<float>), "Lerp function for float and double types");
    m.def("lerp", static_cast<int(*)(int, int, float)>(&lerp_int), "Lerp function for integer types");
    m.def("lerp", &lerp_Vector3, "Lerp function for Vector3 type");
}


struct LitCamera : Camera3D {
    Vector3 front;

    // Constructor with default values
    LitCamera(Vector3 _position = Vector3{}, Vector3 _target = Vector3{},
              Vector3 _up = Vector3{}, float _fovy = 0.0f, int _projection = 0)
        : Camera3D{_position, _target, _up, _fovy, _projection},
          front{Vector3Subtract(_target, _position)} {}
};



Vector2 GetMouseMovement()
{
    static Vector2 lastMousePosition = { 0 };

    Vector2 mousePosition = GetMousePosition();
    Vector2 mouseMove = { mousePosition.x - lastMousePosition.x, mousePosition.y - lastMousePosition.y };

    lastMousePosition = mousePosition;
    
    return mouseMove;
}


PYBIND11_EMBEDDED_MODULE(input_module, m) {
    m.def("IsMouseButtonPressed", &IsMouseButtonPressed, py::call_guard<py::gil_scoped_release>());
    m.def("IsKeyDown", &IsKeyDown, py::call_guard<py::gil_scoped_release>());
    m.def("IsKeyUp", &IsKeyUp, py::call_guard<py::gil_scoped_release>());
    m.def("GetMousePosition", &GetMousePosition, py::call_guard<py::gil_scoped_release>());
    m.def("GetMouseMovement", &GetMouseMovement, py::call_guard<py::gil_scoped_release>());

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









HitInfo raycast(Vector3 origin, Vector3 direction, bool debug=false, std::vector<Entity> ignore = {});


PYBIND11_EMBEDDED_MODULE(collisions_module, m) {
    py::class_<HitInfo>(m, "HitInfo")
        .def(py::init<>())
        .def_readwrite("hit", &HitInfo::hit)
        .def_readwrite("worldPoint", &HitInfo::worldPoint, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("relativePoint", &HitInfo::relativePoint, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("worldNormal", &HitInfo::worldNormal, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("distance", &HitInfo::distance)
        .def_readwrite("hitColor", &HitInfo::hitColor)
        .def_readwrite("entity", &HitInfo::entity, py::call_guard<py::gil_scoped_release>());

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
        .def(py::init<float, float, float, float>())
        .def_readwrite("r", &Color::r, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("g", &Color::g, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("b", &Color::b, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("a", &Color::a, py::call_guard<py::gil_scoped_release>())
        .def("print", [](const Color& color) {
            printColor(color);
        });
}




PYBIND11_EMBEDDED_MODULE(camera_module, m) {
    py::class_<LitCamera>(m, "LitCamera")
        .def(py::init<Vector3, Vector3, Vector3, float, float>())
        .def_readwrite("position", &Camera3D::position, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("front", &LitCamera::front, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("target", &Camera3D::target, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("up", &Camera3D::up, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("fovy", &Camera3D::fovy, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("projection", &Camera3D::projection, py::call_guard<py::gil_scoped_release>());
}








class Time {
public:
    float dt;

    void update() {
        dt = GetFrameTime();
    }
};


PYBIND11_EMBEDDED_MODULE(time_module, m) {
    py::class_<Time>(m, "Time")
        .def(py::init<>())
        .def_readwrite("dt", &Time::dt);
}


Time time_instance;

void UpdateInGameGlobals()
{
    time_instance.update();
}