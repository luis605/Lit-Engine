#include "../../../include_all.h"
#include "SceneEditor.h"
#include "Gizmo/Gizmo.cpp"

void InitEditorCamera()
{
    renderTexture = LoadRenderTexture( 1, 1 );
    texture = renderTexture.texture;

    sceneCamera.position = { 50.0f, 0.0f, 0.0f };
    sceneCamera.target = { 0.0f, 0.0f, 0.0f };
    sceneCamera.up = { 0.0f, 1.0f, 0.0f };

    Vector3 front = Vector3Subtract(sceneCamera.target, sceneCamera.position);
    front = Vector3Normalize(front);

    sceneCamera.fovy = 60.0f;
    sceneCamera.projection = CAMERA_PERSPECTIVE;
}


float GetImGuiWindowTitleHeight() {
    ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f;
}

void CalculateTextureRect(const Texture* texture, Rectangle& rectangle) {
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    rectangle.width = windowSize.x;
    rectangle.height = windowSize.y - GetImGuiWindowTitleHeight();

    rectangle.x = windowPos.x;
    rectangle.y = windowPos.y;
}

void DrawTextureOnRectangle(const Texture* texture) {
    CalculateTextureRect(texture, rectangle);

    ImGui::Image((ImTextureID)texture, ImVec2(rectangle.width, rectangle.height), ImVec2(0,1), ImVec2(1,0));
}

void EditorCameraMovement(void)
{
    // Update Camera Position
    front = Vector3Subtract(sceneCamera.target, sceneCamera.position);
    front = Vector3Normalize(front);

    Vector3 forward = Vector3Subtract(sceneCamera.target, sceneCamera.position);
    Vector3 right = Vector3CrossProduct(front, sceneCamera.up);
    Vector3 normalizedRight = Vector3Normalize(right);
    Vector3 DeltaTimeVec3 = { GetFrameTime(), GetFrameTime(), GetFrameTime() };

    movingEditorCamera = false;

    if (IsKeyDown(KEY_W))
    {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(front, movementSpeed);
        sceneCamera.position = Vector3Add(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Add(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_S))
    {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(front, movementSpeed);
        sceneCamera.position = Vector3Subtract(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Subtract(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_A))
    {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(normalizedRight, -movementSpeed);
        sceneCamera.position = Vector3Add(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Add(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_D))
    {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(normalizedRight, -movementSpeed);
        sceneCamera.position = Vector3Subtract(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Subtract(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_Q))
    {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.up, movementSpeed);
        sceneCamera.position = Vector3Subtract(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Subtract(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_E))
    {
        movingEditorCamera = true;
        Vector3 movement = Vector3Scale(sceneCamera.up, movementSpeed);
        sceneCamera.position = Vector3Add(sceneCamera.position, Vector3Multiply(movement, DeltaTimeVec3));
        sceneCamera.target = Vector3Add(sceneCamera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (GetMouseWheelMove() != 0 && ImGui::IsWindowHovered())
    {
        movingEditorCamera = true;
        CameraMoveToTarget(&sceneCamera, -GetMouseWheelMove());
    }

    sceneCamera.target = Vector3Add(sceneCamera.position, forward);


    if (IsKeyDown(KEY_LEFT_SHIFT))
        movementSpeed = fastCameraSpeed;
    else if (IsKeyDown(KEY_LEFT_CONTROL))
        movementSpeed = slowCameraSpeed;
    else
        movementSpeed = defaultCameraSpeed;


    // Camera Rotation
    static Vector2 lastMousePosition = { 0 };
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
    {
        Vector2 mousePosition = GetMousePosition();
        float angleX = (lastMousePosition.x - mousePosition.x) * 0.005f;
        float angleY = (lastMousePosition.y - mousePosition.y) * 0.005f;

        Camera *cameraPtr = (Camera*)(&sceneCamera);
        CameraYaw(cameraPtr, angleX, false);
        CameraPitch(cameraPtr, angleY, true, false, false);

        lastMousePosition = mousePosition;
    }
    else
    {
        lastMousePosition = GetMousePosition();
    }
}


bool IsMouseHoveringModel(const Model& model, const Camera& camera, const Vector3& position, const Vector3& rotation, const Vector3& scale, const Entity* entity, bool bypassOptimization)
{
    if (model.meshCount <= 0) {
        return false;
    }

    Vector3 originalSize = Vector3Zero();

    if (Vector3Equals(scale, Vector3Zero())) {
        originalSize = Vector3Subtract(entity ? entity->bounds.max : GetMeshBoundingBox(model.meshes[0]).max,
                                      entity ? entity->bounds.min : GetMeshBoundingBox(model.meshes[0]).min);
    }

    Vector2 relativeMousePosition = {
        (float)GetMousePosition().x - (float)rectangle.x,
        (float)GetMousePosition().y - (float)rectangle.y - (float)GetImGuiWindowTitleHeight()
    };

    Ray mouseRay = GetScreenToWorldRayEx(relativeMousePosition, camera, rectangle.width, rectangle.height);
    
    RayCollision meshCollisionInfo = { 0 };


    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++) {
        BoundingBox meshBounds = (entity == nullptr) ? GetMeshBoundingBox(model.meshes[meshIndex]) : entity->bounds;

        // Transform the mesh bounding box based on the model's transform
        meshBounds.min = Vector3Transform(meshBounds.min, model.transform);
        meshBounds.max = Vector3Transform(meshBounds.max, model.transform);

        if (bypassOptimization || GetRayCollisionBox(mouseRay, meshBounds).hit) {
            meshCollisionInfo = GetRayCollisionMesh(mouseRay, model.meshes[meshIndex], model.transform);
            if (meshCollisionInfo.hit) {
                return true;
            }
        }
    }

    return false;
}

void LocateEntity(Entity& entity)
{
    if (selectedGameObjectType == "entity")
    {
        sceneCamera.target = entity.position;
        sceneCamera.position = {
            entity.position.x + 10,
            entity.position.y + 2,
            entity.position.z
        };
    }
}

void ProcessCameraControls()
{
    if (IsKeyPressed(KEY_F))
    {
        LocateEntity(*selectedEntity);
    }
}

void ProcessGizmo()
{
    if (selectedGameObjectType == "entity" && selectedEntity)
    {
        if (selectedEntity->initialized)
        {
            GizmoPosition();
            GizmoRotation();
            GizmoScale();
        }
    }
    else if (selectedGameObjectType == "light")
    {
        GizmoPosition();
    }
    else
    {
        dragging = false;
        draggingGizmoPosition = false;
        draggingGizmoRotation = false;
        draggingGizmoScale = false;
    }

    dragging = (draggingGizmoScale || draggingGizmoPosition || draggingGizmoRotation);
}

void HandleUnselect(bool isEntitySelected, bool isLightSelected) {
    if (
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        ImGui::IsWindowHovered() &&
        !isEntitySelected &&
        !isLightSelected &&
        !isHoveringGizmo &&
        !dragging
        )
    {
        selectedGameObjectType = "none";
    }
}

void ApplyBloomEffect() {
    if (bloomEnabled)
    {
        {
            BeginTextureMode(downsamplerTexture);
            BeginShaderMode(downsamplerShader);

            SetShaderValueTexture(downsamplerShader, GetShaderLocation(downsamplerShader, "srcTexture"), texture);

            DrawTexture(texture, 0, 0, WHITE);

            EndShaderMode();
            EndTextureMode();
        }

        {
            BeginTextureMode(upsamplerTexture);
            BeginShaderMode(upsamplerShader);

            SetShaderValueTexture(upsamplerShader, GetShaderLocation(upsamplerShader, "srcTexture"), downsamplerTexture.texture);

            DrawTexture(downsamplerTexture.texture, 0, 0, WHITE);

            EndShaderMode();
            EndTextureMode();
        }

        DrawTextureOnRectangle(&upsamplerTexture.texture);
    }
    else
    {
        DrawTextureOnRectangle(&texture);
    }

}

void RenderLight(Light* light, bool& isLightSelected) {
    int index = 0;

    for (Light& light : lights)
    {
        Model lightModel = LoadModelFromMesh(GenMeshPlane(4, 4, 1, 1));
        lightModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = lightTexture;

        float rotation = DrawBillboardRotation(sceneCamera, lightTexture, { light.position.x, light.position.y, light.position.z }, 1.0f, WHITE);
        
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging)
        {
            isLightSelected = IsMouseHoveringModel(lightModel, sceneCamera, { light.position.x, light.position.y, light.position.z }, { 0, rotation, 0 }, {1,1,1});
            if (isLightSelected)
            {
                objectInInspector = &light;
                selectedGameObjectType = "light";
            }
        }

        UnloadModel(lightModel);
    }
}

void RenderEntities(bool& isEntitySelected) {
    int index = 0;
    for (Entity& entity : entitiesListPregame)
    {
        entity.calcPhysics = false;
        entity.render();
        
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging)
        {
            isEntitySelected = IsMouseHoveringModel(entity.model, sceneCamera, entity.position, entity.rotation, entity.scale, &entity);
            if (isEntitySelected)
            {
                if (IsModelReady(entity.model) && entity.initialized)
                {
                    objectInInspector = &entitiesListPregame.at(index);
                    selectedGameObjectType = "entity";
                }
            }

            for (std::variant<Entity*, Light*, Text*, LitButton*>& childVariant : entity.children)
            {
                if (auto* childEntity = std::get_if<Entity*>(&childVariant))
                {
                    bool isEntitySelected = IsMouseHoveringModel((*childEntity)->model, sceneCamera, (*childEntity)->position, (*childEntity)->rotation, (*childEntity)->scale);
                    if (isEntitySelected)
                    {
                        objectInInspector = *childEntity;
                        selectedGameObjectType = "entity";
                    }
                }
            }
        }

        index++;
    }
}

void UpdateShaderAndView() {
    float cameraPos[3] = { sceneCamera.position.x, sceneCamera.position.y, sceneCamera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
}

void RenderScene() {
    BeginTextureMode(renderTexture);
    BeginMode3D(sceneCamera);

    ClearBackground(GRAY);

    DrawSkybox();
    DrawGrid(GRID_SIZE, GRID_SCALE);

    ProcessGizmo();

    UpdateShaderAndView();

    bool isLightSelected = false;
    bool isEntitySelected = false;


    for (Light& light : lights) {
        RenderLight(&light, isLightSelected);
    }

    RenderEntities(isEntitySelected);

    HandleUnselect(isEntitySelected, isLightSelected);

    UpdateInGameGlobals();
    UpdateLightsBuffer();

    EndMode3D();

    DrawTextElements();
    DrawButtons();

    EndTextureMode();

    ApplyBloomEffect();
}


void DropEntity()
{
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    ImRect dropTargetArea(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y));
    ImGuiID windowID = ImGui::GetID(ImGui::GetCurrentWindow()->Name);
   
    if (ImGui::BeginDragDropTargetCustom(dropTargetArea, windowID))
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_PAYLOAD"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            std::string path = dirPath.string();
            path += "/" + filesTextureStruct[payload_n].name;

            size_t lastDotIndex = path.find_last_of('.');

            // Extract the substring after the last dot
            std::string entityName = path.substr(0, lastDotIndex);
            
            AddEntity(true, false, path.c_str(), Model(), entityName);
        }
        ImGui::EndDragDropTarget();
    }
}

bool showObjectTypePopup = false;

void ProcessObjectControls()
{
    if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) && IsKeyDown(KEY_A) && !movingEditorCamera)
    {
        ImGui::OpenPopup("Add Object");
        showObjectTypePopup = true;
    }
}

void ObjectsPopup()
{
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

    if (ImGui::BeginPopup("popup"))
    {
        ImGui::Text("Add an Object");
        
        ImGui::Separator();

        if (ImGui::BeginMenu("Entity"))
        {
            if (ImGui::MenuItem("Cube"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cube;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cone"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCone(.5, 1, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cone;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cylinder"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCylinder(1.5, 2, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Cylinder;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Plane"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Plane;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Sphere"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshSphere(.5, 50, 50)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Sphere;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Torus"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshTorus(.5, 1, 30, 30)));
                entitiesListPregame.back().ObjectType = Entity::ObjectType_Torus;
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Light"))
        {
            if (ImGui::MenuItem("Point Light"))
            {
                NewLight({4, 5, 3}, WHITE, LIGHT_POINT);
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Directional Light"))
            {
                NewLight({4, 5, 3}, WHITE, LIGHT_DIRECTIONAL);
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Spot Light"))
            {
                NewLight({4, 5, 3}, WHITE, LIGHT_SPOT);
                showObjectTypePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("GUI"))
        {
            if (ImGui::MenuItem("Text"))
            {
                Text *MyTextElement = &AddText("Default Text", { 100, 100, 1 }, 20, BLUE);
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Button"))
            {
                AddButton("Default Text", { 100, 150, 1 }, {200, 50});
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


void ProcessDeletion()
{
    if (IsKeyPressed(KEY_DELETE))
    {
        if (selectedGameObjectType == "entity")
            selectedEntity->remove();
        else if (selectedGameObjectType == "light")
        {
            auto it = std::find(lights.begin(), lights.end(), *selectedLight);
            if (it != lights.end())
            {
                size_t index = std::distance(lights.begin(), it);
                if (index < lightsInfo.size())
                {
                    lights.erase(it);
                    lightsInfo.erase(lightsInfo.begin() + index);
                    UpdateLightsBuffer(true);
                }
            }
        }
    }        
}


bool canDuplicateEntity = true;

void EntityPaste(const std::shared_ptr<Entity>& entity)
{
    if (entity) {
        Entity newEntity = *entity;
        entitiesListPregame.push_back(newEntity);
        selectedGameObjectType = "entity";
        selectedEntity = &entitiesListPregame.back();
    }
}

void LightPaste(const std::shared_ptr<Light>& light)
{
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


void DuplicateEntity(Entity& entity)
{
    Entity newEntity = entity;
    newEntity.reloadRigidBody();
    entitiesListPregame.reserve(1);
    entitiesListPregame.emplace_back(newEntity);
    selectedEntity = &entitiesListPregame.back();
}


void ProcessCopy()
{
    if (IsKeyPressed(KEY_C) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) && !movingEditorCamera)) {
        if (selectedGameObjectType == "entity")
        {
            currentCopyType = CopyType_Entity;
            copiedEntity = std::make_shared<Entity>(*selectedEntity);
        }
        else if (selectedGameObjectType == "light")
        {
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


    if (IsKeyDown(KEY_D) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && !inGamePreview && selectedGameObjectType == "entity" && canDuplicateEntity)
    {
        DuplicateEntity(*selectedEntity);
        canDuplicateEntity = false;
    }
    if (!canDuplicateEntity)
    {
        canDuplicateEntity = IsKeyUp(KEY_D);
    }
}


int EditorCamera(void)
{

    std::chrono::high_resolution_clock::time_point sceneEditorStart = std::chrono::high_resolution_clock::now();

    if (ImGui::IsWindowHovered() && !dragging && !inGamePreview)
    {
        DropEntity();
    }

    if (ImGui::IsWindowHovered() && !dragging && !inGamePreview)
        ProcessObjectControls();

    if ((ImGui::IsWindowHovered() || ImGui::IsWindowFocused()) && !inGamePreview)
    {
        if (!showObjectTypePopup)
            EditorCameraMovement();
        ProcessCameraControls();
    }

    if (ImGui::IsWindowFocused())
    {
        ProcessCopy();
    }

    ProcessDeletion();

    if (inGamePreview)
    {
        RunGame();
        return 0;
    }

    RenderScene();

    ImVec2 currentWindowSize = ImGui::GetWindowSize();

    if (currentWindowSize.x != prevEditorWindowSize.x || currentWindowSize.y != prevEditorWindowSize.y)
    {
        UnloadRenderTexture(renderTexture);
        renderTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        texture = renderTexture.texture;

        // Unload existing textures before copying
        UnloadRenderTexture(downsamplerTexture);
        UnloadRenderTexture(upsamplerTexture);

        // Create new textures by loading from renderTexture
        downsamplerTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);
        upsamplerTexture = LoadRenderTexture(currentWindowSize.x, currentWindowSize.y);

        prevEditorWindowSize = currentWindowSize;
    }

    ObjectsPopup();

    std::chrono::high_resolution_clock::time_point sceneEditorEnd = std::chrono::high_resolution_clock::now();
    sceneEditorProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(sceneEditorEnd - sceneEditorStart);
    
    return 0;
}
