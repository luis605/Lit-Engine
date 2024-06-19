#ifndef SCENE_EDITOR_H
#define SCENE_EDITOR_H

float slowCameraSpeed = 15.0f;
float defaultCameraSpeed = 25.0f;
float fastCameraSpeed = 50.0f;
float movementSpeed = defaultCameraSpeed;
float lerp_factor = 0.5f;
float mousePositionXLast = 0;
float GRID_SIZE = 40.0f;
float GRID_SCALE = 1.0f;

Model lightModel;

LitCamera sceneCamera;

static Vector2 rlLastMousePosition = { 0 };

typedef enum CopyType {
    CopyType_None = 0,
    CopyType_Entity = 1,
    CopyType_Light = 2
};

bool movingEditorCamera = false;

CopyType currentCopyType = (CopyType)CopyType_None;
std::shared_ptr<Entity> copiedEntity;
std::shared_ptr<Light> copiedLight;

#ifndef GAME_SHIPPING
    ImVec2 prevEditorWindowSize = {0.0f, 0.0f};
#endif // GAME_SHIPPING

#endif // SCENE_EDITOR_H