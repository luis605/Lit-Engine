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
    m.def("lerp", static_cast<float(*)(float, float, float)>(&lerp<float>), "Lerp function for float and double types");
    m.def("lerp", static_cast<int(*)(int, int, float)>(&lerp_int), "Lerp function for integer types");
    m.def("lerp", &lerp_Vector3, "Lerp function for Vector3 type");
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