/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Scripting/time.hpp>
#include <Engine/Scripting/math.hpp>
#include <Engine/Scripting/functions.hpp>
#include <btBulletDynamicsCommon.h>
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <raylib.h>
#include <list>
#include <algorithm>

class PhysicsManager;

#include <Engine/Core/Entity.hpp>
#include <Engine/Core/Engine.hpp>
#include <Engine/Core/Raycast.hpp>

namespace py = pybind11;

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

        .def("normalize", &LitVector3::normalized)
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
             [](const LitVector3& a, float scalar) { return a * LitVector3(scalar, scalar, scalar); })
        .def("__mul__",
             [](const LitVector3& a, int scalar) { return a * LitVector3(scalar, scalar, scalar); })
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
    m.def("lerp", static_cast<LitVector3 (*)(const LitVector3&, const LitVector3&, float)>(&lerpVector3),
          "Lerp function for LitVector3 type");

    // Adding clamp overloads using lambda wrappers
    m.def("clamp", [](float value, float low, float high) {
        return customClamp<float>(value, low, high);
    });

    m.def("clamp", [](LitVector3 value, LitVector3 low, LitVector3 high) -> LitVector3 {
        return LitVector3(customClamp(value.x, low.x, high.x),
                          customClamp(value.y, low.y, high.y),
                          customClamp(value.z, low.z, high.z));
    });
    m.def("clamp", [](LitVector3 value, LitVector3 low, float high) -> LitVector3 {
        return LitVector3(customClamp(value.x, low.x, high),
                          customClamp(value.y, low.y, high),
                          customClamp(value.z, low.z, high));
    });
    m.def("clamp", [](LitVector3 value, LitVector3 low, int high) -> LitVector3 {
        float high_f = static_cast<float>(high);
        return LitVector3(customClamp(value.x, low.x, high_f),
                          customClamp(value.y, low.y, high_f),
                          customClamp(value.z, low.z, high_f));
    });
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

    py::enum_<KeyboardKey>(m, "Key")
        .value("NULL", KEY_NULL)
        .value("APOSTROPHE", KEY_APOSTROPHE)
        .value("COMMA", KEY_COMMA)
        .value("MINUS", KEY_MINUS)
        .value("PERIOD", KEY_PERIOD)
        .value("SLASH", KEY_SLASH)
        .value("ZERO", KEY_ZERO)
        .value("ONE", KEY_ONE)
        .value("TWO", KEY_TWO)
        .value("THREE", KEY_THREE)
        .value("FOUR", KEY_FOUR)
        .value("FIVE", KEY_FIVE)
        .value("SIX", KEY_SIX)
        .value("SEVEN", KEY_SEVEN)
        .value("EIGHT", KEY_EIGHT)
        .value("NINE", KEY_NINE)
        .value("SEMICOLON", KEY_SEMICOLON)
        .value("EQUAL", KEY_EQUAL)
        .value("A", KEY_A)
        .value("B", KEY_B)
        .value("C", KEY_C)
        .value("D", KEY_D)
        .value("E", KEY_E)
        .value("F", KEY_F)
        .value("G", KEY_G)
        .value("H", KEY_H)
        .value("I", KEY_I)
        .value("J", KEY_J)
        .value("K", KEY_K)
        .value("L", KEY_L)
        .value("M", KEY_M)
        .value("N", KEY_N)
        .value("O", KEY_O)
        .value("P", KEY_P)
        .value("Q", KEY_Q)
        .value("R", KEY_R)
        .value("S", KEY_S)
        .value("T", KEY_T)
        .value("U", KEY_U)
        .value("V", KEY_V)
        .value("W", KEY_W)
        .value("X", KEY_X)
        .value("Y", KEY_Y)
        .value("Z", KEY_Z)
        .value("LEFT_BRACKET", KEY_LEFT_BRACKET)
        .value("BACKSLASH", KEY_BACKSLASH)
        .value("RIGHT_BRACKET", KEY_RIGHT_BRACKET)
        .value("GRAVE", KEY_GRAVE)
        .value("ESCAPE", KEY_ESCAPE)
        .value("ENTER", KEY_ENTER)
        .value("TAB", KEY_TAB)
        .value("BACKSPACE", KEY_BACKSPACE)
        .value("INSERT", KEY_INSERT)
        .value("DELETE", KEY_DELETE)
        .value("RIGHT", KEY_RIGHT)
        .value("LEFT", KEY_LEFT)
        .value("DOWN", KEY_DOWN)
        .value("UP", KEY_UP)
        .value("PAGE_UP", KEY_PAGE_UP)
        .value("PAGE_DOWN", KEY_PAGE_DOWN)
        .value("HOME", KEY_HOME)
        .value("END", KEY_END)
        .value("CAPS_LOCK", KEY_CAPS_LOCK)
        .value("SCROLL_LOCK", KEY_SCROLL_LOCK)
        .value("NUM_LOCK", KEY_NUM_LOCK)
        .value("PRINT_SCREEN", KEY_PRINT_SCREEN)
        .value("PAUSE", KEY_PAUSE)
        .value("F1", KEY_F1)
        .value("F2", KEY_F2)
        .value("F3", KEY_F3)
        .value("F4", KEY_F4)
        .value("F5", KEY_F5)
        .value("F6", KEY_F6)
        .value("F7", KEY_F7)
        .value("F8", KEY_F8)
        .value("F9", KEY_F9)
        .value("F10", KEY_F10)
        .value("F11", KEY_F11)
        .value("F12", KEY_F12)
        .value("LEFT_SHIFT", KEY_LEFT_SHIFT)
        .value("LEFT_CONTROL", KEY_LEFT_CONTROL)
        .value("LEFT_ALT", KEY_LEFT_ALT)
        .value("LEFT_SUPER", KEY_LEFT_SUPER)
        .value("RIGHT_SHIFT", KEY_RIGHT_SHIFT)
        .value("RIGHT_CONTROL", KEY_RIGHT_CONTROL)
        .value("RIGHT_ALT", KEY_RIGHT_ALT)
        .value("RIGHT_SUPER", KEY_RIGHT_SUPER)
        .value("KB_MENU", KEY_KB_MENU)
        .value("SPACE", KEY_SPACE);
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

std::list<int> filterEntitiesByName(const std::string& targetName) {
    std::list<int> foundIds;

    std::for_each(entitiesList.begin(), entitiesList.end(),
        [&](const Entity& entity) {
            if (entity.name == targetName) {
                foundIds.push_back(entity.id);
            }
        }
    );

    return foundIds;
}

Entity* findEntityById(const int id) {
    auto it = std::find_if(entitiesList.begin(), entitiesList.end(),
        [id](const Entity& entity) {
            return entity.id == id;
        }
    );

    if (it != entitiesList.end()) {
        return &(*it);
    }

    return nullptr;
}

PYBIND11_EMBEDDED_MODULE(engineModule, m) {
    m.def("filterEntitiesByName", &filterEntitiesByName);
    m.def("findEntityById",       &findEntityById, py::return_value_policy::reference);
    m.def("removeEntity", [](const int id) {
        Entity* entity = findEntityById(id);
        if (entity) {
            entitiesList.erase(
                std::remove_if(entitiesList.begin(), entitiesList.end(),
                    [id](const Entity& e) { return e.id == id; }),
                entitiesList.end()
            );
        } else {
            TraceLog(LOG_WARNING, "Entity with ID %d not found.", id);
        }
    });
    m.def("getAllEntities", []() {
        py::list handle_list;
        for (const auto& entity : entitiesList) {
            handle_list.append(py::cast(EntityHandle(entity.id)));
        }
        return handle_list;
    }, "Returns a list of handles to all existing entities.");
}