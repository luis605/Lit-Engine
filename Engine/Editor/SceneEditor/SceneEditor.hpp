#ifndef SCENE_EDITOR_H
#define SCENE_EDITOR_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Engine.hpp>
#include <Engine/Lighting/lights.hpp>
#include <memory>
#include <raylib.h>

void InitEditorCamera();
static inline double GetImGuiWindowTitleHeight();
void CalculateTextureRect(const Texture* texture, Rectangle& viewportRectangle);
void DrawTextureOnViewportRectangle(const Texture* texture);
void EditorCameraMovement();
bool IsMouseHoveringModel(const Model& model, const Vector3& position,
                          const Vector3& rotation, const Vector3& scale,
                          const Entity* entity = nullptr, bool bypassOptimization = false);
void LocateEntity(Entity& entity);
void ProcessCameraControls();
void ProcessGizmo();
void HandleUnselect();
void RenderViewportTexture();
void ApplyBloomEffect();
void RenderLights();
void RenderEntities();
void UpdateShader();
void RenderGrid();
void ComputeSceneLuminance();
void RenderScene();
void DropEntity();
void ProcessObjectControls();
void ObjectsPopup();
void ProcessDeletion();
void LightPaste(const std::shared_ptr<LightStruct>& lightStruct);
void DuplicateLight(LightStruct& lightStruct, Entity* parent = nullptr);
void DuplicateEntity(Entity& entity, Entity* parent = nullptr);
void EntityPaste(const std::shared_ptr<Entity>& entity,
                 Entity* parent = nullptr);
void ProcessCopy();
void ScaleViewport();
void drawEditorCameraMenu();
void EditorCamera();

enum CopyType { CopyType_None = 0, CopyType_Entity = 1, CopyType_Light = 2 };

struct CacheEntry {
    Matrix transform;
    std::vector<BoundingBox> bounds;
};

bool MatrixEquals(const Matrix& a, const Matrix& b, float tolerance = 0.0001f);

constexpr float GRID_SIZE = 30.0f;
constexpr float GRID_SCALE = 1.0f;
static Vector2 rlLastMousePosition = { 0 };

extern float movementSpeed;
extern float slowCameraSpeed;
extern float fastCameraSpeed;
extern float defaultCameraSpeed;
extern Model lightModel;
extern LitCamera sceneCamera;
extern bool movingEditorCamera;
extern CopyType currentCopyType;
extern std::shared_ptr<Entity> copiedEntity;
extern std::shared_ptr<LightStruct> copiedLight;

#ifndef GAME_SHIPPING
    extern ImVec2 prevEditorWindowSize;
#endif // GAME_SHIPPING

#endif // SCENE_EDITOR_H