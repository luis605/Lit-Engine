#ifndef GIZMO_H
#define GIZMO_H
#include "../../../../include_all.h"

struct GizmoArrow {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = {1,1,1};
    string drag_directions;

};
GizmoArrow gizmo_arrow[5+1]; // +1 solved one arrow not drawing. probably memory corruption in the last arrow index...


struct GizmoTaurus {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = {1,1,1};
    string drag_directions;

};
GizmoTaurus gizmo_taurus[2];



struct GizmoCube {
    Model model;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = {1,1,1};
    string drag_directions;

};
GizmoCube gizmo_cube[5];

int gizmo_cube_selected = -1;
bool dragging_gizmo_scale = false;

float gizmo_drag_sensitivity_factor = 0.1f;


#endif // GIZMO_H