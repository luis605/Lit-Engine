/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Core/Events.hpp>
#include <Engine/Core/RunGame.hpp>
#include <Engine/Core/Textures.hpp>
#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/SceneEditor/Gizmo/Gizmo.hpp>
#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include <Engine/Lighting/Shaders.hpp>
#include <Engine/Lighting/lights.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <custom.h>
#include <extras/IconsFontAwesome6.h>
#include <rcamera.h>
#include <rlgl.h>
#include <raylib.h>

float slowCameraSpeed = 15.0f;
float defaultCameraSpeed = 25.0f;
float fastCameraSpeed = 50.0f;
float movementSpeed   = defaultCameraSpeed;
Model lightModel;
LitCamera sceneCamera;
bool movingEditorCamera  = false;
bool textureViewportFlip = false;
CopyType currentCopyType = (CopyType)CopyType_None;
std::shared_ptr<Entity>      copiedEntity = nullptr;
std::shared_ptr<LightStruct> copiedLight  = nullptr;

#ifndef GAME_SHIPPING
ImVec2 prevEditorWindowSize = {-1.0f, -1.0f};
#endif

void InitEditorCamera() {
    viewportMRT = LoadMRT(1, 1);

    viewportTexture = viewportMRT.color;

    sceneCamera.position = {35.0f, 5.0f, 0.0f};
    sceneCamera.target = {0.0f, 0.0f, 0.0f};
    sceneCamera.up = {0.0f, 1.0f, 0.0f};

    sceneCamera.fovy = 60.0f;
    sceneCamera.projection = CAMERA_PERSPECTIVE;
}

static inline double GetImGuiWindowTitleHeight() {
    return static_cast<float>(ImGui::GetTextLineHeight()) +
           ImGui::GetStyle().FramePadding.y * 2.0;
}

void CalculateTextureRect(const Texture* texture,
                          Rectangle& viewportRectangle) {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    viewportRectangle.x = windowPos.x;
    viewportRectangle.y = windowPos.y;
    viewportRectangle.width = windowSize.x;
    viewportRectangle.height =
        windowSize.y - GetImGuiWindowTitleHeight() - 60.0;
}

void DrawTextureOnViewportRectangle(const Texture& texture) {
    CalculateTextureRect(&texture, viewportRectangle);
    ImGui::SetCursorPos(ImVec2(0, GetImGuiWindowTitleHeight() + 60.0));

    ImVec2 uv0 = ImVec2(0, 1);
    ImVec2 uv1 = ImVec2(1, 0);

    if (textureViewportFlip) {
        std::swap(uv0.y, uv1.y);
    }

    ImGui::Image((ImTextureID)&texture,
                 ImVec2(viewportRectangle.width, viewportRectangle.height),
                 uv0, uv1);
}

void EditorCameraMovement() {
    sceneCamera.calculateVectors();
    Vector3 DeltaTimeVec3 = {GetFrameTime(), GetFrameTime(), GetFrameTime()};
    movingEditorCamera = false;

    auto MoveCamera = [&](Vector3 direction, bool scaleNegative = false) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(
            direction, scaleNegative ? -movementSpeed : movementSpeed);
        sceneCamera.position = Vector3Add(
            sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Add(
            sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    };

    if (IsKeyDown(KEY_W))
        MoveCamera(sceneCamera.front);
    if (IsKeyDown(KEY_S))
        MoveCamera(sceneCamera.front, true);
    if (IsKeyDown(KEY_D))
        MoveCamera(sceneCamera.right);
    if (IsKeyDown(KEY_A))
        MoveCamera(sceneCamera.right, true);
    if (IsKeyDown(KEY_Q))
        MoveCamera(sceneCamera.up, true);
    if (IsKeyDown(KEY_E))
        MoveCamera(sceneCamera.up);

    if (GetMouseWheelMove() != 0 && ImGui::IsWindowHovered()) {
        movingEditorCamera = true;
        CameraMoveToTarget(&sceneCamera, -GetMouseWheelMove());
    }

    sceneCamera.target = Vector3Add(sceneCamera.position, sceneCamera.front);

    if (IsKeyDown(KEY_LEFT_SHIFT))
        movementSpeed = fastCameraSpeed;
    else if (IsKeyDown(KEY_LEFT_CONTROL))
        movementSpeed = slowCameraSpeed;
    else
        movementSpeed = defaultCameraSpeed;

    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mousePosition = GetMousePosition();
        float angleX = (rlLastMousePosition.x - mousePosition.x) * 0.005f;
        float angleY = (rlLastMousePosition.y - mousePosition.y) * 0.005f;

        CameraYaw(&sceneCamera, angleX, false);
        CameraPitch(&sceneCamera, angleY, true, false, false);

        rlLastMousePosition = mousePosition;
    } else
        rlLastMousePosition = GetMousePosition();
}

bool MatrixEquals(const Matrix& a, const Matrix& b, float tolerance) {
    const float* aFloats = reinterpret_cast<const float*>(&a);
    const float* bFloats = reinterpret_cast<const float*>(&b);
    for (int i = 0; i < 16; i++) {
        if (fabs(aFloats[i] - bFloats[i]) > tolerance)
            return false;
    }
    return true;
}

bool IsMouseHoveringModel(const Model& model, const Vector3& position,
                          const Vector3& rotation, const Vector3& scale,
                          const Entity* entity, bool bypassOptimization) {
    if (!IsModelReady(model))
        return false;

    static std::unordered_map<const Model*, CacheEntry> cache;

    Vector2 mousePosition = GetMousePosition();
#ifndef GAME_SHIPPING
    Vector2 relativeMousePosition = {mousePosition.x - viewportRectangle.x,
                                     mousePosition.y - viewportRectangle.y -
                                         GetImGuiWindowTitleHeight() - 60.0f};
#else
    Vector2 relativeMousePosition = {mousePosition.x - viewportRectangle.x,
                                     mousePosition.y - viewportRectangle.y -
                                         GetImGuiWindowTitleHeight()};
#endif

    Ray mouseRay = GetScreenToWorldRayEx(relativeMousePosition, sceneCamera,
                                         viewportRectangle.width,
                                         viewportRectangle.height);

    CacheEntry* cacheEntry = nullptr;
    auto it = cache.find(&model);
    if (it == cache.end()) {
        CacheEntry newEntry;
        newEntry.transform = model.transform;
        newEntry.bounds.resize(model.meshCount);
        for (int i = 0; i < model.meshCount; i++) {
            BoundingBox meshBB = GetMeshBoundingBox(model.meshes[i]);
            newEntry.bounds[i].min =
                Vector3Transform(meshBB.min, model.transform);
            newEntry.bounds[i].max =
                Vector3Transform(meshBB.max, model.transform);
        }
        // Insert new entry into cache.
        auto result = cache.emplace(&model, std::move(newEntry));
        cacheEntry = &result.first->second;
    } else {
        cacheEntry = &it->second;
        if (!MatrixEquals(cacheEntry->transform, model.transform)) {
            cacheEntry->transform = model.transform;
            cacheEntry->bounds.resize(model.meshCount);
            for (int i = 0; i < model.meshCount; i++) {
                BoundingBox meshBB = GetMeshBoundingBox(model.meshes[i]);
                cacheEntry->bounds[i].min =
                    Vector3Transform(meshBB.min, model.transform);
                cacheEntry->bounds[i].max =
                    Vector3Transform(meshBB.max, model.transform);
            }
        }
    }

    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++) {
        BoundingBox meshBounds;
        if (entity == nullptr) {
            meshBounds = cacheEntry->bounds[meshIndex];
        } else {
            meshBounds = entity->bounds;
        }

        if (bypassOptimization ||
            GetRayCollisionBox(mouseRay, meshBounds).hit) {
            // Expensive mesh test.
            if (GetRayCollisionMesh(mouseRay, model.meshes[meshIndex],
                                    model.transform)
                    .hit) {
                return true;
            }
        }
    }

    return false;
}

void LocateEntity(Entity& entity) {
    if (selectedGameObjectType == "entity") {
        sceneCamera.target = entity.position;
        sceneCamera.position = {entity.position.x + 10, entity.position.y + 2,
                                entity.position.z};
    }
}

void ProcessCameraControls() {
    if (IsKeyPressed(KEY_F))
        LocateEntity(*selectedEntity);
}

void ProcessGizmo() {
    if (selectedGameObjectType == "entity" && selectedEntity) {
        GizmoPosition();
        GizmoScale();
        GizmoRotation();
    } else if (selectedGameObjectType == "light" && selectedLight) {
        GizmoPosition();
        GizmoRotation();
    } else {
        draggingPositionGizmo = draggingRotationGizmo = draggingScaleGizmo =
            false;
    }

    dragging =
        (draggingScaleGizmo || draggingPositionGizmo || draggingRotationGizmo);
}

void HandleUnselect() {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ImGui::IsWindowHovered() &&
        !dragging)
        selectedGameObjectType = "none";
}

Texture2D ApplySSGI(const Texture2D& sceneTexture, const Texture2D& depthTexture, const LitCamera& camera) {
    // textureViewportFlip = !textureViewportFlip;

    const Matrix P = MatrixPerspective(
        DEG2RAD * camera.fovy,
        sceneTexture.width / sceneTexture.height,
        rlGetCullDistanceNear(),
        rlGetCullDistanceFar()
    );

    const Matrix  invP       = MatrixInvert(P);
    const Vector2 screenSize = Vector2(sceneTexture.width, sceneTexture.height);

    const Matrix view = GetCameraMatrix(camera);
    const Matrix invView = MatrixInvert(view);

    BeginTextureMode(ssgiTexture);
    ClearBackground(BLANK);
    BeginShaderMode(shaderManager.m_SSGIShader);

    const int locProjection = shaderManager.GetUniformLocation(shaderManager.m_SSGIShader.id, "uProjection");
    const int locInvProj    = shaderManager.GetUniformLocation(shaderManager.m_SSGIShader.id, "uInvProj");
    const int locScreen     = shaderManager.GetUniformLocation(shaderManager.m_SSGIShader.id, "uScreenSize");
    const int locInvView    = shaderManager.GetUniformLocation(shaderManager.m_SSGIShader.id, "uInvView");
    SetShaderValueMatrix(shaderManager.m_SSGIShader, locProjection, P);
    SetShaderValueMatrix(shaderManager.m_SSGIShader, locInvProj, invP);
    SetShaderValueMatrix(shaderManager.m_SSGIShader, locInvView, invView);
    SetShaderValue(shaderManager.m_SSGIShader, locScreen, &screenSize, SHADER_UNIFORM_VEC2);

    const int colorLoc  = rlGetLocationUniform(shaderManager.m_SSGIShader.id, "uColorTex");
    const int normalLoc = rlGetLocationUniform(shaderManager.m_SSGIShader.id, "uNormalTex");
    const int depthLoc  = rlGetLocationUniform(shaderManager.m_SSGIShader.id, "uDepthTex");

    if (colorLoc == -1) {
        //TraceLog(LOG_WARNING, "SSGI: Invalid uniform location for uColorTex.");
    }
    if (depthLoc == -1) {
        //TraceLog(LOG_WARNING, "SSGI: Invalid uniform location for uDepthTex.");
    }

    if (sceneTexture.id == 0) {
        //TraceLog(LOG_WARNING, "SSGI: sceneTexture.id is 0 (invalid).");
    }
    if (viewportMRT.depth.id == 0) {
        //TraceLog(LOG_WARNING, "SSGI: depthTexture.id is 0 (invalid).");
    }

    SetShaderValueTexture(shaderManager.m_SSGIShader, colorLoc,  viewportMRT.color);
    SetShaderValueTexture(shaderManager.m_SSGIShader, normalLoc, viewportMRT.normal);
    SetShaderValueTexture(shaderManager.m_SSGIShader, depthLoc,  viewportMRT.depth);

    DrawTexture(sceneTexture, 0, 0, WHITE);

    EndShaderMode();
    EndTextureMode();

    return viewportMRT.normal; //ssgiTexture.texture;
}

Texture2D ApplyBloomEffect(const Texture2D& sceneTexture) {
    textureViewportFlip = !textureViewportFlip;

    BeginTextureMode(horizontalBlurTexture);
    ClearBackground(BLANK);
    BeginShaderMode(shaderManager.m_horizontalBlurShader);
    SetShaderValueTexture(
        shaderManager.m_horizontalBlurShader,
        shaderManager.GetUniformLocation(shaderManager.m_horizontalBlurShader.id, "srcTexture"),
        sceneTexture);
    DrawTexture(sceneTexture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(verticalBlurTexture);
    BeginShaderMode(shaderManager.m_verticalBlurShader);
    SetShaderValueTexture(shaderManager.m_verticalBlurShader,
                          shaderManager.GetUniformLocation(shaderManager.m_verticalBlurShader.id, "srcTexture"),
                          horizontalBlurTexture.texture);
    DrawTexture(horizontalBlurTexture.texture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(upsamplerTexture);
    BeginShaderMode(shaderManager.m_upsamplerShader);
    SetShaderValueTexture(
        shaderManager.m_upsamplerShader,
        shaderManager.GetUniformLocation(shaderManager.m_upsamplerShader.id, "downsampledTexture"),
        verticalBlurTexture.texture);
    SetShaderValueTexture(
        shaderManager.m_upsamplerShader, shaderManager.GetUniformLocation(shaderManager.m_upsamplerShader.id, "originalTexture"),
        sceneTexture);
    DrawTexture(verticalBlurTexture.texture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    return upsamplerTexture.texture;
}

Texture2D ApplyChromaticAberration(const Texture2D& sceneTexture) {
    textureViewportFlip = !textureViewportFlip;
    BeginTextureMode(chromaticAberrationTexture);
    ClearBackground(BLANK);
    BeginShaderMode(shaderManager.m_chromaticAberration);

    SetShaderValue(shaderManager.m_chromaticAberration, shaderManager.GetUniformLocation(shaderManager.m_chromaticAberration.id, "offset"), &aberrationOffset, SHADER_UNIFORM_VEC3);

    SetShaderValueTexture(
        shaderManager.m_chromaticAberration,
        shaderManager.GetUniformLocation(shaderManager.m_chromaticAberration.id, "screenTexture"),
        sceneTexture);
    DrawTexture(sceneTexture, 0, 0, WHITE);

    EndShaderMode();
    EndTextureMode();

    return chromaticAberrationTexture.texture;
}

Texture2D ApplyVignetteEffect(const Texture2D& sceneTexture) {
    textureViewportFlip = !textureViewportFlip;

    BeginTextureMode(vignetteTexture);
    ClearBackground(BLANK);
    BeginShaderMode(shaderManager.m_vignetteShader);

    SetShaderValueTexture(
        shaderManager.m_vignetteShader,
        shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "screenTexture"),
        sceneTexture);
    DrawTexture(sceneTexture, 0, 0, WHITE);

    EndShaderMode();
    EndTextureMode();

    return vignetteTexture.texture;
}

Texture current;
Texture depthTexture;
bool ssgiEnabled = false;

void RenderViewportTexture(const LitCamera& camera) {
    current = viewportMRT.color;
    depthTexture = viewportMRT.depth;

    if (ssgiEnabled) {
        current = ApplySSGI(current, depthTexture, camera);
    }
    if (bloomEnabled)      current = ApplyBloomEffect(current);
    if (aberrationEnabled) current = ApplyChromaticAberration(current);
    if (vignetteEnabled)   current = ApplyVignetteEffect(current);

    DrawTextureOnViewportRectangle(current);
}

void RenderLights() {
    for (LightStruct& lightStruct : lights) {
        lightModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = lightTexture;

        float rotation = DrawBillboardRotation(sceneCamera, lightTexture,
                                               {lightStruct.light.position.x,
                                                lightStruct.light.position.y,
                                                lightStruct.light.position.z},
                                               1.0f, WHITE);

        if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON) ||
            !ImGui::IsWindowHovered() || dragging)
            continue;

        Matrix transformMatrix = MatrixMultiply(
            MatrixMultiply(
                MatrixScale(1, 1, 1),
                MatrixRotateXYZ(Vector3Scale({0, rotation, 0}, DEG2RAD))),
            MatrixTranslate(lightStruct.light.position.x,
                            lightStruct.light.position.y,
                            lightStruct.light.position.z));

        lightModel.transform = transformMatrix;

        bool isLightSelected = IsMouseHoveringModel(
            lightModel,
            {lightStruct.light.position.x, lightStruct.light.position.y,
             lightStruct.light.position.z},
            {0, rotation, 0}, {1, 1, 1});
        if (isLightSelected) {
            selectedLight = &lightStruct;
            selectedGameObjectType = "light";
        }
    }
}

void RenderEntities() {
    bool isMouseDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool isWindowHovered = ImGui::IsWindowHovered();

    for (Entity& entity : entitiesListPregame) {
        if (entity.getFlag(Entity::Flag::IS_CHILD))
            continue;

        entity.setFlag(Entity::Flag::CALC_PHYSICS, false);
        entity.render();

        if (!isMouseDown || !isWindowHovered || dragging)
            continue;

        if (IsMouseHoveringModel(entity.model, entity.position, entity.rotation,
                                 entity.scale, &entity)) {
            selectedEntity = &entity;
            selectedGameObjectType = "entity";
        }

        for (int childEntityIndex : entity.entitiesChildren) {
            Entity& childEntity = *getEntityById(childEntityIndex);

            if (IsMouseHoveringModel(childEntity.model, childEntity.position,
                                     childEntity.rotation, childEntity.scale)) {
                selectedEntity = &childEntity;
                selectedGameObjectType = "entity";
            }
        }
    }
}

void UpdateShader() {
    float cameraPos[3] = { sceneCamera.position.x, sceneCamera.position.y, sceneCamera.position.z };

    SetShaderValue(*shaderManager.m_defaultShader, shaderManager.GetUniformLocation((*shaderManager.m_defaultShader).id, "viewPos"), cameraPos, SHADER_UNIFORM_VEC3);

    for (Entity& entity : entitiesListPregame) {
        std::shared_ptr<Shader> shader = entity.getShader();
        SetShaderValue(*shader, shaderManager.GetUniformLocation((*shader).id, "viewPos"), cameraPos, SHADER_UNIFORM_VEC3);
    }

    SetShaderValue((*shaderManager.m_instancingShader),
                   (*shaderManager.m_instancingShader).locs[SHADER_LOC_VECTOR_VIEW], cameraPos,
                   SHADER_UNIFORM_VEC3);
}

void RenderGrid() {
    constexpr float halfGridSize = (GRID_SIZE * 0.5f) * GRID_SCALE;

    for (int x = 0; x <= GRID_SIZE; x++) {
        const float xScaled = (x * GRID_SCALE) - halfGridSize;

        DrawLine3D({xScaled, 0.0f, -halfGridSize},
                   {xScaled, 0.0f, halfGridSize}, WHITE);
        DrawLine3D({-halfGridSize, 0.0f, xScaled},
                   {halfGridSize, 0.0f, xScaled}, WHITE);
    }
}

void ComputeSceneLuminance() {
    if (downsampledTextures.empty())
        return;

    for (size_t i = 0; i < downsampledTextures.size(); i++) {
        BeginTextureMode(downsampledTextures[i]);
        BeginShaderMode(shaderManager.m_downsampleShader);
        SetShaderValueTexture(
            shaderManager.m_downsampleShader, shaderManager.GetUniformLocation(shaderManager.m_downsampleShader.id, "srcTexture"),
            i == 0 ? viewportTexture : downsampledTextures[i - 1].texture);

        Vector2 srcResolution = {(float)downsampledTextures[i].texture.width,
                                 (float)downsampledTextures[i].texture.height};
        SetShaderValue(shaderManager.m_downsampleShader,
                       shaderManager.GetUniformLocation(shaderManager.m_downsampleShader.id, "srcResolution"),
                       &srcResolution, SHADER_UNIFORM_VEC2);
        DrawTexture(i == 0 ? viewportTexture
                           : downsampledTextures[i - 1].texture,
                    0, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
    }

    rlEnableShader(shaderManager.m_exposureShaderProgram);

    rlBindShaderBuffer(exposureSSBO, 1);
    rlSetUniformSampler(0, downsampledTextures[9].texture.id);
    rlSetUniform(1, &timeInstance.dt, SHADER_UNIFORM_FLOAT, 1);

    rlComputeShaderDispatch(1, 1, 1);

    rlDisableShader();
}

void RenderScene() {
    textureViewportFlip = false;
    BeginMRTMode(viewportMRT);

    BeginMode3D(sceneCamera);
    ClearBackground(GRAY);

    UpdateLightsBuffer(true, lights);
    UpdateInGameGlobals();
    UpdateFrustum();
    UpdateShader();

    ProcessGizmo();

    skybox.drawSkybox(sceneCamera);

    glDepthRange(0, 0.001);
    DrawGizmos();
    glDepthRange(0.001, 1.0);

    RenderGrid();
    RenderLights();
    RenderEntities();

    glDepthRange(0, 1.0);

    HandleUnselect();

    EndMode3D();

    DrawTextElements();
    DrawButtons();
    EndMRTMode();

    ComputeSceneLuminance();
    // DrawTextureOnViewportRectangle(viewportMRT.normal);
    RenderViewportTexture(sceneCamera);
}

void DropEntity() {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    ImRect dropTargetArea(windowPos, windowPos + windowSize);
    ImGuiID windowID = ImGui::GetID(ImGui::GetCurrentWindow()->Name);

    if (ImGui::BeginDragDropTargetCustom(dropTargetArea, windowID)) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("MODEL_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));

            int payloadIndex = *(const int*)payload->Data;

            std::string modelFilePath =
                fs::path(dirPath / fileStruct[payloadIndex].name).string();

            size_t lastDotIndex = modelFilePath.find_last_of('.');
            std::string entityName = modelFilePath.substr(0, lastDotIndex);

            AddEntity(modelFilePath.c_str(), Model(), entityName);
        }

        ImGui::EndDragDropTarget();
    }
}

void ProcessObjectControls() {
    if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
        IsKeyDown(KEY_A) && !movingEditorCamera) {
        ImGui::OpenPopup("Add Object");
        showObjectTypePopup = true;
    }
}

void ObjectsPopup() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2f, 0.2f, 0.2f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.8f, 0.8f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImVec4(0.4f, 0.4f, 0.4f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::IsWindowHovered() && showObjectTypePopup)
        ImGui::OpenPopup("popup");

    if (ImGui::BeginPopup("popup")) {
        ImGui::Text("Add an Object");

        ImGui::Separator();

        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Cube")) {
                AddEntity("", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
                entitiesListPregame.back().ObjectType = ObjectTypeEnum::ObjectType_Cube;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cone")) {
                AddEntity("", LoadModelFromMesh(GenMeshCone(.5, 1, 30)));
                entitiesListPregame.back().ObjectType = ObjectTypeEnum::ObjectType_Cone;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cylinder")) {
                AddEntity("", LoadModelFromMesh(GenMeshCylinder(1.5, 2, 30)));
                entitiesListPregame.back().ObjectType =
                    ObjectTypeEnum::ObjectType_Cylinder;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Plane")) {
                AddEntity("", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));
                entitiesListPregame.back().ObjectType =
                    ObjectTypeEnum::ObjectType_Plane;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Sphere")) {
                AddEntity("", LoadModelFromMesh(GenMeshSphere(.5, 50, 50)));
                entitiesListPregame.back().ObjectType =
                    ObjectTypeEnum::ObjectType_Sphere;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Torus")) {
                AddEntity("", LoadModelFromMesh(GenMeshTorus(.5, 1, 30, 30)));
                entitiesListPregame.back().ObjectType =
                    ObjectTypeEnum::ObjectType_Torus;
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Point Light")) {
                selectedLight = &NewLight({0, 0, 0}, WHITE, LIGHT_POINT);
                selectedGameObjectType = "light";
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Directional Light")) {
                selectedLight = &NewLight({0, 0, 0}, WHITE, LIGHT_DIRECTIONAL);
                selectedGameObjectType = "light";
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Spot Light")) {
                selectedLight = &NewLight({0, 0, 0}, WHITE, LIGHT_SPOT);
                selectedLight = &lights.back();
                selectedGameObjectType = "light";
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("GUI")) {
            if (ImGui::MenuItem("Text")) {
                selectedTextElement =
                    &AddText("Default Text", {100, 100, 1}, 20, BLUE);
                selectedGameObjectType = "text";
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Button")) {
                selectedButton =
                    &AddButton("Default Text", {100, 150, 1}, {200, 50});
                selectedGameObjectType = "button";
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    if (ImGui::IsMouseClicked(0) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        showObjectTypePopup = false;

    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(5);
}

void ProcessDeletion() {
    if (!IsKeyPressed(KEY_DELETE))
        return;

    if (selectedGameObjectType == "entity" && selectedEntity) {
        removeEntity(selectedEntity->id);
        selectedEntity = nullptr;
        selectedGameObjectType = "";
    } else if (selectedGameObjectType == "light" && selectedLight) {
        removeLight(selectedLight->id);
        selectedLight = nullptr;
        selectedGameObjectType = "";
    }
}

void LightPaste(const std::shared_ptr<LightStruct>& lightStruct) {
    if (!lightStruct)
        return;

    LightStruct newLightStruct = *lightStruct;
    newLightStruct.id = entitiesListPregame.size() + lights.size();

    lightIdToIndexMap[newLightStruct.id] = lights.size();

    lights.emplace_back(std::move(newLightStruct));

    if (newLightStruct.isChild) {
        newLightStruct.parent->addLightChild(lights.back().id);
    }

    selectedGameObjectType = "light";
    selectedLight = &lights.back();
}

void DuplicateLight(LightStruct& lightStruct, Entity* parent) {
    LightStruct newLightStruct = lightStruct;
    newLightStruct.id = entitiesListPregame.size() + lights.size();

    lightIdToIndexMap[newLightStruct.id] = lights.size();

    lights.reserve(1);
    lights.emplace_back(std::move(newLightStruct));

    if (parent) {
        parent->addLightChild(lights.back().id);
    } else if (newLightStruct.isChild) {
        newLightStruct.parent->addLightChild(lights.back().id);
    }

    selectedGameObjectType = "light";
    selectedLight = &lights.back();
}

void DuplicateEntity(Entity& entity, Entity* parent) {
    Entity newEntity = entity;
    std::vector<int> oldEntityChildrenIds = newEntity.entitiesChildren;
    std::vector<int> oldLightChildrenIds = newEntity.lightsChildren;
    newEntity.reloadRigidBody();
    newEntity.entitiesChildren.clear();
    newEntity.lightsChildren.clear();
    newEntity.id = entitiesListPregame.size() + lights.size();

    entityIdToIndexMap[newEntity.id] = entitiesListPregame.size();

    entitiesListPregame.reserve(1);
    entitiesListPregame.emplace_back(std::move(newEntity));
    Entity* newEntityRef = &entitiesListPregame.back();

    if (parent) {
        parent->addEntityChild(newEntity.id);
    }

    for (int childIndex : oldEntityChildrenIds) {
        DuplicateEntity(*getEntityById(childIndex), newEntityRef);
    }

    for (int childIndex : oldLightChildrenIds) {
        DuplicateLight(*getLightById(childIndex), newEntityRef);
    }

    selectedGameObjectType = "entity";
    selectedEntity = &entitiesListPregame.back();
}

void EntityPaste(const std::shared_ptr<Entity>& entity, Entity* parent) {
    if (!entity)
        return;

    Entity newEntity = *entity;
    std::vector<int> oldEntityChildrenIds = newEntity.entitiesChildren;
    std::vector<int> oldLightChildrenIds = newEntity.lightsChildren;
    newEntity.entitiesChildren.clear();
    newEntity.lightsChildren.clear();
    newEntity.reloadRigidBody();
    newEntity.id = entitiesListPregame.size() + lights.size();

    entityIdToIndexMap[newEntity.id] = entitiesListPregame.size();

    entitiesListPregame.reserve(1);
    entitiesListPregame.emplace_back(std::move(newEntity));
    Entity* newEntityRef = &entitiesListPregame.back();

    if (parent) {
        parent->addEntityChild(newEntity.id);
    }

    for (int childIndex : oldEntityChildrenIds) {
        DuplicateEntity(*getEntityById(childIndex), newEntityRef);
    }

    for (int childIndex : oldLightChildrenIds) {
        DuplicateLight(*getLightById(childIndex), newEntityRef);
    }

    selectedGameObjectType = "entity";
    selectedEntity = &entitiesListPregame.back();
}

void ProcessCopy() {
    if (IsKeyPressed(KEY_C) &&
        (IsKeyDown(KEY_LEFT_CONTROL) ||
         IsKeyDown(KEY_RIGHT_CONTROL) && !movingEditorCamera)) {
        if (selectedGameObjectType == "entity") {
            currentCopyType = CopyType_Entity;
            if (selectedEntity)
                copiedEntity = std::make_shared<Entity>(*selectedEntity);
        } else if (selectedGameObjectType == "light") {
            currentCopyType = CopyType_Light;
            copiedLight = std::make_shared<LightStruct>(*selectedLight);
        }
    }

    if (IsKeyPressed(KEY_V) &&
        (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        if (currentCopyType == CopyType_Entity)
            EntityPaste(copiedEntity);
        if (currentCopyType == CopyType_Light)
            LightPaste(copiedLight);
    }

    if (IsKeyDown(KEY_D) &&
        (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        !inGamePreview && selectedGameObjectType == "entity" &&
        canDuplicateEntity) {
        DuplicateEntity(*selectedEntity);
        canDuplicateEntity = false;
    }

    if (!canDuplicateEntity)
        canDuplicateEntity = IsKeyUp(KEY_D);
}

void ScaleViewport() {
    ImVec2 currentWindowSize = ImGui::GetWindowSize();

    if (currentWindowSize.x != prevEditorWindowSize.x ||
        currentWindowSize.y != prevEditorWindowSize.y) {
        prevEditorWindowSize = currentWindowSize;


        UnloadMRT(viewportMRT);
        UnloadRenderTexture(chromaticAberrationTexture);
        UnloadRenderTexture(verticalBlurTexture);
        UnloadRenderTexture(horizontalBlurTexture);
        UnloadRenderTexture(upsamplerTexture);
        UnloadRenderTexture(vignetteTexture);
        UnloadRenderTexture(ssgiTexture);

        viewportMRT = LoadMRT(currentWindowSize.x, currentWindowSize.y);
        chromaticAberrationTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        verticalBlurTexture   = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        horizontalBlurTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        upsamplerTexture      = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        vignetteTexture       = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        ssgiTexture           = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        viewportTexture       = viewportMRT.color;

        int index = 0;
        downsampledTextures.clear();

        while (currentWindowSize.x > 1 && currentWindowSize.y > 1) {
            if (currentWindowSize.x > 1)
                currentWindowSize.x /= 2;
            if (currentWindowSize.y > 1)
                currentWindowSize.y /= 2;

            currentWindowSize.x = std::max(currentWindowSize.x, 1.0f);
            currentWindowSize.y = std::max(currentWindowSize.y, 1.0f);

            downsampledTextures.emplace_back(LoadRenderTexture(static_cast<int>(currentWindowSize.x), static_cast<int>(currentWindowSize.y)));
            index++;
        }
    }
}

void drawEditorCameraMenu() {
    float windowWidth = ImGui::GetWindowWidth();

    const float buttonPadding = 5.0f;
    constexpr ImVec2 imgButtonSize(17, 17);
    static float buttonOffsetY = GetImGuiWindowTitleHeight() + 60.0f * 0.5f -
                                 imgButtonSize.y * 0.5f - buttonPadding * 2;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(buttonPadding, 10));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

    ImGui::SetCursorPos(ImVec2(0, GetImGuiWindowTitleHeight()));
    ImGui::BeginGroup();

    ImGui::Dummy(ImVec2(0, 60.0f));
    ImGui::SameLine();

    ImGui::SetCursorPosY(buttonOffsetY);

    if (ImGui::ImageButton("runTex", (ImTextureID)&runTexture, imgButtonSize) &&
        !inGamePreview) {
        eventManager.onScenePlay.triggerEvent();

        entitiesList.clear();

        for (Entity& entity : entitiesListPregame)
            entity.reloadRigidBody();

        entitiesList.assign(entitiesListPregame.begin(),
                            entitiesListPregame.end());

        physics.Backup();
        InitGameCamera();
        inGamePreview = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play the game");

    ImGui::SameLine();
    ImGui::SetCursorPosY(buttonOffsetY);

    if ((ImGui::ImageButton("pauseTex", (ImTextureID)&pauseTexture,
                            imgButtonSize)) &&
            inGamePreview ||
        IsKeyDown(KEY_ESCAPE)) {
        eventManager.onSceneStop.triggerEvent();
        EnableCursor();

        inGamePreview = false;
        firstTimeGameplay = true;

        physics.UnBackup();
        for (Entity& entity : entitiesListPregame)
            entity.resetPhysics();
        for (Entity& entity : entitiesList)
            entity.resetPhysics();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop the game");

    ImGui::SameLine();
    ImGui::SetCursorPosY(buttonOffsetY);

    static ImVec2 buttonSize =
        ImVec2(imgButtonSize.x + ImGui::GetStyle().FramePadding.x * 2.0f,
               imgButtonSize.y + ImGui::GetStyle().FramePadding.y * 2.0f);

    if ((ImGui::Button("+", buttonSize)) && !inGamePreview) {
        showObjectTypePopup = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add objects");

    ImGui::EndGroup();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

void EditorCamera() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(ICON_FA_VIDEO " Scene Editor", NULL);

    drawEditorCameraMenu();
    std::chrono::high_resolution_clock::time_point sceneEditorStart =
        std::chrono::high_resolution_clock::now();

    if (ImGui::IsWindowHovered() && !dragging && !inGamePreview) {
        DropEntity();
        ProcessObjectControls();
    }

    if (ImGui::IsWindowFocused() ||
        (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) &&
            !inGamePreview) {
        ProcessCameraControls();
        ProcessCopy();
        ProcessDeletion();

        if (!showObjectTypePopup)
            EditorCameraMovement();
    } else
        rlLastMousePosition = GetMousePosition();

    if (inGamePreview) {
        RunGame();

        std::chrono::high_resolution_clock::time_point sceneEditorEnd =
            std::chrono::high_resolution_clock::now();
        sceneEditorProfilerDuration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                sceneEditorEnd - sceneEditorStart);

        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    RenderScene();
    ScaleViewport();
    ObjectsPopup();

    std::chrono::high_resolution_clock::time_point sceneEditorEnd =
        std::chrono::high_resolution_clock::now();
    sceneEditorProfilerDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(sceneEditorEnd -
                                                              sceneEditorStart);

    ImGui::End();
    ImGui::PopStyleVar();
}