/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef SCENE_EDITOR_H
#define SCENE_EDITOR_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Engine.hpp>
#include <Engine/Lighting/lights.hpp>
#include <memory>
#include <raylib.h>

class SceneEditor {
private:
    struct CacheEntry {
        Matrix transform;
        std::vector<BoundingBox> bounds;
    };

    float slowCameraSpeed    = 15.0f;
    float defaultCameraSpeed = 25.0f;
    float fastCameraSpeed    = 50.0f;
    float movementSpeed      = defaultCameraSpeed;
    bool textureViewportFlip = false;
    float sceneEditorMenuHeight = 0.0f;
    Vector2 lastMousePosition = { 0 };
    Texture currentTexturePostProcessing;
    static constexpr float GRID_SIZE = 30.0f;
    static constexpr float GRID_SCALE = 1.0f;

public:
    enum CopyType { CopyType_None = 0, CopyType_Entity = 1, CopyType_Light = 2 };
    bool movingEditorCamera = false;
    Model lightModel;
    LitCamera sceneCamera;
    CopyType currentCopyType = (CopyType)CopyType_None;
    std::shared_ptr<Entity>      copiedEntity = nullptr;
    std::shared_ptr<LightStruct> copiedLight  = nullptr;

private:
    const float GetImGuiWindowTitleHeight();
    void CalculateTextureRect(Rectangle& viewportRectangle);
    void DrawTextureOnViewportRectangle(const Texture& texture);
    void EditorCameraMovement();
    void ProcessGizmo();
    void HandleUnselect();
    void RenderLights();
    void RenderEntities();
    void UpdateShader();
    void DropEntity();
    void ProcessObjectControls();
    void ObjectsPopup();
    void ProcessDeletion();
    void LightPaste(const std::shared_ptr<LightStruct>& lightStruct);
    void EntityPaste(const std::shared_ptr<Entity>& entity, Entity* parent = nullptr);
    void ProcessCopy();
    void ScaleViewport();
    void HandleScenePlay();
    void HandleSceneStop();
    void DrawTooltip(const char* text);
    void PushMenuBarStyle();
    void PopMenuBarStyle();
    void DrawSceneEditorMenu();
    void RenderGrid();
    void RenderScene();

public:
    void LocateEntity(const LitVector3& entityPosition);
    void ComputeSceneLuminance();
    void RenderViewportTexture(const LitCamera& camera);
    Texture2D ApplyBloomEffect(const Texture2D& sceneTexture);
    Texture2D ApplyChromaticAberration(const Texture2D& sceneTexture);
    Texture2D ApplyVignetteEffect(const Texture2D& sceneTexture);
    Texture2D ApplyFilmGrainEffect(const Texture2D& sceneTexture);
    void InitEditorCamera();
    void EditorCamera();
    void DuplicateLight(LightStruct& lightStruct, Entity* parent = nullptr);
    void DuplicateEntity(Entity& entity, Entity* parent = nullptr);
    bool IsMouseHoveringModel(const Model& model, const Entity* entity = nullptr, bool bypassOptimization = false);
};

extern SceneEditor sceneEditor;

void RenderViewportTexture(const LitCamera& camera);
Texture2D ApplyBloomEffect(const Texture2D& sceneTexture);
Texture2D ApplyChromaticAberration(const Texture2D& sceneTexture);
Texture2D ApplyVignetteEffect(const Texture2D& sceneTexture);
Texture2D ApplyFilmGrainEffect(const Texture2D& sceneTexture);
void ComputeSceneLuminance();

#ifndef GAME_SHIPPING
    extern ImVec2 prevEditorWindowSize;
#endif // GAME_SHIPPING

#endif // SCENE_EDITOR_H