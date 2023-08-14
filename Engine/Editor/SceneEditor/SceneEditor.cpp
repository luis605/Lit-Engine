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
    renderTexture = LoadRenderTexture( GetScreenWidth(), GetScreenHeight() );
    texture = renderTexture.texture;

    scene_camera.position = { 50.0f, 0.0f, 0.0f };
    scene_camera.target = { 0.0f, 0.0f, 0.0f };
    scene_camera.up = { 0.0f, 1.0f, 0.0f };

    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front);

    scene_camera.fovy = 60.0f;
    scene_camera.projection = CAMERA_PERSPECTIVE;
}


float GetImGuiWindowTitleHeight()
{
    ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f;
}

void CalculateTextureRect(const Texture* texture, Rectangle& rectangle)
{
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    float targetWidth = windowSize.x;
    float targetHeight = windowSize.y + GetImGuiWindowTitleHeight();

    float offsetX = windowPos.x;
    float offsetY = windowPos.y;

    float textureAspectRatio = (float)texture->width / (float)texture->height;
    float imguiWindowAspectRatio = targetWidth / targetHeight;
    float scale = 1.0f;

    if (textureAspectRatio > imguiWindowAspectRatio)
        scale = targetWidth / (float)texture->width;
    else
        scale = targetHeight / (float)texture->height;

    float scaledWidth = scale * (float)texture->width;
    float scaledHeight = scale * (float)texture->height - GetImGuiWindowTitleHeight() * 2;

    rectangle.width = scaledWidth;
    rectangle.height = scaledHeight;

    rectangle.x = windowPos.x;
    rectangle.y = windowPos.y + GetImGuiWindowTitleHeight();

    offsetX += (targetWidth - scaledWidth) * 0.5f;
    offsetY += (targetHeight - scaledHeight) * 0.5f;
}

void DrawTextureOnRectangle(const Texture* texture)
{
    CalculateTextureRect(texture, rectangle);

    ImGui::Image((ImTextureID)texture, ImVec2(rectangle.width, rectangle.height), ImVec2(0,1), ImVec2(1,0));
    DrawTexturePro(
        *texture,
        Rectangle{ 0, 0, (float)texture->width, (float)texture->height },
        Rectangle{ rectangle.x, rectangle.y, rectangle.width, rectangle.height },
        Vector2{ 0, 0 },
        0.0f,
        WHITE
    );
}




bool is_mouse_pressed = false;
float mousePositionXLast = 0;

void EditorCameraMovement(void)
{
    // Update Camera Position
    front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front);

    Vector3 forward = Vector3Subtract(scene_camera.target, scene_camera.position);
    Vector3 right = Vector3CrossProduct(front, scene_camera.up);
    Vector3 normalized_right = Vector3Normalize(right);
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
        Vector3 movement = Vector3Scale(normalized_right, -movementSpeed);
        scene_camera.position = Vector3Add(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Add(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
    }

    if (IsKeyDown(KEY_D))
    {
        Vector3 movement = Vector3Scale(normalized_right, -movementSpeed);
        scene_camera.position = Vector3Subtract(scene_camera.position, Vector3Multiply(movement, DeltaTimeVec3));
        scene_camera.target = Vector3Subtract(scene_camera.target, Vector3Multiply(movement, DeltaTimeVec3));
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





float GetModelHeight(Model model) 
{
    BoundingBox modelBBox = GetMeshBoundingBox(model.meshes[0]);
    float modelHeight = modelBBox.max.y - modelBBox.min.y;
    return modelHeight;
}


bool IsMouseInRectangle(Vector2 mousePos, Rectangle rectangle)
{
    if (mousePos.x >= sceneEditorWindowX && mousePos.x <= sceneEditorWindowX + sceneEditorWindowWidth &&
        mousePos.y >= sceneEditorWindowY && mousePos.y <= sceneEditorWindowY + sceneEditorWindowHeight)
    {
        return true;
    }
    return false;
}






bool IsMouseHoveringModel(Model model, Camera camera, Vector3 position, Vector3 rotation, Entity* entity = nullptr, bool bypass_optimization = false)
{
    float x = position.x;
    float y = position.y;
    float z = position.z;
    float extreme_rotation = GetExtremeValue(rotation);

    Matrix matScale = MatrixScale(model.transform.m0, model.transform.m5, model.transform.m10);
    Matrix matRotation = MatrixRotate(rotation, extreme_rotation*DEG2RAD);
    Matrix matTranslation = MatrixTranslate(x, y, z);
    Matrix modelMatrix = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);

    Ray ray = { 0 };

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 windowPadding = style.WindowPadding;

    Vector2 pos = { GetMousePosition().x - rectangle.x + windowPadding.x, GetMousePosition().y - rectangle.y + windowPadding.y };
    Vector2 realPos = { pos.x * GetScreenWidth() / rectangle.width, pos.y * GetScreenHeight() / rectangle.height }; 

    
    ray = GetMouseRay(realPos, camera);

    RayCollision meshHitInfo = { 0 };

    for (int mesh_i = 0; mesh_i < model.meshCount; mesh_i++)
    {
        BoundingBox bounds = { 0 };

        if (entity == nullptr)
            bounds = GetMeshBoundingBox(model.meshes[mesh_i]);
        else
            bounds = entity->bounds;

        if (bypass_optimization || GetRayCollisionBox(GetMouseRay(GetMousePosition(), scene_camera),  bounds).hit)
        {
            meshHitInfo = GetRayCollisionMesh(ray, model.meshes[mesh_i], modelMatrix);
            if (meshHitInfo.hit)
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





void ProcessCameraControls()
{
    if (IsKeyPressed(KEY_F))
    {
        if (selected_game_object_type == "entity")
        {
            scene_camera.target = selected_entity->position;
            scene_camera.position = {
                selected_entity->position.x + 10,
                selected_entity->position.y + 2,
                selected_entity->position.z
            };
            
        }
    }
}

void ProcessSelection()
{
    if ((selected_game_object_type == "entity") ||
        (selected_game_object_type == "light"))
    {
        GizmoPosition();
        GizmoRotation();
        GizmoScale();

    }
}



void RenderScene()
{
    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    
    ClearBackground(GRAY);

    DrawSkybox();
    DrawGrid(40, 1.0f);

    float cameraPos[3] = { scene_camera.position.x, scene_camera.position.y, scene_camera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);


    ProcessSelection();

    for (Light& light : lights)
    {
        Model light_model = LoadModelFromMesh(GenMeshPlane(10, 10, 1, 1));
        light_model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = light_texture;

        float rotation = DrawBillboardRotation(scene_camera, light_texture, { light.position.x, light.position.y, light.position.z }, 1.0f, WHITE);
        
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging_gizmo_position && !dragging_gizmo_rotation)
        {
            bool isLightSelected = IsMouseHoveringModel(light_model, scene_camera, { light.position.x, light.position.y, light.position.z }, { 0, rotation, 0 } );
            if (isLightSelected)
            {
                object_in_inspector = &light;
                selected_game_object_type = "light";
            }
        }
    }


    for (Entity& entity : entities_list_pregame)
    {
        entity.calc_physics = false;
        entity.render();

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging_gizmo_position && !dragging_gizmo_rotation)
        {
            bool isEntitySelected = IsMouseHoveringModel(entity.model, scene_camera, entity.position, entity.rotation, &entity);
            if (isEntitySelected)
            {
                object_in_inspector = &entity;
                selected_game_object_type = "entity";
            }

            for (Entity* child : entity.children)
            {
                isEntitySelected = IsMouseHoveringModel(child->model, scene_camera, child->position, child->rotation);
                if (isEntitySelected)
                {
                    object_in_inspector = child;
                    selected_game_object_type = "entity";
                }
            }
        }
    }

    EndMode3D();
    DrawTextElements();
    DrawButtons();
    
    EndTextureMode();
    DrawTextureOnRectangle(&texture);
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

            string path = dir_path.c_str();
            path += "/" + files_texture_struct[payload_n].name;

            size_t lastDotIndex = path.find_last_of('.');

            // Extract the substring after the last dot
            std::string entity_name = path.substr(0, lastDotIndex);
            
            AddEntity(true, false, path.c_str(), Model(), entity_name);
        }
        ImGui::EndDragDropTarget();
    }
}



enum class ObjectType
{
    None,
    Cube,
    Sphere,
    // Add more object types here
};


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
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cone"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCone(1, 1, 30)));
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Cylinder"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshCylinder(1, 2, 30)));
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Plane"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Sphere"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshSphere(1, 30, 30)));
                showObjectTypePopup = false;
            }

            if (ImGui::MenuItem("Torus"))
            {
                AddEntity(true, false, "", LoadModelFromMesh(GenMeshTorus(1, 1, 30, 30)));
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
            entities_list_pregame.erase(std::remove(entities_list_pregame.begin(), entities_list_pregame.end(), *selected_entity), entities_list_pregame.end());
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

void ProcessCopy()
{
    if (IsKeyDown(KEY_D) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && !in_game_preview && selected_game_object_type == "entity" && can_duplicate_entity)
    {
        std::cout << "Creating Entity" << std::endl;
        Entity new_entity = *selected_entity;
        new_entity.reloadRigidBody();
        entities_list_pregame.reserve(1);
        entities_list_pregame.emplace_back(new_entity);
        selected_entity = &entities_list_pregame.back();
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


    ObjectsPopup();

    

    return 0;
}
