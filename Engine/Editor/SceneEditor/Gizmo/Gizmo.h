#ifndef GIZMO_H
#define GIZMO_H
#include "../../../../include_all.h"

struct GizmoArrow {
    Model model;
    Vector3 position;
    Vector3 rotation;
    string drag_directions;

};
GizmoArrow gizmo_arrow[5];


struct GizmoTaurus {
    Model model;
    Vector3 position;
    Vector3 rotation;
    string drag_directions;

};
GizmoTaurus gizmo_taurus[2];

float gizmo_drag_sensitivity_factor = 0.1f;


#endif // GIZMO_H