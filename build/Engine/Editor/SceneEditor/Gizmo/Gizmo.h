#ifndef GIZMO_H
#define GIZMO_H

#include "../../../../include_all.h"

// Define constants for gizmo arrow and cube count
constexpr int NUM_GIZMO_ARROWS = 6;
constexpr int NUM_GIZMO_TAURUS = 3;
constexpr int NUM_GIZMO_CUBES = 6;

// Gizmos
struct Gizmo {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = {1, 1, 1};
    string drag_directions;
};

Gizmo gizmo_arrow[NUM_GIZMO_ARROWS];
Gizmo gizmo_taurus[NUM_GIZMO_TAURUS];
Gizmo gizmo_cube[NUM_GIZMO_CUBES];

// Declare selected object variables and flags
Vector3 selected_object_position;
Vector3 selected_object_rotation;
Vector3 selected_object_scale;
Vector2 mouse_drag_start;
Vector2 mousePosition;
Vector2 mousePositionPrev;
int gizmo_arrow_selected            = -1;
int gizmo_taurus_selected           = -1;
int gizmo_cube_selected             = -1;
bool dragging;
bool dragging_gizmo_position        = false;
bool dragging_gizmo_rotation        = false;
bool dragging_gizmo_scale           = false;
bool dragging_gizmo_arrow           = false;
bool isHoveringGizmo;

float gizmo_drag_sensitivity_factor = 0.1f;

#endif // GIZMO_H
