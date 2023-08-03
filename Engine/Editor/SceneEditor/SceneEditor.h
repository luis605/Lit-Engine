#ifndef SCENE_EDITOR_H
#define SCENE_EDITOR_H

#include "../../../include_all.h"

float slowCameraSpeed = 15.0f;
float defaultCameraSpeed = 25.0f;
float fastCameraSpeed = 50.0f;
float movementSpeed = defaultCameraSpeed;


Camera3D scene_camera;
float lerp_factor = 0.5f;
Vector3 front;

#endif // SCENE_EDITOR_H