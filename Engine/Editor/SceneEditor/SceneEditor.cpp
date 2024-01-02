#include "../../../include_all.h"
#include "SceneEditor.h"
#include "Gizmo/Gizmo.cpp"

#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION            330
#else   // PLATFORM_RPI, PLATFORM_ANDROID, PLATFORM_WEB
    #define GLSL_VERSION            100
#endif



void InitEditorCamera()
{
    renderTexture = LoadRenderTexture( 1, 1 );
    texture = renderTexture.texture;

    scene_camera.position = { 50.0f, 0.0f, 0.0f };
    scene_camera.target = { 0.0f, 0.0f, 0.0f };
    scene_camera.up = { 0.0f, 1.0f, 0.0f };

    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front);

    scene_camera.fovy = 60.0f;
    scene_camera.projection = CAMERA_PERSPECTIVE;
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
    front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front);

    Vector3 forward = Vector3Subtract(scene_camera.target, scene_camera.position);
    Vector3 right = Vector3CrossProduct(front, scene_camera.up);
    Vector3 normalizedRight = Vector3Normalize(right);
    Vector3 DeltaTimeVec3 = { GetFrameTime(), GetFrameTime(), GetFrameTime() };

    if (IsKeyDown(KEY_W))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed);
        scene_camera.position = Vector3Add(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Add(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_S))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed);
        scene_camera.position = Vector3Subtract(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Subtract(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_A))
    {
        Vector3 movement = Vector3Scale(normalizedRight, -movementSpeed);
        scene_camera.position = Vector3Add(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Add(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_D))
    {
        Vector3 movement = Vector3Scale(normalizedRight, -movementSpeed);
        scene_camera.position = Vector3Subtract(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Subtract(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_Q))
    {
        Vector3 movement = Vector3Scale(scene_camera.up, movementSpeed);
        scene_camera.position = Vector3Subtract(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Subtract(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_E))
    {
        Vector3 movement = Vector3Scale(scene_camera.up, movementSpeed);
        scene_camera.position = Vector3Add(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Add(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (GetMouseWheelMove() != 0 && ImGui::IsWindowHovered())
    {
        CameraMoveToTarget(&scene_camera, -GetMouseWheelMove());
    }

    scene_camera.target = Vector3Add(scene_camera.position, forward);


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
        float angle_in_x_axis = (lastMousePosition.x - mousePosition.x) * 0.005f;
        float angle_in_y_axis = (lastMousePosition.y - mousePosition.y) * 0.005f;

        Camera *camera_ptr = (Camera*)(&scene_camera);
        CameraYaw(camera_ptr, angle_in_x_axis, false);
        CameraPitch(camera_ptr, angle_in_y_axis, true, false, false);

        lastMousePosition = mousePosition;
    }
    else
    {
        lastMousePosition = GetMousePosition();
    }
}


Ray GetMouseRayEx(Vector2 mouse, Camera camera, Rectangle rect) {
    Ray ray = { 0 };

    // NOTE: y value is negative
    float x = (2.0f*mouse.x)/(float)rect.width - 1.0f;
    float y = 1.0f - (2.0f*mouse.y)/rect.height;
    float z = 1.0f;

    // Store values in a vector
    Vector3 deviceCoords = { x, y, z };

    // Calculate view matrix from camera look at
    Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);

    Matrix matProj = MatrixIdentity();

    if (camera.projection == CAMERA_PERSPECTIVE)
    {
        // Calculate projection matrix from perspective
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)rect.width/(double)rect.height), RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    }
    else if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        float aspect = (float)rect.width/(float)rect.height;
        double top = camera.fovy/2.0;
        double right = top*aspect;

        // Calculate projection matrix from orthographic
        matProj = MatrixOrtho(-right, right, -top, top, 0.01, 1000.0);
    }

    // Unproject far/near points
    Vector3 nearPoint = Vector3Unproject((Vector3){ deviceCoords.x, deviceCoords.y, 0.0f }, matProj, matView);
    Vector3 farPoint = Vector3Unproject((Vector3){ deviceCoords.x, deviceCoords.y, 1.0f }, matProj, matView);

    // Unproject the mouse cursor in the near plane.
    // We need this as the source position because orthographic projects, compared to perspective doesn't have a
    // convergence point, meaning that the "eye" of the camera is more like a plane than a point.
    Vector3 cameraPlanePointerPos = Vector3Unproject((Vector3){ deviceCoords.x, deviceCoords.y, -1.0f }, matProj, matView);

    // Calculate normalized direction vector
    Vector3 direction = Vector3Normalize(Vector3Subtract(farPoint, nearPoint));

    if (camera.projection == CAMERA_PERSPECTIVE) ray.position = camera.position;
    else if (camera.projection == CAMERA_ORTHOGRAPHIC) ray.position = cameraPlanePointerPos;

    // Apply calculated vectors to ray
    ray.direction = direction;

    return ray;
}


bool IsMouseHoveringModel(Model model, Camera camera, Vector3 position, Vector3 rotation, Vector3 scale, Entity* entity, bool bypassOptimization)
{
    Matrix modelTransform = MatrixIdentity();
    modelTransform = MatrixMultiply(modelTransform, MatrixScale(scale.x, scale.y, scale.z));
    modelTransform = MatrixMultiply(modelTransform, MatrixRotateXYZ(rotation));
    modelTransform = MatrixMultiply(modelTransform, MatrixTranslate(position.x, position.y, position.z));

    Vector2 relativeMousePos = {
        ImGui::GetMousePos().x - rectangle.x,
        ImGui::GetMousePos().y - rectangle.y - GetImGuiWindowTitleHeight()
    };

    Vector2 adjustedMousePos = relativeMousePos;

    Ray mouseRay = GetMouseRayEx(adjustedMousePos, camera, rectangle);
    RayCollision meshCollisionInfo = { 0 };

    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
    {
        BoundingBox meshBounds = (entity == nullptr) ? GetMeshBoundingBox(model.meshes[meshIndex]) : entity->bounds;

        if (bypassOptimization || GetRayCollisionBox(mouseRay, meshBounds).hit)
        {
            meshCollisionInfo = GetRayCollisionMesh(mouseRay, model.meshes[meshIndex], modelTransform);
            if (meshCollisionInfo.hit)
                return true;
        }
    }

    return false;
}


bool isVectorPositive(const Vector3& vector) {
    return (vector.x > 0 && vector.y > 0 && vector.z > 0);
}

bool isVectorNegative(const Vector3& vector) {
    return (vector.x < 0 && vector.y < 0 && vector.z < 0);
}


bool isVectorNeutral(const Vector3& vector) {
    return (vector.x == 0 && vector.y == 0 && vector.z == 0);
}



void LocateEntity(Entity& entity)
{
    if (selected_game_object_type == "entity")
    {
        scene_camera.target = entity.position;
        scene_camera.position = {
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
        LocateEntity(*selected_entity);
    }
}

void ProcessGizmo()
{
    if (selected_game_object_type == "entity" && selected_entity)
    {
        if (selected_entity->initialized)
        {
            GizmoPosition();
            GizmoRotation();
            GizmoScale();
        }
    }
    else if (selected_game_object_type == "light")
    {
        GizmoPosition();
    }
}

struct EmptyType {};

void RenderScene()
{
    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    
    ClearBackground(GRAY);

    DrawSkybox();
    DrawGrid(40, 1.0f);

    float cameraPos[3] = { scene_camera.position.x, scene_camera.position.y, scene_camera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);

    SetShaderValueMatrix(shader, GetShaderLocation(shader, "cameraMatrix"), GetCameraMatrix(scene_camera));


    bool isLightSelected   = false;
    bool isEntitySelected  = false;

    ProcessGizmo();


    for (Light& light : lights)
    {
        Model light_model = LoadModelFromMesh(GenMeshPlane(10, 10, 1, 1));
        light_model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = light_texture;

        float rotation = DrawBillboardRotation(scene_camera, light_texture, { light.position.x, light.position.y, light.position.z }, 1.0f, WHITE);
        
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging_gizmo_position && !dragging_gizmo_rotation)
        {
            isLightSelected = IsMouseHoveringModel(light_model, scene_camera, { light.position.x, light.position.y, light.position.z }, { 0, rotation, 0 }, {1,1,1});
            if (isLightSelected)
            {
                object_in_inspector = &light;
                selected_game_object_type = "light";
            }
        }
    }


    int index = 0;
    for (Entity& entity : entities_list_pregame)
    {
        entity.calc_physics = false;
        entity.render();

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging_gizmo_position && !dragging_gizmo_rotation)
        {
            isEntitySelected = IsMouseHoveringModel(entity.model, scene_camera, entity.position, entity.rotation, entity.scale, &entity);
            if (isEntitySelected)
            {
                if (IsModelReady(entity.model) && entity.initialized)
                {
                    object_in_inspector = &entities_list_pregame.at(index);
                    selected_game_object_type = "entity";
                }
            }

            for (std::variant<Entity*, Light*, Text*, LitButton*>& childVariant : entity.children)
            {
                if (auto* childEntity = std::get_if<Entity*>(&childVariant))
                {
                    bool isEntitySelected = IsMouseHoveringModel((*childEntity)->model, scene_camera, (*childEntity)->position, (*childEntity)->rotation, (*childEntity)->scale);
                    if (isEntitySelected)
                    {
                        object_in_inspector = *childEntity;
                        selected_game_object_type = "entity";
                    }
                }
            }

        }

        index++;
    }



    if (
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        ImGui::IsWindowHovered() &&
        !isEntitySelected &&
        !isLightSelected &&
        !isHoveringGizmo &&
        !dragging_gizmo_scale &&
        !dragging_gizmo_rotation &&
        !dragging_gizmo_position
        )
    {
        selected_game_object_type = "none";
    }


    UpdateInGameGlobals();
    UpdateLightsBuffer();

    EndMode3D();
    
    DrawTextElements();
    DrawButtons();
    
    EndTextureMode();

    if (bloomEnabled)
    {
        // BeginTextureMode(brightPass);
        // BeginShaderMode(brightPassShader);
        //     SetShaderValueTexture(brightPassShader, GetShaderLocation(brightPassShader, "colorTexture"), texture);

        //     Rectangle sourceRec = { 0, 0, static_cast<float>(texture.width), static_cast<float>(-texture.height) };
        //     Rectangle destRec = { 0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height) };
        //     Vector2 origin = { 0, 0 };

        //     // Draw the texture with the modified sourceRec
        //     DrawTexturePro(texture, sourceRec, destRec, origin, 0, WHITE);


        // EndShaderMode();
        // EndTextureMode();


        // BeginTextureMode(blurPass);
        // BeginShaderMode(blurShader);

        //     SetShaderValueTexture(blurShader, GetShaderLocation(blurShader, "screenTexture"), brightPass.texture);

        //     // Draw a full-screen quad to apply the blur effect
        //     DrawTexture(brightPass.texture, 0, 0, WHITE);

        // EndShaderMode();
        // EndTextureMode();


        // BeginTextureMode(blurPass);
        // BeginShaderMode(blurShader);
        //     bool true_var = true;
        //     SetShaderValueTexture(blurShader, GetShaderLocation(blurShader, "screenTexture"), brightPass.texture);
        //     SetShaderValue(blurShader, GetShaderLocation(blurShader, "horizontal"), &true_var, SHADER_UNIFORM_INT);
        //     // Draw a full-screen quad to apply the blur effect
        //     DrawTexture(mixPass.texture, 0, 0, WHITE);

        // EndShaderMode();
        // EndTextureMode();


        // BeginTextureMode(flipped_texture);
        //     sourceRec = { 0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height) };
        //     DrawTexturePro(texture, sourceRec, destRec, origin, 0, WHITE);

        // EndTextureMode();


        // BeginTextureMode(mixPass);
        // BeginShaderMode(mixShader);
        //     SetShaderValueTexture(mixShader, GetShaderLocation(mixShader, "scene"), flipped_texture.texture);
        //     SetShaderValueTexture(mixShader, GetShaderLocation(mixShader, "bloomBlur"), blurPass.texture);
        //     DrawTexture(texture,0,0,WHITE);
        // EndShaderMode();
        // EndTextureMode();


        BeginTextureMode(downsamplerTexture);
        BeginShaderMode(downsamplerShader);
            SetShaderValueTexture(downsamplerShader, GetShaderLocation(downsamplerShader, "srcTexture"), texture);
            Vector2 screenResolution = { static_cast<float>(texture.width), static_cast<float>(texture.height) };
            SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "srcResolution"), &screenResolution, SHADER_UNIFORM_VEC2);

            DrawTexture(texture,0,0,WHITE);
        EndShaderMode();
        EndTextureMode();

        BeginTextureMode(upsamplerTexture);
        BeginShaderMode(upsamplerShader);
            SetShaderValueTexture(downsamplerShader, GetShaderLocation(downsamplerShader, "srcTexture"), downsamplerTexture.texture);
            float filter = 100.0f;
            SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "filterRadius"), &filter, SHADER_UNIFORM_FLOAT);

            DrawTexture(downsamplerTexture.texture,0,0,WHITE);
        EndShaderMode();
        EndTextureMode();

        DrawTextureOnRectangle(&upsamplerTexture.texture);
    }
    else
    {
        DrawTextureOnRectangle(&texture);
    }
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

            string path = dir_path.string();
            path += "/" + files_texture_struct[payload_n].name;

            size_t lastDotIndex = path.find_last_of('.');

            // Extract the substring after the last dot
            std::string entity_name = path.substr(0, lastDotIndex);
            
            AddEntity(true, false, path.c_str(), Model(), entity_name);
        }
        ImGui::EndDragDropTarget();
    }
}

bool showObjectTypePopup = false;

void ProcessObjectControls()
{
    if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) && IsKeyDown(KEY_A))
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
                entities_list_pregame.back().ObjectType = Entity::ObjectType_Cube;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cone"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCone(1, 1, 30)));
                entities_list_pregame.back().ObjectType = Entity::ObjectType_Cone;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cylinder"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCylinder(1, 2, 30)));
                entities_list_pregame.back().ObjectType = Entity::ObjectType_Cylinder;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Plane"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));
                entities_list_pregame.back().ObjectType = Entity::ObjectType_Plane;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Sphere"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshSphere(1, 50, 50)));
                entities_list_pregame.back().ObjectType = Entity::ObjectType_Sphere;
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Torus"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshTorus(1, 1, 30, 30)));
                entities_list_pregame.back().ObjectType = Entity::ObjectType_Torus;
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
        if (selected_game_object_type == "entity")
            selected_entity->remove();
        else if (selected_game_object_type == "light")
        {
            lights.erase(std::remove(lights.begin(), lights.end(), *selected_light), lights.end());
            lights_info.erase(std::remove(lights_info.begin(), lights_info.end(), *selected_light), lights_info.end());
            UpdateLightsBuffer(true);
            return;
        }
    }        
}

bool can_duplicate_entity = true;

void EntityPaste(const std::shared_ptr<Entity>& entity)
{
    if (entity) {
        Entity new_entity = *entity;
        entities_list_pregame.push_back(new_entity);
        selected_game_object_type = "entity";
        selected_entity = &entities_list_pregame.back();
    }
}

void LightPaste(const std::shared_ptr<Light>& light)
{
    if (light) {
        Light new_light = *light;
        new_light.id = lights.back().id+1;
        lights.push_back(new_light);
        AdditionalLightInfo new_light_info;
        new_light_info.id = new_light.id;
        lights_info.push_back(new_light_info);
        
        selected_game_object_type = "light";
        selected_light = &lights.back();
    }
}


void DuplicateEntity(Entity& entity)
{
    Entity new_entity = entity;
    new_entity.reloadRigidBody();
    entities_list_pregame.reserve(1);
    entities_list_pregame.emplace_back(new_entity);
    selected_entity = &entities_list_pregame.back();
}


void ProcessCopy()
{
    if (IsKeyPressed(KEY_C) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        if (selected_game_object_type == "entity")
        {
            current_copy_type = CopyType_Entity;
            copiedEntity = std::make_shared<Entity>(*selected_entity);
        }
        else if (selected_game_object_type == "light")
        {
            current_copy_type = CopyType_Light;
            copiedLight = std::make_shared<Light>(*selected_light);
        }
    }

    if (IsKeyPressed(KEY_V) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        if (current_copy_type == CopyType_Entity)
            EntityPaste(copiedEntity);
        if (current_copy_type == CopyType_Light)
            LightPaste(copiedLight);
    }


    if (IsKeyDown(KEY_D) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && !in_game_preview && selected_game_object_type == "entity" && can_duplicate_entity)
    {
        DuplicateEntity(*selected_entity);
        can_duplicate_entity = false;
    }
    if (!can_duplicate_entity)
    {
        can_duplicate_entity = IsKeyUp(KEY_D);
    }
}


int EditorCamera(void)
{

    if (ImGui::IsWindowHovered() && !dragging_gizmo_position && !dragging_gizmo_rotation && !in_game_preview)
    {
        DropEntity();
    }
    if ((ImGui::IsWindowHovered() || ImGui::IsWindowFocused()) && !dragging_gizmo_position && !dragging_gizmo_rotation && !in_game_preview)
    {
        if (!showObjectTypePopup)
            EditorCameraMovement();
        ProcessCameraControls();
    }

    if (ImGui::IsWindowFocused())
    {
        ProcessDeletion();
        ProcessCopy();
    }


    if (in_game_preview)
    {
        RunGame();
        return 0;
    }

    RenderScene();

    if (ImGui::IsWindowHovered() && !dragging_gizmo_position && !dragging_gizmo_rotation && !in_game_preview)
        ProcessObjectControls();



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


    return 0;
}
