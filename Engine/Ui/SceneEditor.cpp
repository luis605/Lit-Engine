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



void DrawTextureOnRectangle(const Texture *texture, Rectangle rectangle)
{
    // Get Rectangle Proprieties
    Vector2 position = { rectangle.x*2, rectangle.y*1.8 };
    Vector2 size = { rectangle.width, rectangle.height };
    float rotation = 0.0f;

    // Update ImGui Window Properties
    ImVec2 childSize = ImGui::GetContentRegionAvail();
    sceneEditorWindowWidth = childSize.x;
    sceneEditorWindowHeight = childSize.y;

    ImVec2 windowPos = ImGui::GetWindowPos();
    sceneEditorWindowX = windowPos.x;
    sceneEditorWindowY = windowPos.y;

    rectangle.x = sceneEditorWindowX;
    rectangle.y = sceneEditorWindowY;
    
    // Rotate Texture in Y-Axis
    glm::mat4 matrix;
    matrix = glm::scale(matrix, glm::vec3(-1.0f, 1.0f, 1.0f));

    DrawTextureEx(*texture, (Vector2){0,0}, 0, 1.0f, WHITE);
    DrawTexturePro(*texture, (Rectangle){0, 0, texture->width, texture->height}, (Rectangle){0, 0, 0, 0}, (Vector2){0, 0}, 0.0f, WHITE);
    
    // Draw Texture
    ImGui::Image((ImTextureID)texture, ImVec2(sceneEditorWindowWidth, sceneEditorWindowHeight), ImVec2(0,1), ImVec2(1,0));
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

    if (IsMouseInRectangle(GetMousePosition(), rectangle))
    {
        Ray ray = { 0 };

        Vector2 pos = { GetMousePosition().x - sceneEditorWindowX, GetMousePosition().y - sceneEditorWindowY };
        Vector2 realPos = { pos.x * GetScreenWidth()/rectangle.width, pos.y * GetScreenHeight()/rectangle.height };        
        ray = GetMouseRay(realPos, camera);
        RayCollision meshHitInfo = { 0 };

        for (int mesh_i = 0; mesh_i < model.meshCount; mesh_i++)
        {
            meshHitInfo = GetRayCollisionMesh(ray, model.meshes[mesh_i], modelMatrix);
            if (meshHitInfo.hit)
                return true;
        }
    }
    
    return false;
}



void Gizmo()
{

    Vector3 *object_in_inspector_position = visit([](auto& obj) {
        return &obj->position;
    }, object_in_inspector);


    Vector3 *object_in_inspector_relative_position = visit([](auto& obj) {
        return &obj->relative_position;
    }, object_in_inspector);


    bool *object_in_inspector_isChildren = visit([](auto& obj) {
        return &obj->isChildren;
    }, object_in_inspector);


    // Gizmo Arrow Up
    gizmo_arrow[0].position = {object_in_inspector_position->x, object_in_inspector_position->y + 6, object_in_inspector_position->z};
    gizmo_arrow[0].rotation = {0, 0, 0};

    // Gizmo Arrow Down
    gizmo_arrow[1].position = {object_in_inspector_position->x, object_in_inspector_position->y - 6, object_in_inspector_position->z};
    gizmo_arrow[1].rotation = {180, 0, 0};

    // Gizmo Arrow Right
    gizmo_arrow[2].position = {object_in_inspector_position->x, object_in_inspector_position->y, object_in_inspector_position->z + 6};
    gizmo_arrow[2].rotation = {90, 0, 0};

    // Gizmo Arrow Left
    gizmo_arrow[3].position = {object_in_inspector_position->x, object_in_inspector_position->y, object_in_inspector_position->z - 6};
    gizmo_arrow[3].rotation = {-90, 0, 0};

    // Gizmo Arrow Forward
    gizmo_arrow[4].position = {object_in_inspector_position->x + 6, object_in_inspector_position->y, object_in_inspector_position->z};
    gizmo_arrow[4].rotation = {0, 0, -90};

    // Gizmo Arrow Backward
    gizmo_arrow[5].position = {object_in_inspector_position->x - 6, object_in_inspector_position->y, object_in_inspector_position->z};
    gizmo_arrow[5].rotation = {0, 0, 90};


    // Position Update
    for (int arrow_i = 0; arrow_i < (sizeof(gizmo_arrow) / sizeof(gizmo_arrow[0])); arrow_i++)
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


    object_in_inspector_position->x = gizmo_arrow[0].position.x;
    object_in_inspector_position->y = y_axis_arrows_center_pos;
    object_in_inspector_position->z = gizmo_arrow[0].position.z;
    

    if (object_in_inspector_isChildren)
    {   
        object_in_inspector_relative_position->x = gizmo_arrow[0].position.x;
        object_in_inspector_relative_position->y = y_axis_arrows_center_pos;
        object_in_inspector_relative_position->z = gizmo_arrow[0].position.z;
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
    rectangle.width = sceneEditorWindowWidth;
    rectangle.height = sceneEditorWindowHeight;


    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    ClearBackground(GRAY);

    // float cameraPos[3] = { scene_camera.position.x, scene_camera.position.y, scene_camera.position.z };
    // SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
    
    int entity_index = 0;
    for (Entity& entity : entities_list_pregame)
    {
        entity.render();
        // entity.setShader(shader);

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            bool isEntitySelected = IsMouseHoveringModel(entity.model, scene_camera, entity.position, entity.rotation);
            if (isEntitySelected)
            {
                listViewExActive = entity_index;
                object_in_inspector = &entity;
            }

            for (Entity* child : entity.children)
            {
                isEntitySelected = IsMouseHoveringModel(child->model, scene_camera, child->position, child->rotation);
                if (isEntitySelected)
                {
                    object_in_inspector = child;
                }
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


    DrawTextureOnRectangle(&texture, rectangle);

    return 0;
}
