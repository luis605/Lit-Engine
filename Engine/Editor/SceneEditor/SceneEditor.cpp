/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Core/Events.hpp>
#include <Engine/Core/RunGame.hpp>
#include <Engine/Core/Textures.hpp>
#include <Engine/Core/Core.hpp>
#include <Engine/Core/Math.hpp>
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
#include <iostream>

SceneEditor sceneEditor;

#ifndef GAME_SHIPPING
ImVec2 prevEditorWindowSize = {-1.0f, -1.0f};
#endif

void SceneEditor::InitEditorCamera() {
    viewportRT = LoadRenderTexture(1, 1);

    viewportTexture = viewportRT.texture;

    sceneCamera.position = {0.0f, 5.0f, 25.0f};
    sceneCamera.target = {0.0f, 0.0f, 0.0f};
    sceneCamera.up = {0.0f, 1.0f, 0.0f};

    sceneCamera.fovy = 60.0f;
    sceneCamera.projection = CAMERA_PERSPECTIVE;
}

const float SceneEditor::GetImGuiWindowTitleHeight() {
    return static_cast<float>(ImGui::GetTextLineHeight()) +
           ImGui::GetStyle().FramePadding.y * 2.0;
}

void SceneEditor::CalculateTextureRect(Rectangle& viewportRectangle) {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    viewportRectangle.x = windowPos.x;
    viewportRectangle.y = windowPos.y + sceneEditorMenuHeight + GetImGuiWindowTitleHeight() * 0.5;
    viewportRectangle.width = windowSize.x;
    viewportRectangle.height = windowSize.y - sceneEditorMenuHeight;
}

void SceneEditor::DrawTextureOnViewportRectangle(const Texture& texture) {
    CalculateTextureRect(viewportRectangle);
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

void SceneEditor::EditorCameraMovement() {
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
    if (IsKeyPressed(KEY_F))
        LocateEntity(selectedEntity->position);

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
        float angleX = (lastMousePosition.x - mousePosition.x) * 0.005f;
        float angleY = (lastMousePosition.y - mousePosition.y) * 0.005f;

        CameraYaw(&sceneCamera, angleX, false);
        CameraPitch(&sceneCamera, angleY, true, false, false);

        lastMousePosition = mousePosition;
    } else
        lastMousePosition = GetMousePosition();
}

bool MatrixEquals(const Matrix& a, const Matrix& b, float tolerance = 0.001f) {
    const float* aFloats = reinterpret_cast<const float*>(&a);
    const float* bFloats = reinterpret_cast<const float*>(&b);
    for (int i = 0; i < 16; i++) {
        if (fabs(aFloats[i] - bFloats[i]) > tolerance)
            return false;
    }
    return true;
}

bool SceneEditor::IsMouseHoveringModel(const Model& model, const Entity* entity, bool bypassOptimization) {
    if (!IsModelValid(model))
        return false;

    static std::unordered_map<const Model*, CacheEntry> cache;

    Vector2 mousePosition = GetMousePosition();
#ifndef GAME_SHIPPING
    Vector2 relativeMousePosition = {
        static_cast<float>(mousePosition.x - viewportRectangle.x),
        static_cast<float>(mousePosition.y - viewportRectangle.y - GetImGuiWindowTitleHeight() - sceneEditorMenuHeight)};
#else
    Vector2 relativeMousePosition = {
        static_cast<float>(mousePosition.x - viewportRectangle.x),
        static_cast<float>(mousePosition.y - viewportRectangle.y -
                        GetImGuiWindowTitleHeight())};
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
            newEntry.bounds[i].min = Vector3Transform(meshBB.min, model.transform);
            newEntry.bounds[i].max = Vector3Transform(meshBB.max, model.transform);
        }

        auto result = cache.emplace(&model, std::move(newEntry));
        cacheEntry = &result.first->second;
    } else {
        cacheEntry = &it->second;
        if (!MatrixEquals(cacheEntry->transform, model.transform)) {
            cacheEntry->transform = model.transform;
            cacheEntry->bounds.resize(model.meshCount);
            for (int i = 0; i < model.meshCount; i++) {
                BoundingBox meshBB = GetMeshBoundingBox(model.meshes[i]);
                cacheEntry->bounds[i].min = Vector3Transform(meshBB.min, model.transform);
                cacheEntry->bounds[i].max = Vector3Transform(meshBB.max, model.transform);
            }
        }
    }

    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++) {
        BoundingBox meshBounds;
        if (entity) {
            meshBounds = entity->bounds;
        } else {
            meshBounds = cacheEntry->bounds[meshIndex];
        }

        if (bypassOptimization || GetRayCollisionBox(mouseRay, meshBounds).hit) {
            if (GetRayCollisionMesh(mouseRay, model.meshes[meshIndex], model.transform).hit) {
                return true;
            }
        }
    }

    return false;
}

void SceneEditor::LocateEntity(const LitVector3& entityPosition) {
    if (selectedGameObjectType == "entity") {
        sceneCamera.target = entityPosition;
        sceneCamera.position = {entityPosition.x + 10, entityPosition.y + 2,
                                entityPosition.z};
    }
}

void SceneEditor::ProcessGizmo() {
    if (selectedGameObjectType == "entity" && selectedEntity) {
        editorGizmo.beginFrame();
        glDepthRange(0, 0.001);
        editorGizmo.updateAndDraw(Gizmo::Type::Position, selectedEntity->position, selectedEntity->scale, selectedEntity->rotation, sceneCamera, viewportRectangle);
        editorGizmo.updateAndDraw(Gizmo::Type::Scale, selectedEntity->position,    selectedEntity->scale, selectedEntity->rotation, sceneCamera, viewportRectangle);
        editorGizmo.updateAndDraw(Gizmo::Type::Rotation, selectedEntity->position, selectedEntity->scale, selectedEntity->rotation, sceneCamera, viewportRectangle);
        glDepthRange(0.001, 1.0);
        editorGizmo.endFrame();

        if (selectedEntity->getFlag(Entity::Flag::IS_CHILD)) {
            selectedEntity->relativePosition = Vector3Subtract(
                selectedEntity->position,
                selectedEntity->parent->position
            );
        }
    } else if (selectedGameObjectType == "light" && selectedLight) {
        Vector3 lightPosition = glm3ToVec3(selectedLight->light.position);
        Quaternion lightRotation = QuaternionFromEuler(glm3ToVec3(selectedLight->light.direction));
        static Vector3 lightScale = { 0.3f, 0.3f, 0.3f };

        editorGizmo.beginFrame();
        glDepthRange(0, 0.001);
        editorGizmo.updateAndDraw(Gizmo::Type::Position, lightPosition, lightScale, lightRotation, sceneCamera, viewportRectangle);
        editorGizmo.updateAndDraw(Gizmo::Type::Rotation, lightPosition, lightScale, lightRotation, sceneCamera, viewportRectangle);
        glDepthRange(0.001, 1.0);
        editorGizmo.endFrame();

        selectedLight->light.position = vec3ToGlm3(lightPosition);
        selectedLight->light.direction = vec3ToGlm3(QuaternionToEuler(lightRotation));
    }
}

void SceneEditor::HandleUnselect() {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ImGui::IsWindowHovered() && !editorGizmo.isGizmoActive()) selectedGameObjectType = "none";
}

Texture2D SceneEditor::ApplyBloomEffect(const Texture2D& sceneTexture) {
    BeginTextureMode(brightPassTexture);
    ClearBackground(BLANK);
    BeginShaderMode(shaderManager.m_brightFilterShader);

    const int thresholdLoc = shaderManager.GetUniformLocation(shaderManager.m_brightFilterShader.id, "threshold");
    SetShaderValue(shaderManager.m_brightFilterShader, thresholdLoc, &bloomThreshold, SHADER_UNIFORM_FLOAT);

    SetShaderValueTexture(
        shaderManager.m_brightFilterShader,
        shaderManager.GetUniformLocation(shaderManager.m_brightFilterShader.id, "sceneTexture"),
        sceneTexture);
    DrawTexture(sceneTexture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(horizontalBlurTexture);
    ClearBackground(BLANK);
    BeginShaderMode(shaderManager.m_horizontalBlurShader);
    SetShaderValueTexture(
        shaderManager.m_horizontalBlurShader,
        shaderManager.GetUniformLocation(shaderManager.m_horizontalBlurShader.id, "srcTexture"),
        brightPassTexture.texture);
    DrawTexture(sceneTexture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(verticalBlurTexture);
    BeginShaderMode(shaderManager.m_verticalBlurShader);
    SetShaderValueTexture(shaderManager.m_verticalBlurShader, shaderManager.GetUniformLocation(shaderManager.m_verticalBlurShader.id, "srcTexture"), horizontalBlurTexture.texture);
    DrawTexture(horizontalBlurTexture.texture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(bloomCompositorTexture);
    BeginShaderMode(shaderManager.m_bloomCompositorShader);

        SetShaderValueTexture(shaderManager.m_bloomCompositorShader, shaderManager.GetUniformLocation(shaderManager.m_bloomCompositorShader.id, "blurredBrightPassTexture"), verticalBlurTexture.texture);
        SetShaderValueTexture(shaderManager.m_bloomCompositorShader, shaderManager.GetUniformLocation(shaderManager.m_bloomCompositorShader.id, "sceneTexture"), sceneTexture);

        DrawTexture(bloomCompositorTexture.texture, 0, 0, WHITE);

    EndShaderMode();
    EndTextureMode();

    return bloomCompositorTexture.texture;
}

Texture2D SceneEditor::ApplyChromaticAberration(const Texture2D& sceneTexture) {
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

Texture2D SceneEditor::ApplyFilmGrainEffect(const Texture2D& sceneTexture) {
    filmGrainTime += GetFrameTime();
    textureViewportFlip = !textureViewportFlip;

    BeginTextureMode(verticalBlurTexture); // reuse verticalBlurTexture as temp
    ClearBackground(BLANK);
    BeginShaderMode(shaderManager.m_filmGrainShader);
    SetShaderValueTexture(shaderManager.m_filmGrainShader, shaderManager.GetUniformLocation(shaderManager.m_filmGrainShader.id, "sceneTexture"), sceneTexture);
    SetShaderValue(shaderManager.m_filmGrainShader, shaderManager.GetUniformLocation(shaderManager.m_filmGrainShader.id, "grainStrength"), &filmGrainStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shaderManager.m_filmGrainShader, shaderManager.GetUniformLocation(shaderManager.m_filmGrainShader.id, "grainSize"), &filmGrainSize, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shaderManager.m_filmGrainShader, shaderManager.GetUniformLocation(shaderManager.m_filmGrainShader.id, "time"), &filmGrainTime, SHADER_UNIFORM_FLOAT);

    DrawTexture(sceneTexture, 0, 0, WHITE);

    EndShaderMode();
    EndTextureMode();

    return verticalBlurTexture.texture;
}

Texture2D SceneEditor::ApplyVignetteEffect(const Texture2D& sceneTexture) {
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

void SceneEditor::RenderViewportTexture(const LitCamera& camera) {
    currentTexturePostProcessing = viewportRT.texture;

    if (bloomEnabled)      currentTexturePostProcessing = ApplyBloomEffect(currentTexturePostProcessing);
    if (aberrationEnabled) currentTexturePostProcessing = ApplyChromaticAberration(currentTexturePostProcessing);
    if (filmGrainEnabled)  currentTexturePostProcessing = ApplyFilmGrainEffect(currentTexturePostProcessing);
    if (vignetteEnabled)   currentTexturePostProcessing = ApplyVignetteEffect(currentTexturePostProcessing);

    DrawTextureOnViewportRectangle(currentTexturePostProcessing);
}

void SceneEditor::RenderLights() {
    for (LightStruct& lightStruct : lights) {
        lightModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = lightTexture;

        float rotation = DrawBillboardRotation(sceneCamera, lightTexture,
                                               {lightStruct.light.position.x,
                                                lightStruct.light.position.y,
                                                lightStruct.light.position.z},
                                               1.0f, WHITE);

        if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON) ||
            !ImGui::IsWindowHovered() || editorGizmo.isGizmoActive())
            continue;

        Matrix transformMatrix = MatrixMultiply(
            MatrixMultiply(
                MatrixScale(1, 1, 1),
                MatrixRotateXYZ(Vector3Scale({0, rotation, 0}, DEG2RAD))),
            MatrixTranslate(lightStruct.light.position.x,
                            lightStruct.light.position.y,
                            lightStruct.light.position.z));

        lightModel.transform = transformMatrix;

        bool isLightSelected = IsMouseHoveringModel(lightModel);
        if (isLightSelected) {
            selectedLight = &lightStruct;
            selectedGameObjectType = "light";
        }
    }
}

void SceneEditor::RenderEntities() {
    bool isMouseDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool isWindowHovered = ImGui::IsWindowHovered();

    for (Entity& entity : entitiesListPregame) {
        if (entity.getFlag(Entity::Flag::IS_CHILD))
            continue;

        entity.setFlag(Entity::Flag::CALC_PHYSICS, false);
        entity.render();

        if (!isMouseDown || !isWindowHovered || editorGizmo.isGizmoActive())
            continue;

        if (IsMouseHoveringModel(entity.model, &entity)) {
            selectedEntity = &entity;
            selectedGameObjectType = "entity";
        }

        for (int childEntityIndex : entity.entitiesChildren) {
            Entity& childEntity = *getEntityById(childEntityIndex);

            if (IsMouseHoveringModel(childEntity.model)) {
                selectedEntity = &childEntity;
                selectedGameObjectType = "entity";
            }
        }
    }
}

void SceneEditor::UpdateShader() {
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

void SceneEditor::RenderGrid() {
    constexpr float halfGridSize = (GRID_SIZE * 0.5f) * GRID_SCALE;

    for (int x = 0; x <= GRID_SIZE; x++) {
        const float xScaled = (x * GRID_SCALE) - halfGridSize;

        DrawLine3D({xScaled, 0.0f, -halfGridSize},
                   {xScaled, 0.0f, halfGridSize}, WHITE);
        DrawLine3D({-halfGridSize, 0.0f, xScaled},
                   {halfGridSize, 0.0f, xScaled}, WHITE);
    }
}

void SceneEditor::ComputeSceneLuminance() {
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
    rlSetUniformSampler(0, downsampledTextures[downsampledTextures.size() - 1].texture.id);
    rlSetUniform(1, &timeInstance.dt, SHADER_UNIFORM_FLOAT, 1);

    rlComputeShaderDispatch(1, 1, 1);

    rlDisableShader();
}

void SceneEditor::RenderScene() {
    textureViewportFlip = false;
    BeginTextureMode(viewportRT);

    BeginMode3D(sceneCamera);
    ClearBackground(GRAY);

    UpdateLightsBuffer(true, lights);
    UpdateInGameGlobals();
    UpdateFrustum();
    UpdateShader();

    skybox.drawSkybox(sceneCamera);

    ProcessGizmo();

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
    RenderViewportTexture(sceneCamera);
}

void SceneEditor::DropEntity() {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    ImRect dropTargetArea(windowPos, windowPos + windowSize);
    ImGuiID windowID = ImGui::GetID(ImGui::GetCurrentWindow()->Name);

    if (ImGui::BeginDragDropTargetCustom(dropTargetArea, windowID)) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("MODEL_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));

            int payloadIndex = *(const int*)payload->Data;

            std::string modelFilePath = fs::path(dirPath / allItems[payloadIndex].name).string();

            size_t lastDotIndex = modelFilePath.find_last_of('.');
            std::string entityName = modelFilePath.substr(0, lastDotIndex);

            AddEntity(modelFilePath.c_str(), Model(), entityName);
        }

        ImGui::EndDragDropTarget();
    }
}

void SceneEditor::ProcessObjectControls() {
    if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
        IsKeyDown(KEY_A) && !movingEditorCamera) {
        ImGui::OpenPopup("Add Object");
        showObjectTypePopup = true;
    }
}

void SceneEditor::ObjectsPopup() {
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

void SceneEditor::ProcessDeletion() {
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

void SceneEditor::LightPaste(const std::shared_ptr<LightStruct>& lightStruct) {
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

void SceneEditor::DuplicateLight(LightStruct& lightStruct, Entity* parent) {
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

void SceneEditor::DuplicateEntity(Entity& entity, Entity* parent) {
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

void SceneEditor::EntityPaste(const std::shared_ptr<Entity>& entity, Entity* parent) {
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

void SceneEditor::ProcessCopy() {
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

void SceneEditor::ScaleViewport() {
    if (viewportRectangle.width != prevEditorWindowSize.x ||
        viewportRectangle.height != prevEditorWindowSize.y) {
        prevEditorWindowSize = ImVec2(viewportRectangle.width, viewportRectangle.height);

        UnloadRenderTexture(viewportRT);
        UnloadRenderTexture(chromaticAberrationTexture);
        UnloadRenderTexture(verticalBlurTexture);
        UnloadRenderTexture(horizontalBlurTexture);
        UnloadRenderTexture(bloomCompositorTexture);
        UnloadRenderTexture(vignetteTexture);
        UnloadRenderTexture(brightPassTexture);

        viewportRT = LoadRenderTexture(viewportRectangle.width, viewportRectangle.height);
        chromaticAberrationTexture = LoadRenderTexture(viewportRectangle.width, viewportRectangle.height);
        verticalBlurTexture   = LoadRenderTexture(viewportRectangle.width, viewportRectangle.height);
        horizontalBlurTexture = LoadRenderTexture(viewportRectangle.width, viewportRectangle.height);
        bloomCompositorTexture      = LoadRenderTexture(viewportRectangle.width, viewportRectangle.height);
        vignetteTexture       = LoadRenderTexture(viewportRectangle.width, viewportRectangle.height);
        brightPassTexture     = LoadRenderTexture(viewportRectangle.width, viewportRectangle.height);
        viewportTexture       = viewportRT.texture;

        int index = 0;
        downsampledTextures.clear();

        while (viewportRectangle.width > 1 && viewportRectangle.height > 1) {
            if (viewportRectangle.width > 1)
                viewportRectangle.width /= 2;
            if (viewportRectangle.height > 1)
                viewportRectangle.height /= 2;

            viewportRectangle.width = std::max(viewportRectangle.width, 1.0f);
            viewportRectangle.height = std::max(viewportRectangle.height, 1.0f);

            downsampledTextures.emplace_back(LoadRenderTexture(static_cast<int>(viewportRectangle.width), static_cast<int>(viewportRectangle.height)));
            index++;
        }
    }
}

void SceneEditor::HandleScenePlay() {
    eventManager.onScenePlay.triggerEvent();

    entitiesList.clear();
    entitiesList = entitiesListPregame;

    for (Entity& entity : entitiesList) {
        entity.reloadRigidBody();
    }

    physics.Backup();
    InitGameCamera();
    inGamePreview = true;
}

void SceneEditor::HandleSceneStop() {
    eventManager.onSceneStop.triggerEvent();
    EnableCursor();

    inGamePreview = false;
    firstTimeGameplay = true;

    physics.UnBackup();
    for (Entity& entity : entitiesListPregame) {
        entity.resetPhysics();
    }
    for (Entity& entity : entitiesList) {
        entity.resetPhysics();
    }
}

void SceneEditor::DrawTooltip(const char* text) {
    if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.5f) {
        ImGui::SetTooltip("%s", text);
    }
}

void SceneEditor::PushMenuBarStyle() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 10.0f)); // 5.0f is the button padding
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
}

void SceneEditor::PopMenuBarStyle() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

void SceneEditor::DrawSceneEditorMenu() {
    constexpr float MENU_BAR_HEIGHT = 60.0f;
    constexpr ImVec2 ICON_SIZE(17.0f, 17.0f);

    const float menuBarYStart = ImGui::GetCursorPosY();

    PushMenuBarStyle();

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, MENU_BAR_HEIGHT));

    const float buttonHeight = ICON_SIZE.y + ImGui::GetStyle().FramePadding.y * 2.0f;
    const float buttonStartY = menuBarYStart + MENU_BAR_HEIGHT * 0.5f - buttonHeight * 0.5f;
    const float contentRegionStartY = ImGui::GetCursorPosY();

    ImGui::SetCursorPos(
        ImVec2(ImGui::GetCursorPosX() + 10.0f, buttonStartY)
    );

    if (!inGamePreview) {
        if (ImGui::ImageButton("##PlayButton", (ImTextureID)&runTexture, ICON_SIZE)) {
            HandleScenePlay();
        }
        DrawTooltip("Play");
    } else {
        if (ImGui::ImageButton("##StopButton", (ImTextureID)&pauseTexture, ICON_SIZE)) {
            HandleSceneStop();
        }
        DrawTooltip("Stop");
    }

    ImGui::SameLine();
    ImGui::SetCursorPosY(buttonStartY);

    if (ImGui::Button("+", ImVec2(buttonHeight, buttonHeight))) {
        if (!inGamePreview) showObjectTypePopup = true;
    }
    DrawTooltip("Add Object");

    ImGui::EndGroup();

    PopMenuBarStyle();

    sceneEditorMenuHeight = ImGui::GetCursorPosY() - menuBarYStart;
}

void SceneEditor::EditorCamera() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(ICON_FA_VIDEO " Scene Editor", NULL);

    DrawSceneEditorMenu();
    std::chrono::high_resolution_clock::time_point sceneEditorStart =
        std::chrono::high_resolution_clock::now();

    if (ImGui::IsWindowHovered() && !editorGizmo.isGizmoActive() && !inGamePreview) {
        DropEntity();
        ProcessObjectControls();
    }

    if (ImGui::IsWindowFocused() ||
        (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) &&
            !inGamePreview) {
        ProcessCopy();
        ProcessDeletion();

        if (!showObjectTypePopup)
            EditorCameraMovement();
    } else
        lastMousePosition = GetMousePosition();

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
        std::chrono::duration_cast<std::chrono::milliseconds>(sceneEditorEnd - sceneEditorStart);

    ImGui::End();
    ImGui::PopStyleVar();
}