#include "../../../include_all.h"
#include "SceneEditor.h"
#include "Gizmo/Gizmo.cpp"

void InitEditorCamera() {
    viewportRenderTexture = LoadRenderTexture( 1, 1 );
    viewportTexture = viewportRenderTexture.texture;

    sceneCamera.position = { 35.0f, 5.0f, 0.0f };
    sceneCamera.target = { 0.0f, 0.0f, 0.0f };
    sceneCamera.up = { 0.0f, 1.0f, 0.0f };

    sceneCamera.fovy = 60.0f;
    sceneCamera.projection = CAMERA_PERSPECTIVE;
}

float GetImGuiWindowTitleHeight() {
    return ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
}

void CalculateTextureRect(const Texture* texture, Rectangle& viewportRectangle) {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    viewportRectangle.x = windowPos.x;
    viewportRectangle.y = windowPos.y;

    viewportRectangle.width = windowSize.x;
    viewportRectangle.height = windowSize.y - GetImGuiWindowTitleHeight();
}

void DrawTextureOnViewportRectangle(const Texture* texture) {
    CalculateTextureRect(texture, viewportRectangle);

    ImGui::Image((ImTextureID)texture, ImVec2(viewportRectangle.width, viewportRectangle.height), ImVec2(0,1), ImVec2(1,0));
}

void EditorCameraMovement() {
    sceneCamera.calculateVectors();

    Vector3 DeltaTimeVec3 = { GetFrameTime(), GetFrameTime(), GetFrameTime() };
    movingEditorCamera = false;

    if (IsKeyDown(KEY_W)) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.front, movementSpeed);
        sceneCamera.position = Vector3Add(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Add(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_S)) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.front, movementSpeed);
        sceneCamera.position = Vector3Subtract(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Subtract(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_A)) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.right, -movementSpeed);
        sceneCamera.position = Vector3Add(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_D)) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.right, -movementSpeed);
        sceneCamera.position = Vector3Subtract(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_Q)) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.up, movementSpeed);
        sceneCamera.position = Vector3Subtract(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_E)) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.up, movementSpeed);
        sceneCamera.position = Vector3Add(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (GetMouseWheelMove() != 0 && ImGui::IsWindowHovered()) {
        movingEditorCamera = true;
        CameraMoveToTarget(&sceneCamera, -GetMouseWheelMove());
    }

    sceneCamera.target = Vector3Add(sceneCamera.position, sceneCamera.front);

    if (IsKeyDown(KEY_LEFT_SHIFT))         movementSpeed = fastCameraSpeed;
    else if (IsKeyDown(KEY_LEFT_CONTROL))  movementSpeed = slowCameraSpeed;
    else                                   movementSpeed = defaultCameraSpeed;

    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mousePosition = GetMousePosition();
        float angleX = (rlLastMousePosition.x - mousePosition.x) * 0.005f;
        float angleY = (rlLastMousePosition.y - mousePosition.y) * 0.005f;

        CameraYaw(&sceneCamera, angleX, false);
        CameraPitch(&sceneCamera, angleY, true, false, false);

        rlLastMousePosition = mousePosition;
    } else rlLastMousePosition = GetMousePosition();
}

bool IsMouseHoveringModel(const Model& model, const Vector3& position, const Vector3& rotation, const Vector3& scale, const Entity* entity = nullptr, bool bypassOptimization = false) {
    if (!IsModelReady(model)) return false;

    Vector2 relativeMousePosition = {
        (float)GetMousePosition().x - (float)viewportRectangle.x,
        (float)GetMousePosition().y - (float)viewportRectangle.y - (float)GetImGuiWindowTitleHeight()
    };

    Ray mouseRay = GetScreenToWorldRayEx(relativeMousePosition, sceneCamera, viewportRectangle.width, viewportRectangle.height);    

    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++) {
        BoundingBox meshBounds = (entity == nullptr) ? GetMeshBoundingBox(model.meshes[meshIndex]) : entity->bounds;

        meshBounds.min = Vector3Transform(meshBounds.min, model.transform);
        meshBounds.max = Vector3Transform(meshBounds.max, model.transform);

        if (bypassOptimization || GetRayCollisionBox(mouseRay, meshBounds).hit) {
            if (GetRayCollisionMesh(mouseRay, model.meshes[meshIndex], model.transform).hit)
                return true;
        }
    }

    return false;
}

void LocateEntity(Entity& entity) {
    if (selectedGameObjectType == "entity") {
        sceneCamera.target = entity.position;
        sceneCamera.position = {
            entity.position.x + 10,
            entity.position.y + 2,
            entity.position.z
        };
    }
}

void ProcessCameraControls() {
    if (IsKeyPressed(KEY_F))
        LocateEntity(*selectedEntity);
}

void ProcessGizmo() {
    if (selectedGameObjectType == "entity" && selectedEntity) {
        GizmoPosition();
        GizmoRotation();
        GizmoScale();
    } else if (selectedGameObjectType == "light" && selectedLight) {
        GizmoPosition();
        GizmoRotation();
    } else {
        draggingGizmoPosition = draggingGizmoRotation = draggingGizmoScale = false;
    }

    dragging = (draggingGizmoScale || draggingGizmoPosition || draggingGizmoRotation);
}

void HandleUnselect() {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ImGui::IsWindowHovered() && !dragging) selectedGameObjectType = "none";
}

void RenderViewportTexture() {
    DrawTextureOnViewportRectangle(&viewportTexture);
}

void ApplyBloomEffect() {
    if (!bloomEnabled) return;

    BeginTextureMode(downsamplerTexture);
    BeginShaderMode(downsamplerShader);
    SetShaderValueTexture(downsamplerShader, GetShaderLocation(downsamplerShader, "srcTexture"), viewportTexture);
        DrawTexture(viewportTexture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(upsamplerTexture);
    BeginShaderMode(upsamplerShader);
    SetShaderValueTexture(upsamplerShader, GetShaderLocation(upsamplerShader, "srcTexture"), downsamplerTexture.texture);
        DrawTexture(downsamplerTexture.texture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    viewportTexture = upsamplerTexture.texture;
}

void RenderLight() {
    for (Light& light : lights) {
        lightModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = lightTexture;

        float rotation = DrawBillboardRotation(sceneCamera, lightTexture, { light.position.x, light.position.y, light.position.z }, 1.0f, WHITE);

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging) {
            bool isLightSelected = IsMouseHoveringModel(lightModel, { light.position.x, light.position.y, light.position.z }, { 0, rotation, 0 }, {1,1,1});
            if (isLightSelected) {
                selectedLight = &light;
                selectedGameObjectType = "light";
            }
        }
    }
}

void RenderEntities() {
    for (Entity& entity : entitiesListPregame) {
        entity.calcPhysics = false;
        entity.render();

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging) {
            bool isEntitySelected = IsMouseHoveringModel(entity.model, entity.position, entity.rotation, entity.scale, &entity);
            if (isEntitySelected) {
                selectedEntity = &entity;
                selectedGameObjectType = "entity";
            }

            for (auto childVariant : entity.children) {
                if (Entity** childEntity = std::any_cast<Entity*>(&childVariant)) {
                    bool isEntitySelected = IsMouseHoveringModel((*childEntity)->model, (*childEntity)->position, (*childEntity)->rotation, (*childEntity)->scale);
                    if (isEntitySelected) {
                        selectedEntity = *childEntity;
                        selectedGameObjectType = "entity";
                    }
                }
            }
        }
    }
}

void UpdateShader() {
    float cameraPos[3] = { sceneCamera.position.x, sceneCamera.position.y, sceneCamera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
}

void RenderScene() {
    BeginTextureMode(viewportRenderTexture);
        BeginMode3D(sceneCamera);
            ClearBackground(GRAY);

            DrawSkybox();
            DrawGrid(GRID_SIZE, GRID_SCALE);

            if (ImGui::IsWindowFocused())
                ProcessGizmo();
    
            UpdateShader();

            RenderLight();
            RenderEntities();

            HandleUnselect();

            UpdateInGameGlobals();
            UpdateLightsBuffer(true);
        EndMode3D();

        DrawTextElements();
        DrawButtons();
    EndTextureMode();

    ApplyBloomEffect();
    RenderViewportTexture();
}

void DropEntity() {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    ImRect dropTargetArea(windowPos, windowPos + windowSize);
    ImGuiID windowID = ImGui::GetID(ImGui::GetCurrentWindow()->Name);

    if (ImGui::BeginDragDropTargetCustom(dropTargetArea, windowID)) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));

            int payloadIndex = *(const int*)payload->Data;

            std::string modelFilePath = dirPath.string() + "/" + filesTextureStruct[payloadIndex].name;

            size_t lastDotIndex = modelFilePath.find_last_of('.');
            std::string entityName = modelFilePath.substr(0, lastDotIndex);

            AddEntity(true, false, modelFilePath.c_str(), Model(), entityName);
        }

        ImGui::EndDragDropTarget();
    }
}

void ProcessObjectControls() {
    if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) && IsKeyDown(KEY_A) && !movingEditorCamera) {
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
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::IsWindowHovered() && showObjectTypePopup)
        ImGui::OpenPopup("popup");

    if (ImGui::BeginPopup("popup")) {
        ImGui::Text("Add an Object");
        
        ImGui::Separator();

        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Cube")) {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cube;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cone")) {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCone(.5, 1, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cone;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cylinder")) {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCylinder(1.5, 2, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cylinder;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Plane")) {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Plane;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Sphere")) {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshSphere(.5, 50, 50)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Sphere;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Torus")) {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshTorus(.5, 1, 30, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Torus;
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Point Light")) {
                NewLight({0, 0, 5}, WHITE, LIGHT_POINT);
                selectedLight = &lights.back();
                selectedGameObjectType = "light";
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Directional Light")) {
                NewLight({0, 0, 5}, WHITE, LIGHT_DIRECTIONAL);
                selectedLight = &lights.back();
                selectedGameObjectType = "light";
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Spot Light")) {
                selectedLight = &NewLight({0, 0, 5}, WHITE, LIGHT_SPOT);
                selectedLight = &lights.back();
                selectedGameObjectType = "light";
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("GUI")) {
            if (ImGui::MenuItem("Text")) {
                selectedTextElement = &AddText("Default Text", { 100, 100, 1 }, 20, BLUE);
                selectedGameObjectType = "text";
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Button")) {
                selectedButton = &AddButton("Default Text", { 100, 150, 1 }, {200, 50});
                selectedGameObjectType = "button";
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        showObjectTypePopup = false;

    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(5);
}

void ProcessDeletion() {
    if (IsKeyPressed(KEY_DELETE)) {
        if (selectedGameObjectType == "entity" && selectedEntity)
            selectedEntity->remove();
        else if (selectedGameObjectType == "light" && selectedLight) {
            auto it = std::find(lights.begin(), lights.end(), *selectedLight);
            if (it != lights.end()) {
                size_t index = std::distance(lights.begin(), it);
                if (index < lightsInfo.size()) {
                    lights.erase(it);
                    lightsInfo.erase(lightsInfo.begin() + index);
                }
            }
        }
    }
}

void EntityPaste(const std::shared_ptr<Entity>& entity) {
    if (entity) {
        Entity newEntity = *entity;
        newEntity.reloadRigidBody();
        newEntity.id = GenerateUniqueID(entitiesListPregame);
        entitiesListPregame.emplace_back(newEntity);
        selectedGameObjectType = "entity";
        selectedEntity = &entitiesListPregame.back();
    }
}

void LightPaste(const std::shared_ptr<Light>& light) {
    if (light) {
        Light newLight = *light;
        newLight.id = lights.back().id+1;
        lights.push_back(newLight);
        AdditionalLightInfo newLightInfo;
        newLightInfo.id = newLight.id;
        lightsInfo.push_back(newLightInfo);
        
        selectedGameObjectType = "light";
        selectedLight = &lights.back();
    }
}

void DuplicateEntity(Entity& entity) {
    Entity newEntity = entity;
    newEntity.reloadRigidBody();
    entitiesListPregame.reserve(1);
    entitiesListPregame.emplace_back(newEntity);
    selectedEntity = &entitiesListPregame.back();
}

void ProcessCopy() {
    if (IsKeyPressed(KEY_C) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) && !movingEditorCamera)) {
        if (selectedGameObjectType == "entity") {
            currentCopyType = CopyType_Entity;
            if (selectedEntity) copiedEntity = std::make_shared<Entity>(*selectedEntity);
        } else if (selectedGameObjectType == "light") {
            currentCopyType = CopyType_Light;
            copiedLight = std::make_shared<Light>(*selectedLight);
        }
    }

    if (IsKeyPressed(KEY_V) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        if (currentCopyType == CopyType_Entity)
            EntityPaste(copiedEntity);
        if (currentCopyType == CopyType_Light)
            LightPaste(copiedLight);
    }


    if (IsKeyDown(KEY_D) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && !inGamePreview && selectedGameObjectType == "entity" && canDuplicateEntity) {
        DuplicateEntity(*selectedEntity);
        canDuplicateEntity = false;
    }

    if (!canDuplicateEntity) canDuplicateEntity = IsKeyUp(KEY_D);
}

void ScaleViewport() {
    ImVec2 currentWindowSize = ImGui::GetWindowSize();

    if (currentWindowSize.x != prevEditorWindowSize.x || currentWindowSize.y != prevEditorWindowSize.y) {
        UnloadRenderTexture(viewportRenderTexture);
        viewportRenderTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        viewportTexture = viewportRenderTexture.texture;

        UnloadRenderTexture(downsamplerTexture);
        UnloadRenderTexture(upsamplerTexture);

        downsamplerTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        upsamplerTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);

        prevEditorWindowSize = currentWindowSize;
    }
}

int EditorCamera() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(ICON_FA_VIDEO " Scene Editor", NULL);

    std::chrono::high_resolution_clock::time_point sceneEditorStart = std::chrono::high_resolution_clock::now();

    if (ImGui::IsWindowHovered() && !dragging && !inGamePreview) {
        DropEntity();
        ProcessObjectControls();
    }

    if (ImGui::IsWindowFocused() || (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) && !inGamePreview) {
        ProcessCameraControls();
        ProcessCopy();

        if (!showObjectTypePopup) EditorCameraMovement();
    }
    else rlLastMousePosition = GetMousePosition();

    if (inGamePreview) {
        RunGame();
        return;
    }

    ProcessDeletion();
    RenderScene();
    ScaleViewport();
    ObjectsPopup();

    std::chrono::high_resolution_clock::time_point sceneEditorEnd = std::chrono::high_resolution_clock::now();
    sceneEditorProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(sceneEditorEnd - sceneEditorStart);

    ImGui::End();
    ImGui::PopStyleVar();
}