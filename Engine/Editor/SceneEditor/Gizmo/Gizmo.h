#ifndef GIZMO_H
#define GIZMO_H

constexpr int NUM_GIZMO_POSITION = 6;
constexpr int NUM_GIZMO_SCALE    = 6;
constexpr int NUM_GIZMO_ROTATION = 1;

enum GizmoAxis {
    UNDEFINED_AXIS = 0,
    X_AXIS         = 1,
    Y_AXIS         = 2,
    Z_AXIS         = 3
};

struct Gizmo {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = {1, 1, 1};
    Color color;
    GizmoAxis axis;
};

Gizmo gizmoPosition[NUM_GIZMO_POSITION];
Gizmo gizmoScale[NUM_GIZMO_SCALE];
Gizmo gizmoRotation[NUM_GIZMO_ROTATION];

// Declare selected object variables and flags
Vector3 selectedObjectPosition;
Vector3 selectedObjectRotation;
Vector3 selectedObjectScale;
Vector2 mouseDragStart;
Vector2 mousePosition;
Vector2 mousePositionPrev;
Vector3 gizmoRotationDelta;
int selectedPositionGizmo            = -1;
int selectedScaleGizmo             = -1;
bool dragging                     = false;
bool draggingPositionGizmo        = false;
bool draggingRotationGizmo        = false;
bool draggingScaleGizmo           = false;
bool isHoveringGizmo;
bool lockRotationX                = false;
bool lockRotationY                = false;
bool lockRotationZ                = false;

float gizmoDragSensitivityFactor = 0.1f;

struct GizmoPosition {
    Vector3 position;
    Vector3 rotation;
};

GizmoPosition gizmoPositionOffsets[] = {
    {{6, 0, 0}, {0, 0, -90}},
    {{-6, 0, 0}, {0, 0, 90}},
    {{0, 6, 0}, {0, 0, 0}},
    {{0, -6, 0}, {180, 0, 0}},
    {{0, 0, 6}, {90, 0, 0}},
    {{0, 0, -6}, {-90, 0, 0}}
};

Color gizmoPositionColorsUnselected[] = {
    { 100, 0, 0, 255 },
    { 100, 0, 0, 255 },
    { 0, 0, 100, 255 },
    { 0, 0, 100, 255 },
    { 0, 100, 0, 255 },
    { 0, 100, 0, 255 }
};

Color gizmoPositionColorsSelected[] = {
    { 150, 0, 0, 255 },
    { 150, 0, 0, 255 },
    { 0, 0, 150, 255 },
    { 0, 0, 150, 255 },
    { 0, 150, 0, 255 },
    { 0, 150, 0, 255 }
};

#endif // GIZMO_H
