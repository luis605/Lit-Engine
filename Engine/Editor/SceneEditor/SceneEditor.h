#ifndef SCENE_EDITOR_H
#define SCENE_EDITOR_H

#include "../../../include_all.h"

float slowCameraSpeed = 15.0f;
float defaultCameraSpeed = 25.0f;
float fastCameraSpeed = 50.0f;
float movementSpeed = defaultCameraSpeed;


LitCamera scene_camera;
float lerp_factor = 0.5f;
Vector3 front;

typedef enum CopyType {
    CopyType_None = 0,
    CopyType_Entity = 1,
    CopyType_Light = 2
};

CopyType current_copy_type = (CopyType)CopyType_None;
std::shared_ptr<Entity> copiedEntity;
std::shared_ptr<Light> copiedLight;

#endif // SCENE_EDITOR_H