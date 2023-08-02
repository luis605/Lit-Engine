#include "../../../include_all.h"
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


Vector2 GetMouseMove()
{
    static Vector2 lastMousePosition = { 0 };

    Vector2 mousePosition = GetMousePosition();
    Vector2 mouseMove = { mousePosition.x - lastMousePosition.x, mousePosition.y - lastMousePosition.y };

    lastMousePosition = mousePosition;
    
    return mouseMove;
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

    if (IsKeyDown(KEY_W))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed);
        scene_camera.position = Vector3Add(scene_camera.position, movement);
        scene_camera.target = Vector3Add(scene_camera.target, movement);
    }

    if (IsKeyDown(KEY_S))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed);
        scene_camera.position = Vector3Subtract(scene_camera.position, movement);
        scene_camera.target = Vector3Subtract(scene_camera.target, movement);
    }

    if (IsKeyDown(KEY_A))
    {
        Vector3 movement = Vector3Scale(right, -movementSpeed);
        scene_camera.position = Vector3Add(scene_camera.position, movement);
        scene_camera.target = Vector3Add(scene_camera.target, movement);
    }

    if (IsKeyDown(KEY_D))
    {
        Vector3 movement = Vector3Scale(right, -movementSpeed);
        scene_camera.position = Vector3Subtract(scene_camera.position, movement);
        scene_camera.target = Vector3Subtract(scene_camera.target, movement);
    }

    scene_camera.target = Vector3Add(scene_camera.position, forward);

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

    }
}

void RenderScene()
{
    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    
    ClearBackground(GRAY);

    float cameraPos[3] = { scene_camera.position.x, scene_camera.position.y, scene_camera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);

    ProcessSelection();

    for (Light& light : lights)
    {
        Model light_model = LoadModelFromMesh(GenMeshPlane(10, 10, 1, 1));
        light_model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = light_texture;

        float rotation = DrawBillboardRotation(scene_camera, light_texture, { light.position.x, light.position.y, light.position.z }, 1.0f, WHITE);
        
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging_gizmo && !dragging_gizmo_rotation)
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


        entity.render();

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ImGui::IsWindowHovered() && !dragging_gizmo && !dragging_gizmo_rotation)
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

            AddEntity(true, false, path.c_str());
        }
        ImGui::EndDragDropTarget();
    }
}


int EditorCamera(void)
{
    if (ImGui::IsWindowHovered() && !dragging_gizmo && !dragging_gizmo_rotation && !in_game_preview)
        DropEntity();

    if ((ImGui::IsWindowHovered() || ImGui::IsWindowFocused()) && !dragging_gizmo && !dragging_gizmo_rotation && !in_game_preview)
    {
        EditorCameraMovement();
        ProcessCameraControls();
    }

    if (in_game_preview)
    {
        RunGame();
        return 0;
    }

    RenderScene();

    


    return 0;
}
