void InitEditorCamera() {
    viewportRenderTexture = LoadRenderTexture( 1, 1 );
    viewportTexture = viewportRenderTexture.texture;

    sceneCamera.position = { 35.0f, 5.0f, 0.0f };
    sceneCamera.target = { 0.0f, 0.0f, 0.0f };
    sceneCamera.up = { 0.0f, 1.0f, 0.0f };

    sceneCamera.fovy = 60.0f;
    sceneCamera.projection = CAMERA_PERSPECTIVE;
}

static inline double GetImGuiWindowTitleHeight() {
    return static_cast<float>(ImGui::GetTextLineHeight()) + ImGui::GetStyle().FramePadding.y * 2.0;
}

void CalculateTextureRect(const Texture* texture, Rectangle& viewportRectangle) {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    viewportRectangle.x = windowPos.x;
    viewportRectangle.y = windowPos.y;
    viewportRectangle.width = windowSize.x;
    viewportRectangle.height = windowSize.y - GetImGuiWindowTitleHeight() - 60.0;
}

void DrawTextureOnViewportRectangle(const Texture* texture) {
    CalculateTextureRect(texture, viewportRectangle);
    ImGui::SetCursorPos(ImVec2(0, GetImGuiWindowTitleHeight() + 60.0));
    ImGui::Image((ImTextureID)texture, ImVec2(viewportRectangle.width, viewportRectangle.height), ImVec2(0,1), ImVec2(1,0));
}

void EditorCameraMovement() {
    sceneCamera.calculateVectors();
    Vector3 DeltaTimeVec3 = { GetFrameTime(), GetFrameTime(), GetFrameTime() };
    movingEditorCamera = false;

    auto MoveCamera = [&](Vector3 direction, bool scaleNegative = false) {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(direction, scaleNegative ? -movementSpeed : movementSpeed);
        sceneCamera.position = Vector3Add(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Add(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    };

    if (IsKeyDown(KEY_W)) MoveCamera(sceneCamera.front);
    if (IsKeyDown(KEY_S)) MoveCamera(sceneCamera.front, true);
    if (IsKeyDown(KEY_D)) MoveCamera(sceneCamera.right);
    if (IsKeyDown(KEY_A)) MoveCamera(sceneCamera.right, true);
    if (IsKeyDown(KEY_Q)) MoveCamera(sceneCamera.up, true);
    if (IsKeyDown(KEY_E)) MoveCamera(sceneCamera.up);

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

bool IsMouseHoveringModel(const Model& model, const Vector3& position, const Vector3& rotation, const Vector3& scale, const Entity* entity, bool bypassOptimization) {
    if (!IsModelReady(model)) return false;

    Vector2 mousePosition = GetMousePosition();
#ifndef GAME_SHIPPING
    Vector2 relativeMousePosition = {
        (float)mousePosition.x - (float)viewportRectangle.x,
        (float)mousePosition.y - (float)viewportRectangle.y - (float)GetImGuiWindowTitleHeight() - 60.0f
    };
#elif GAME_SHIPPING
    Vector2 relativeMousePosition = {
        (float)mousePosition.x - (float)viewportRectangle.x,
        (float)mousePosition.y - (float)viewportRectangle.y - (float)GetImGuiWindowTitleHeight()
    };
#endif

    Ray mouseRay = GetScreenToWorldRayEx(relativeMousePosition, sceneCamera, viewportRectangle.width, viewportRectangle.height);

    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++) {
        BoundingBox meshBounds;
        if (entity == nullptr) {
            meshBounds =  GetMeshBoundingBox(model.meshes[meshIndex]);
            meshBounds.min = Vector3Transform(meshBounds.min, model.transform);
            meshBounds.max = Vector3Transform(meshBounds.max, model.transform);
        } else meshBounds = entity->bounds;

        if (bypassOptimization || GetRayCollisionBox(mouseRay, meshBounds).hit) {
            if (GetRayCollisionMesh(mouseRay, model.meshes[meshIndex], model.transform).hit) {
                return true;
            }
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
    if (IsKeyPressed(KEY_F)) LocateEntity(*selectedEntity);
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
        draggingPositionGizmo = draggingRotationGizmo = draggingScaleGizmo = false;
    }

    dragging = (draggingScaleGizmo || draggingPositionGizmo || draggingRotationGizmo);
}

void HandleUnselect() {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ImGui::IsWindowHovered() && !dragging) selectedGameObjectType = "none";
}

void RenderViewportTexture() {
    bloomEnabled ? DrawTextureOnViewportRectangle(&upsamplerTexture.texture) : DrawTextureOnViewportRectangle(&viewportTexture);
}

void ApplyBloomEffect() {
    if (!bloomEnabled) return;

    BeginTextureMode(horizontalBlurTexture);
    BeginShaderMode(horizontalBlurShader);
    SetShaderValueTexture(horizontalBlurShader, GetUniformLocation(horizontalBlurShader, "srcTexture"), viewportTexture);
        DrawTexture(viewportTexture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(verticalBlurTexture);
    BeginShaderMode(verticalBlurShader);
    SetShaderValueTexture(verticalBlurShader, GetUniformLocation(verticalBlurShader, "srcTexture"), horizontalBlurTexture.texture);
        DrawTexture(horizontalBlurTexture.texture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(upsamplerTexture);
    BeginShaderMode(upsamplerShader);
    SetShaderValueTexture(upsamplerShader, GetUniformLocation(upsamplerShader, "downsampledTexture"), verticalBlurTexture.texture);
    SetShaderValueTexture(upsamplerShader, GetUniformLocation(upsamplerShader, "originalTexture"), viewportTexture);
        DrawTexture(verticalBlurTexture.texture, 0, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void RenderLights() {
    for (LightStruct& lightStruct : lights) {
        lightModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = lightTexture;

        float rotation = DrawBillboardRotation(sceneCamera, lightTexture, { lightStruct.light.position.x, lightStruct.light.position.y, lightStruct.light.position.z }, 1.0f, WHITE);

        if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON) || !ImGui::IsWindowHovered() || dragging) continue;

        Matrix transformMatrix = MatrixMultiply(MatrixMultiply(MatrixScale(1, 1, 1),
                                                            MatrixRotateXYZ(Vector3Scale({0, rotation, 0}, DEG2RAD))),
                                                MatrixTranslate(lightStruct.light.position.x, lightStruct.light.position.y, lightStruct.light.position.z));

        lightModel.transform = transformMatrix;

        bool isLightSelected = IsMouseHoveringModel(lightModel, { lightStruct.light.position.x, lightStruct.light.position.y, lightStruct.light.position.z }, { 0, rotation, 0 }, {1,1,1});
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
        if (entity.isChild) continue;

        entity.calcPhysics = false;
        entity.render();

        if (!isMouseDown || !isWindowHovered || dragging) continue;

        if (IsMouseHoveringModel(entity.model, entity.position, entity.rotation, entity.scale, &entity)) {
            selectedEntity = &entity;
            selectedGameObjectType = "entity";
        }

        for (int childEntityIndex : entity.entitiesChildren) {
            Entity& childEntity = *getEntityById(childEntityIndex);

            if (IsMouseHoveringModel(childEntity.model, childEntity.position, childEntity.rotation, childEntity.scale)) {
                selectedEntity = &childEntity;
                selectedGameObjectType = "entity";
            }
        }
    }
}

void UpdateShader() {
    float cameraPos[3] = { sceneCamera.position.x, sceneCamera.position.y, sceneCamera.position.z };
    SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), cameraPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetUniformLocation(shader, "exposure"), &prevExposure, SHADER_UNIFORM_FLOAT);

    for (Entity& entity : entitiesListPregame) {
        SetShaderValue(entity.getShader(), GetShaderLocation(entity.getShader(), "viewPos"), cameraPos, SHADER_UNIFORM_VEC3);
        SetShaderValue(entity.getShader(), GetUniformLocation(entity.getShader(), "exposure"), &prevExposure, SHADER_UNIFORM_FLOAT);
    }

    SetShaderValue(instancingShader, instancingShader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
}

void RenderGrid() {
    constexpr float halfGridSize = (GRID_SIZE * 0.5f) * GRID_SCALE;

    for (int x = 0; x <= GRID_SIZE; x++) {
        const float xScaled = (x * GRID_SCALE) - halfGridSize;

        DrawLine3D({ xScaled, 0.0f, -halfGridSize }, { xScaled, 0.0f, halfGridSize }, WHITE);
        DrawLine3D({ -halfGridSize, 0.0f, xScaled }, { halfGridSize, 0.0f, xScaled }, WHITE);
    }
}

void ComputeSceneLuminance() {
    BeginTextureMode(downsamplerTexture);
    BeginShaderMode(downsamplerShader);
    SetShaderValue(downsamplerShader, GetUniformLocation(downsamplerShader, "uDownsampleFactor"), &downsamplerFactor, SHADER_UNIFORM_INT);
    SetShaderValueTexture(downsamplerShader, GetUniformLocation(downsamplerShader, "srcTexture"), viewportRenderTexture.texture);
        DrawTexture(viewportRenderTexture.texture, 0, 0, WHITE);

        GLuint pixelCount = 0;
        GLuint totalLuminance = 0;
        constexpr GLuint zero = 0;

        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, downsamplerPixelCountBuffer);
        glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &pixelCount);

        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);


        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, downsamplerLuminanceBuffer);
        glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &totalLuminance);

        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);

        if (pixelCount > 0) {
            float avgLuminance = (totalLuminance / float(pixelCount)) / 1000.0f;

            static float targetLuminance = 0.6f;
            static float exposureSpeed = 2.8f;
            float deltaTime = GetFrameTime();
            float exposure = targetLuminance / (avgLuminance + 0.0001f);
            exposure = Lerp(prevExposure, exposure, deltaTime * exposureSpeed);
            prevExposure = exposure;
        }

    EndShaderMode();
    EndTextureMode();
}

void RenderScene() {
    BeginTextureMode(viewportRenderTexture);
        BeginMode3D(sceneCamera);
            ClearBackground(GRAY);

            skybox.setExposure(prevExposure);
            skybox.drawSkybox(sceneCamera);

            UpdateLightsBuffer(true, lights);
            UpdateInGameGlobals();
            UpdateFrustum();
            UpdateShader();

            ProcessGizmo();

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
    EndTextureMode();

    ComputeSceneLuminance();
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

            std::string modelFilePath = fs::path(dirPath / fileStruct[payloadIndex].name).string();

            size_t lastDotIndex = modelFilePath.find_last_of('.');
            std::string entityName = modelFilePath.substr(0, lastDotIndex);

            AddEntity(modelFilePath.c_str(), Model(), entityName);
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
                AddEntity("", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cube;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cone")) {
                AddEntity("", LoadModelFromMesh(GenMeshCone(.5, 1, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cone;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cylinder")) {
                AddEntity("", LoadModelFromMesh(GenMeshCylinder(1.5, 2, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cylinder;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Plane")) {
                AddEntity("", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Plane;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Sphere")) {
                AddEntity("", LoadModelFromMesh(GenMeshSphere(.5, 50, 50)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Sphere;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Torus")) {
                AddEntity("", LoadModelFromMesh(GenMeshTorus(.5, 1, 30, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Torus;
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
    if (!IsKeyPressed(KEY_DELETE)) return;

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
    if (!lightStruct) return;

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

void DuplicateLight(LightStruct& lightStruct, Entity* parent = nullptr) {
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

void DuplicateEntity(Entity& entity, Entity* parent = nullptr) {
    Entity newEntity = entity;
    std::vector<int> oldEntityChildrenIds = newEntity.entitiesChildren;
    std::vector<int> oldLightChildrenIds  = newEntity.lightsChildren;
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

void EntityPaste(const std::shared_ptr<Entity>& entity, Entity* parent = nullptr) {
    if (!entity) return;

    Entity newEntity = *entity;
    std::vector<int> oldEntityChildrenIds = newEntity.entitiesChildren;
    std::vector<int> oldLightChildrenIds  = newEntity.lightsChildren;
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
    if (IsKeyPressed(KEY_C) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) && !movingEditorCamera)) {
        if (selectedGameObjectType == "entity") {
            currentCopyType = CopyType_Entity;
            if (selectedEntity) copiedEntity = std::make_shared<Entity>(*selectedEntity);
        } else if (selectedGameObjectType == "light") {
            currentCopyType = CopyType_Light;
            copiedLight = std::make_shared<LightStruct>(*selectedLight);
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

        UnloadRenderTexture(verticalBlurTexture);
        UnloadRenderTexture(horizontalBlurTexture);
        UnloadRenderTexture(upsamplerTexture);

        verticalBlurTexture   = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        horizontalBlurTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        upsamplerTexture      = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        downsamplerTexture    = LoadRenderTexture(currentWindowSize.x / downsamplerFactor,
                                                  currentWindowSize.y / downsamplerFactor);

        prevEditorWindowSize = currentWindowSize;
    }
}

void drawEditorCameraMenu() {
    float windowWidth = ImGui::GetWindowWidth();

    const float buttonPadding = 5.0f;
    constexpr ImVec2 imgButtonSize(17, 17);
    static float buttonOffsetY = GetImGuiWindowTitleHeight() + 60.0f * 0.5f - imgButtonSize.y * 0.5f - buttonPadding * 2;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(buttonPadding, 10));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

    ImGui::SetCursorPos(ImVec2(0, GetImGuiWindowTitleHeight()));
    ImGui::BeginGroup();

    ImGui::Dummy(ImVec2(0, 60.0f));
    ImGui::SameLine();

    ImGui::SetCursorPosY(buttonOffsetY);

    if (ImGui::ImageButton("runTex", (ImTextureID)&runTexture, imgButtonSize) && !inGamePreview) {
        eventManager.onScenePlay.triggerEvent();

        for (Entity& entity : entitiesListPregame) entity.reloadRigidBody();
        entitiesList.assign(entitiesListPregame.begin(), entitiesListPregame.end());

        physics.backup();
        InitGameCamera();
       inGamePreview = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play the game");

    ImGui::SameLine();
    ImGui::SetCursorPosY(buttonOffsetY);

    if ((ImGui::ImageButton("pauseTex", (ImTextureID)&pauseTexture, imgButtonSize)) && inGamePreview || IsKeyDown(KEY_ESCAPE)) {
        eventManager.onSceneStop.triggerEvent();
        EnableCursor();

        inGamePreview = false;
        firstTimeGameplay = true;

        physics.unBackup();
        for (Entity& entity : entitiesListPregame) entity.resetPhysics();
        for (Entity& entity : entitiesList) entity.resetPhysics();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop the game");

    ImGui::SameLine();
    ImGui::SetCursorPosY(buttonOffsetY);

    static ImVec2 buttonSize = ImVec2(imgButtonSize.x + ImGui::GetStyle().FramePadding.x * 2.0f,
                                        imgButtonSize.y + ImGui::GetStyle().FramePadding.y * 2.0f);

    if ((ImGui::Button("+", buttonSize)) && !inGamePreview) {
        showObjectTypePopup = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add objects");

    ImGui::EndGroup();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

void EditorCamera() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(ICON_FA_VIDEO " Scene Editor", NULL);

    drawEditorCameraMenu();
    std::chrono::high_resolution_clock::time_point sceneEditorStart = std::chrono::high_resolution_clock::now();

    if (ImGui::IsWindowHovered() && !dragging && !inGamePreview) {
        DropEntity();
        ProcessObjectControls();
    }

    if (ImGui::IsWindowFocused() || (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) && !inGamePreview) {
        ProcessCameraControls();
        ProcessCopy();
        ProcessDeletion();

        if (!showObjectTypePopup) EditorCameraMovement();
    }
    else rlLastMousePosition = GetMousePosition();

    if (inGamePreview) {
        RunGame();

        std::chrono::high_resolution_clock::time_point sceneEditorEnd = std::chrono::high_resolution_clock::now();
        sceneEditorProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(sceneEditorEnd - sceneEditorStart);

        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    RenderScene();
    ScaleViewport();
    ObjectsPopup();

    std::chrono::high_resolution_clock::time_point sceneEditorEnd = std::chrono::high_resolution_clock::now();
    sceneEditorProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(sceneEditorEnd - sceneEditorStart);

    ImGui::End();
    ImGui::PopStyleVar();
}