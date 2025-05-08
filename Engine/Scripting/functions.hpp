/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <string>
#include <vector>

#include <btBulletDynamicsCommon.h>
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define RAYMATH_IMPLEMENTATION
#include <raylib.h>
#include <raymath.h>


class PhysicsManager;
class Entity;
struct HitInfo;
struct LitCamera;

//#include <Engine/Core/Entity.hpp>
#include <Engine/Scripting/time.hpp>
#include <Engine/Scripting/math.hpp>
#include <Engine/Core/Raycast.hpp>

namespace py = pybind11;

Vector2 GetMouseMovement();
pybind11::object exportCamera();

struct LitCamera : public Camera3D {
    LitVector3 front, right, left, back;
    LitVector3 pos = LitVector3{};
    LitVector3 look_at = LitVector3{};
    LitVector3 up_vector = LitVector3{0.0f, 1.0f, 0.0f};
    std::string name = "Camera";

    LitCamera(LitVector3 pos = LitVector3{},
        LitVector3 look_at = LitVector3{},
        LitVector3 up_vector = LitVector3{0.0f, 1.0f, 0.0f},
        float _fovy = 0.0f, int _projection = 0)
        : Camera3D{}, front{Vector3Subtract(look_at, pos)}, up_vector{up_vector}
    {
        update();
    }

    void update();
    void calculateVectors();
};

extern LitCamera camera;
extern Vector2 mouseMove;
extern float lastFrameCount;

#endif // FUNCTIONS_H