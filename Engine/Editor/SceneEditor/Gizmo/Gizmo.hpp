/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef GIZMO_H
#define GIZMO_H

#include <Engine/Core/Engine.hpp>
#include <Engine/Core/Entity.hpp>
#include <Engine/Lighting/lights.hpp>
#include <filesystem>
#include <raylib.h>

namespace fs = std::filesystem;

void InitGizmo();

constexpr int NUM_GIZMO_POSITION = 6;
constexpr int NUM_GIZMO_SCALE = 6;
constexpr int NUM_GIZMO_ROTATION = 1;

enum GizmoAxis { UNDEFINED_AXIS = 0, X_AXIS = 1, Y_AXIS = 2, Z_AXIS = 3 };

struct Gizmo {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = {1, 1, 1};
    Color color;
    GizmoAxis axis;
};

extern Gizmo gizmoPosition[NUM_GIZMO_POSITION];
extern Gizmo gizmoScale[NUM_GIZMO_SCALE];
extern Gizmo gizmoRotation[NUM_GIZMO_ROTATION];

// Declare selected object variables and flags
extern Vector3 selectedObjectPosition;
extern Vector3 selectedObjectRotation;
extern Vector3 selectedObjectScale;
extern Vector2 mouseDragStart;
extern Vector2 mousePosition;
extern Vector2 mousePositionPrev;
extern Vector3 gizmoRotationDelta;
extern int selectedPositionGizmo;
extern int selectedScaleGizmo;
extern bool dragging;
extern bool draggingPositionGizmo;
extern bool draggingRotationGizmo;
extern bool draggingScaleGizmo;
extern bool isHoveringGizmo;
extern bool lockRotationX;
extern bool lockRotationY;
extern bool lockRotationZ;

extern float gizmoDragSensitivityFactor;

struct GizmoPositionStructure {
    Vector3 position;
    Vector3 rotation;
};

extern GizmoPositionStructure gizmoPositionOffsets[6];
extern Color gizmoPositionColorsUnselected[6];
extern Color gizmoPositionColorsSelected[6];

void IsGizmoBeingInteracted(const Gizmo& gizmo, const int& index,
                            int& selectedGizmoIndex);
void SetGizmoVisibility(Gizmo& gizmo, const bool& isVisible);
void UpdateGizmoVisibilityAndScaling(Gizmo& gizmo, const Camera& camera);
bool HandleGizmo(bool& draggingGizmoProperty, Vector3& selectedObjectProperty,
                 const GizmoAxis& axis,
                 const bool& applyMinimumConstraint = false);
void DrawGizmo(Gizmo& gizmo, const bool& wireframe = false,
               const bool& applyRotation = false);
void DrawGizmos();
void GizmoPosition();
void GizmoScale();
void GizmoRotation();

#endif // GIZMO_H
