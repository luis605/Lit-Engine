#include <Engine/Scripting/functions.hpp>
#include <btBulletDynamicsCommon.h>
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <raylib.h>

class PhysicsManager;
struct HitInfo;
class Entity;

#include <Engine/Core/Entity.hpp>
#include <Engine/Core/Raycast.hpp>
#include <Engine/Scripting/math.hpp>
#include <Engine/Scripting/time.hpp>

namespace py = pybind11;

class PhysicsManager;
struct HitInfo;

Vector2 mouseMove;
float lastFrameCount = 0.0f;

void LitCamera::update() {
    position = pos;
    target = look_at;
    up = up_vector;
    calculateVectors();
}

void LitCamera::calculateVectors() {
    front = Vector3Normalize(Vector3Subtract(target, position));
    right = Vector3Normalize(Vector3CrossProduct(front, up));
    left = Vector3Negate(right);
    back = Vector3Negate(front);
}

LitCamera camera;

PYBIND11_EMBEDDED_MODULE(timeModule, m) {
    py::class_<Time>(m, "Time")
        .def(py::init<>())
        .def_readwrite("dt", &Time::dt);
}

PYBIND11_EMBEDDED_MODULE(mathModule, m) {
    py::class_<LitVector3>(m, "Vector3")
        .def(py::init<float, float, float>())
        .def_property(
            "x", [](LitVector3& position) { return position.x; },
            [](LitVector3& position, float value) { position.x = value; })
        .def_property(
            "y", [](LitVector3& position) { return position.y; },
            [](LitVector3& position, float value) { position.y = value; })
        .def_property(
            "z", [](LitVector3& position) { return position.z; },
            [](LitVector3& position, float value) { position.z = value; })
        .def("__repr__",
             [](LitVector3& position) {
                 return "(" + std::to_string(position.x) + ", " +
                        std::to_string(position.y) + ", " +
                        std::to_string(position.z) + ")";
             })

        .def("normalized", &LitVector3::normalized)
        .def("lengthSquared", &LitVector3::lengthSquared)
        .def("crossProduct", &LitVector3::CrossProduct)
        .def("pos", &LitVector3::pos)
        .def("__sub__",
             [](const LitVector3& a, const LitVector3& b) {
                 return LitVector3(a.x - b.x, a.y - b.y, a.z - b.z);
             })
        .def("__add__",
             [](const LitVector3& a, const LitVector3& b) {
                 return LitVector3(a.x + b.x, a.y + b.y, a.z + b.z);
             })
        .def("__mul__",
             [](const LitVector3& a, const LitVector3& b) { return a * b; })
        .def("__mul__",
             [](const LitVector3& a, float scalar) { return a * scalar; })
        .def("__mul__",
             [](const LitVector3& a, int scalar) { return a * scalar; })
        .def("__truediv__",
             [](const LitVector3& a, const LitVector3& b) {
                 return LitVector3(a.x / b.x, a.y / b.y, a.z / b.z);
             })
        .def("__truediv__",
             [](const LitVector3& a, float scalar) {
                 return LitVector3(a.x / scalar, a.y / scalar, a.z / scalar);
             })
        .def("__truediv__",
             [](const LitVector3& a, int scalar) {
                 return LitVector3(a.x / scalar, a.y / scalar, a.z / scalar);
             })
        .def("__str__", [](const LitVector3& a) {
            return std::to_string(a.x) + " " + std::to_string(a.y) + " " +
                   std::to_string(a.z);
        });

    py::class_<Vector2>(m, "Vector2")
        .def(py::init<float, float>())
        .def_readwrite("x", &Vector2::x)
        .def_readwrite("y", &Vector2::y)
        .def("__str__", [](const Vector2& a) {
            return std::to_string(a.x) + " " + std::to_string(a.y);
        });

    m.def("vector3Scale", &LitVector3Scale);
    m.def("vector3Length", &LitVector3Length);
    m.def("vector3LengthSqr", &LitVector3LengthSqr);
    m.def("vector3Distance", &LitVector3Distance);
    m.def("lerp", static_cast<float (*)(float, float, float)>(&LitLerp<float>),
          "Lerp function for float and double types");
    m.def("lerp", static_cast<int (*)(int, int, float)>(&LitLerpInt),
          "Lerp function for integer types");
    m.def("lerp", &lerpVector3, "Lerp function for Vector3 type");
}

PYBIND11_EMBEDDED_MODULE(mouseModule, m) {
    m.def("LockMouse", &DisableCursor);
    m.def("UnlockMouse", &EnableCursor);
}

PYBIND11_EMBEDDED_MODULE(physicsModule, m) {
    py::class_<PhysicsManager>(m, "physics")
        .def_property(
            "gravity",
            [](const PhysicsManager& physics) { return physics.gravity; },
            [](PhysicsManager& physics, const LitVector3& gravity) {
                physics.SetGravity(gravity);
            });
}

PYBIND11_EMBEDDED_MODULE(cameraModule, m) {
    py::class_<LitCamera>(m, "LitCamera")
        .def(py::init<LitVector3, LitVector3, LitVector3, float, float>())
        .def_readwrite("position", &LitCamera::pos)
        .def_readwrite("look_at", &LitCamera::look_at)
        .def_readwrite("up", &LitCamera::up_vector)
        .def_readwrite("fovy", &LitCamera::fovy)
        .def_readwrite("projection", &LitCamera::projection)
        .def_readwrite("front", &LitCamera::front)
        .def_readwrite("back", &LitCamera::back)
        .def_readwrite("left", &LitCamera::left)
        .def_readwrite("right", &LitCamera::right);
}

pybind11::object exportCamera() {
    pybind11::module_ m = pybind11::module_::import("raylib_camera");
    pybind11::class_<LitCamera>(m, "LitCamera")
        .def(pybind11::init<>())
        .def_readwrite("position", &LitCamera::position)
        .def_readwrite("front", &LitCamera::front)
        .def_readwrite("target", &LitCamera::target)
        .def_readwrite("up", &LitCamera::up)
        .def_readwrite("fovy", &LitCamera::fovy)
        .def_readwrite("projection", &LitCamera::projection);

    LitCamera camera;
    pybind11::object camera_obj = pybind11::cast(camera);

    return camera_obj;
}

PYBIND11_EMBEDDED_MODULE(inputModule, m) {
    m.def("isMouseButtonPressed", &IsMouseButtonPressed);
    m.def("isKeyDown", &IsKeyDown);
    m.def("isKeyPressed", &IsKeyPressed);
    m.def("isKeyUp", &IsKeyUp);
    m.def("getMousePosition", &GetMousePosition);
    m.def("getMouseMovement", &GetMouseMovement);

    py::enum_<MouseButton>(m, "MouseButton")
        .value("MOUSE_BUTTON_LEFT", MOUSE_BUTTON_LEFT)
        .value("MOUSE_BUTTON_RIGHT", MOUSE_BUTTON_RIGHT)
        .value("MOUSE_BUTTON_MIDDLE", MOUSE_BUTTON_MIDDLE)
        .value("MOUSE_BUTTON_SIDE", MOUSE_BUTTON_SIDE)
        .value("MOUSE_BUTTON_EXTRA", MOUSE_BUTTON_EXTRA)
        .value("MOUSE_BUTTON_FORWARD", MOUSE_BUTTON_FORWARD)
        .value("MOUSE_BUTTON_BACK", MOUSE_BUTTON_BACK);

    py::enum_<KeyboardKey>(m, "Keys")
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

PYBIND11_EMBEDDED_MODULE(collisionModule, m) {
    py::enum_<CollisionShapeType>(m, "CollisionShape")
        .value("Box", CollisionShapeType::Box)
        .value("HighPolyMesh", CollisionShapeType::HighPolyMesh)
        .value("None", CollisionShapeType::None);

    py::class_<HitInfo>(m, "HitInfo")
        .def(py::init<>())
        .def_readwrite("hit", &HitInfo::hit)
        .def_readwrite("worldPoint", &HitInfo::worldPoint)
        .def_readwrite("relativePoint", &HitInfo::relativePoint)
        .def_readwrite("worldNormal", &HitInfo::worldNormal)
        .def_readwrite("distance", &HitInfo::distance)
        .def_readwrite("hitColor", &HitInfo::hitColor)
        .def_property(
            "entity",
            [](const HitInfo& info) -> py::object {
                if (info.entity) {
                    return py::cast(info.entity);
                } else {
                    return py::none();
                }
            },
            [](HitInfo& info, py::object pyEntity) {
                if (!pyEntity.is_none()) {
                    info.entity = pyEntity.cast<Entity*>();
                } else {
                    info.entity = nullptr;
                }
            });

    m.def("raycast", &raycast, py::arg("origin"), py::arg("direction"),
          py::arg("debug") = false, py::arg("ignore") = std::vector<Entity>());
}

PYBIND11_EMBEDDED_MODULE(colorModule, m) {
    py::class_<Color>(m, "Color")
        .def(py::init<unsigned char, unsigned char, unsigned char, unsigned char>())
        .def_readwrite("r", &Color::r)
        .def_readwrite("g", &Color::g)
        .def_readwrite("b", &Color::b)
        .def_readwrite("a", &Color::a);
}

Vector2 GetMouseMovement() {
    static Vector2 lastMousePosition = {0};

    if (timeInstance.dt - lastFrameCount != 0) {
        Vector2 mousePosition = GetMousePosition();
        mouseMove = {mousePosition.x - lastMousePosition.x,
                     mousePosition.y - lastMousePosition.y};

        lastMousePosition = mousePosition;
        lastFrameCount = timeInstance.dt;
    }

    return mouseMove;
}