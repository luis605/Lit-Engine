#ifndef GIZMO_H
#define GIZMO_H

#include "../../../../include_all.h"

// Define constants for gizmo arrow and cube count
constexpr int NUM_GIZMO_ARROWS = 6;
constexpr int NUM_GIZMO_TORUS = 1;
constexpr int NUM_GIZMO_CUBES = 6;

// Gizmos
struct Gizmo {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = {1, 1, 1};
    std::string dragDirections;
};

Gizmo gizmoArrow[NUM_GIZMO_ARROWS];
Gizmo gizmoTorus[NUM_GIZMO_TORUS];
Gizmo gizmoCube[NUM_GIZMO_CUBES];

// Declare selected object variables and flags
Vector3 selectedObjectPosition;
Vector3 selectedObjectRotation;
Vector3 selectedObjectScale;
Vector2 mouseDragStart;
Vector2 mousePosition;
Vector2 mousePositionPrev;
int selectedGizmoArrow            = -1;
int selectedGizmoCube             = -1;
bool dragging                     = false;
bool draggingGizmoPosition        = false;
bool draggingGizmoRotation        = false;
bool draggingGizmoScale           = false;
bool isHoveringGizmo;

float gizmoDragSensitivityFactor = 0.1f;

struct GizmoArrow {
    Vector3 position;
    Vector3 rotation;
};

GizmoArrow gizmoArrowOffsets[] = {
    {{0, 6, 0}, {0, 0, 0}},     // Up
    {{0, -6, 0}, {180, 0, 0}},  // Down
    {{0, 0, 6}, {90, 0, 0}},    // Right
    {{0, 0, -6}, {-90, 0, 0}},  // Left
    {{6, 0, 0}, {0, 0, -90}},   // Forward
    {{-6, 0, 0}, {0, 0, 90}}    // Backward
};

SurfaceMaterial gizmoMaterial;

#endif // GIZMO_H
