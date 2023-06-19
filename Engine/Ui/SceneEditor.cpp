#include "../../include_all.h"


#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION            330
#else   // PLATFORM_RPI, PLATFORM_ANDROID, PLATFORM_WEB
    #define GLSL_VERSION            100
#endif



struct GizmoArrow {
    Model model;
    Vector3 position;
    Vector3 rotation;
    string drag_directions;

};

GizmoArrow gizmo_arrow[5];



// Gizmo - Rotation
Model gizmo_taurus_up;
Model gizmo_taurus_down;
Model gizmo_taurus_right;
Model gizmo_taurus_left;
Model gizmo_taurus_forward;
Model gizmo_taurus_backward;

Vector3 gizmo_taurus_up_position;
Vector3 gizmo_taurus_down_position;
Vector3 gizmo_taurus_right_position;
Vector3 gizmo_taurus_left_position;
Vector3 gizmo_taurus_forward_position;
Vector3 gizmo_taurus_backward_position;

Vector3 gizmo_taurus_up_rotation;
Vector3 gizmo_taurus_down_rotation;
Vector3 gizmo_taurus_right_rotation;
Vector3 gizmo_taurus_left_rotation;
Vector3 gizmo_taurus_forward_rotation;
Vector3 gizmo_taurus_backward_rotation;

string rotation_x_axis = "x-axis";
string rotation_y_axis = "y-axis";
string rotation_z_axis = "z-axis";

string gizmo_taurus_up_drag_direction = rotation_x_axis;
string gizmo_taurus_left_drag_direction = rotation_y_axis;
string gizmo_taurus_backward_drag_direction = rotation_z_axis;


Model gizmo_taurus[] = {
    gizmo_taurus_up,
    gizmo_taurus_down,
    gizmo_taurus_right,
};

Vector3 gizmo_taurus_position[] = {
    gizmo_taurus_up_position,
    gizmo_taurus_down_position,
    gizmo_taurus_right_position,
};

Vector3 gizmo_taurus_rotation[] = {
    gizmo_taurus_up_rotation,
    gizmo_taurus_down_rotation,
    gizmo_taurus_right_rotation,
};

string gizmo_taurus_drag_directions[] = {
    gizmo_taurus_up_drag_direction,
    gizmo_taurus_left_drag_direction,
    gizmo_taurus_backward_drag_direction,
};



float gizmo_drag_sensitivity_factor = 0.1f;

void InitEditorCamera()
{
    // Set Textures
    renderTexture = LoadRenderTexture( GetScreenWidth(), GetScreenHeight() );
    texture = renderTexture.texture;

    // Camera Proprieties
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
    float targetHeight = windowSize.y;

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
    // Camera Position Change
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

    // Update Camera Target along with Camera
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






bool IsMouseHoveringModel(Model model, Camera camera, Vector3 position, Vector3 rotation)
{
    float x = position.x;
    float y = position.y;
    float z = position.z;
    float extreme_rotation = GetExtremeValue(rotation);

    Matrix matScale = MatrixScale(1, 1, 1);
    Matrix matRotation = MatrixRotate(rotation, extreme_rotation*DEG2RAD);
    Matrix matTranslation = MatrixTranslate(x, y, z);
    Matrix modelMatrix = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);


    // if (IsMouseInRectangle(GetMousePosition(), rectangle))
    // {


    Ray ray = { 0 };

    Vector2 pos = { GetMousePosition().x - rectangle.x, GetMousePosition().y - rectangle.y };
    Vector2 realPos = { pos.x * GetScreenWidth()/rectangle.width, pos.y * GetScreenHeight()/rectangle.height };        
    ray = GetMouseRay(realPos, camera);
    RayCollision meshHitInfo = { 0 };

    for (int mesh_i = 0; mesh_i < model.meshCount; mesh_i++)
    {
        meshHitInfo = GetRayCollisionMesh(ray, model.meshes[mesh_i], modelMatrix);
        if (meshHitInfo.hit)
            return true;
    }
    //}
    // else
    // {
    //     std::cout << "Rectangle x/y: " << rectangle.x << "/" << rectangle.y << std::endl;
    //     std::cout << "Rectangle width/height: " << rectangle.width << "/" << rectangle.height << std::endl;
    // }
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



void Gizmo()
{

    if (std::holds_alternative<Entity*>(object_in_inspector)) {
        Entity* entity = std::get<Entity*>(object_in_inspector);
        selected_entity_position = entity->position;
        selected_entity_relative_position = entity->relative_position;
        selected_entity_isChild = entity->isChild;
    }


    if (std::holds_alternative<Light*>(object_in_inspector)) {
        Light* light = std::get<Light*>(object_in_inspector);
        glm::vec3* selected_entity_position = &light->position;
        glm::vec3* object_in_inspector_relative_position = &light->relative_position;
        bool* object_in_inspector_isChild = &light->isChild;
    }


    // Gizmo Arrow Up
    gizmo_arrow[0].position = {selected_entity_position.x, selected_entity_position.y + 6, selected_entity_position.z};
    gizmo_arrow[0].rotation = {0, 0, 0};

    // Gizmo Arrow Down
    gizmo_arrow[1].position = {selected_entity_position.x, selected_entity_position.y - 6, selected_entity_position.z};
    gizmo_arrow[1].rotation = {180, 0, 0};

    // Gizmo Arrow Right
    gizmo_arrow[2].position = {selected_entity_position.x, selected_entity_position.y, selected_entity_position.z + 6};
    gizmo_arrow[2].rotation = {90, 0, 0};

    // Gizmo Arrow Left
    gizmo_arrow[3].position = {selected_entity_position.x, selected_entity_position.y, selected_entity_position.z - 6};
    gizmo_arrow[3].rotation = {-90, 0, 0};

    // Gizmo Arrow Forward
    gizmo_arrow[4].position = {selected_entity_position.x + 6, selected_entity_position.y, selected_entity_position.z};
    gizmo_arrow[4].rotation = {0, 0, -90};

    // Gizmo Arrow Backward
    gizmo_arrow[5].position = {selected_entity_position.x - 6, selected_entity_position.y, selected_entity_position.z};
    gizmo_arrow[5].rotation = {0, 0, 90};


    for (int arrow_i = 0; arrow_i < (sizeof(gizmo_arrow) / sizeof(gizmo_arrow[0])) + 1; arrow_i++)
    {
        Color color1;

        if (!dragging_gizmo)
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmo_arrow[arrow_i].model, scene_camera, gizmo_arrow[arrow_i].position, gizmo_arrow[arrow_i].rotation);
            
            if (isHoveringGizmo)
            {
                color1 = GREEN;
                gizmo_arrow_selected = arrow_i;
            }
            else
            {
                color1 = { 255, 0, 0, 100 };
                gizmo_arrow_selected == -1;
            }
        }
        else
        {
            color1 = RED;
            gizmo_arrow_selected == -1;
        }

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (isHoveringGizmo)
            {
                if (!dragging_gizmo)
                {
                    mouse_drag_start = GetMousePosition();
                    dragging_gizmo = true;
                }
            }
            if (dragging_gizmo)
            {
                Vector2 mouse_drag_end = GetMousePosition();
                if ( gizmo_arrow_selected == 0 || gizmo_arrow_selected == 1 )
                {
                    float delta_y = (mouse_drag_end.y - mouse_drag_start.y) * gizmo_drag_sensitivity_factor;
                    
                    gizmo_arrow[0].position.y -= delta_y;
                    gizmo_arrow[1].position.y -= delta_y;
                    
                }

                else if ( gizmo_arrow_selected == 2 || gizmo_arrow_selected == 3 )
                {
                    float delta_z = ((mouse_drag_end.x - mouse_drag_start.x) + (mouse_drag_end.y - mouse_drag_start.y)) * gizmo_drag_sensitivity_factor;
                    gizmo_arrow[arrow_i].position.z -= delta_z;
                }
                
                else if ( gizmo_arrow_selected == 4 || gizmo_arrow_selected == 5 )
                {
                    float delta_x = (mouse_drag_end.x - mouse_drag_start.x) * gizmo_drag_sensitivity_factor;
                    gizmo_arrow[arrow_i].position.x += delta_x;
                }

                mouse_drag_start = mouse_drag_end;
            }
        }
        else dragging_gizmo = false;

        float extreme_rotation = GetExtremeValue(gizmo_arrow[arrow_i].rotation);
        DrawModelEx(gizmo_arrow[arrow_i].model, gizmo_arrow[arrow_i].position, gizmo_arrow[arrow_i].rotation, extreme_rotation, {1,1,1}, color1);

    }

    float y_axis_arrows_center_pos = (gizmo_arrow[0].position.y + gizmo_arrow[1].position.y) / 2.0f;



    if ((bool)selected_entity_isChild)
    {   

        selected_entity_position.x = gizmo_arrow[0].position.x;
        selected_entity_position.y = y_axis_arrows_center_pos;
        selected_entity_position.z = gizmo_arrow[0].position.z;

        selected_entity->relative_position = Vector3Subtract(selected_entity->position, selected_entity->parent->position);
    }
    else
    {
        selected_entity_position.x = gizmo_arrow[0].position.x;
        selected_entity_position.y = y_axis_arrows_center_pos;
        selected_entity_position.z = gizmo_arrow[0].position.z;
    }

}

int EditorCamera(void)
{
    if (in_game_preview)
    {
        RunGame();
        return 0;
    }

    EditorCameraMovement();


    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    ClearBackground(GRAY);

    float cameraPos[3] = { scene_camera.position.x, scene_camera.position.y, scene_camera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
    
    int entity_index = 0;
    for (Entity& entity : entities_list_pregame)
    {
        entity.render();

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            bool isEntitySelected = IsMouseHoveringModel(entity.model, scene_camera, entity.position, entity.rotation);
            if (isEntitySelected)
            {
                listViewExActive = entity_index;
                object_in_inspector = &entity;
                selected_gameObject_type = "entity";

            }
            // else
            // {
            //     selected_gameObject_type = "NotAnObject";
            //     object_in_inspector = std::variant<Entity*, Light*>();
            // }
                
            for (Entity* child : entity.children)
            {
                isEntitySelected = IsMouseHoveringModel(child->model, scene_camera, child->position, child->rotation);
                if (isEntitySelected)
                {
                    object_in_inspector = child;
                    selected_gameObject_type = "entity";
                }
                // else
                // {
                //     selected_gameObject_type = "NotAnObject";
                //     object_in_inspector = std::variant<Entity*, Light*>();
                // }
            }
            

        }

        entity_index++;
    }


    if ((selected_gameObject_type == "entity") ||
        (selected_gameObject_type == "light"))
    {
        Gizmo();
    }



    // End 3D rendering
    EndMode3D();
    EndTextureMode();


    DrawTextureOnRectangle(&texture);

    return 0;
}
