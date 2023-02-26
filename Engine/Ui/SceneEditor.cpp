#include "../../include_all.h"


#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION            330
#else   // PLATFORM_RPI, PLATFORM_ANDROID, PLATFORM_WEB
    #define GLSL_VERSION            100
#endif



/* Scene Camera */

// Physics
bool do_physics = false;

// Textures
RenderTexture2D renderTexture;
Texture2D texture;
Rectangle rectangle = { screenWidth*.2, screenHeight*.2, texture.width, texture.height };

// Camera
Camera3D scene_camera;
float lerp_factor = 0.5f;
float movementSpeed = 0.5f;

bool dragging = false;

Vector2 mousePosition;
Vector2 mousePositionPrev = GetMousePosition();
Vector3 front;
// ImGui Window Info
float windowWidth;
float windowHeight;
float windowX;
float windowY;

// Gizmo - Position
Model gizmo_arrow_up;
Model gizmo_arrow_down;
Model gizmo_arrow_right;
Model gizmo_arrow_left;
Model gizmo_arrow_forward;
Model gizmo_arrow_backward;

Vector3 gizmo_arrow_up_position;
Vector3 gizmo_arrow_down_position;
Vector3 gizmo_arrow_right_position;
Vector3 gizmo_arrow_left_position;
Vector3 gizmo_arrow_forward_position;
Vector3 gizmo_arrow_backward_position;

Vector3 gizmo_arrow_up_rotation;
Vector3 gizmo_arrow_down_rotation;
Vector3 gizmo_arrow_right_rotation;
Vector3 gizmo_arrow_left_rotation;
Vector3 gizmo_arrow_forward_rotation;
Vector3 gizmo_arrow_backward_rotation;

string up_down = "up-down";
string right_left = "up-down";
string forward_backward = "up-down";

string gizmo_arrow_up_drag_direction = up_down;
string gizmo_arrow_down_drag_direction = up_down;
string gizmo_arrow_right_drag_direction = right_left;
string gizmo_arrow_left_drag_direction = right_left;
string gizmo_arrow_forward_drag_direction = forward_backward;
string gizmo_arrow_backward_drag_direction = forward_backward;


Model gizmo_arrows[] = {
    gizmo_arrow_up,
    gizmo_arrow_down,
    gizmo_arrow_right,
    gizmo_arrow_left,
    gizmo_arrow_forward,
    gizmo_arrow_backward,
};

Vector3 gizmo_arrows_position[] = {
    gizmo_arrow_up_position,
    gizmo_arrow_down_position,
    gizmo_arrow_right_position,
    gizmo_arrow_left_position,
    gizmo_arrow_forward_position,
    gizmo_arrow_backward_position
};

Vector3 gizmo_arrows_rotation[] = {
    gizmo_arrow_up_rotation,
    gizmo_arrow_down_rotation,
    gizmo_arrow_right_rotation,
    gizmo_arrow_left_rotation,
    gizmo_arrow_forward_rotation,
    gizmo_arrow_backward_rotation
};

string gizmo_arrows_drag_directions[] = {
    gizmo_arrow_up_drag_direction,
    gizmo_arrow_down_drag_direction,
    gizmo_arrow_right_drag_direction,
    gizmo_arrow_left_drag_direction,
    gizmo_arrow_forward_drag_direction,
    gizmo_arrow_backward_drag_direction
};





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
    windowWidth = childSize.x;
    windowHeight = childSize.y;

    ImVec2 windowPos = ImGui::GetWindowPos();
    windowX = windowPos.x;
    windowY = windowPos.y;

    rectangle.x = windowX;
    rectangle.y = windowY;
    
    // Rotate Texture in Y-Axis
    glm::mat4 matrix;
    matrix = glm::scale(matrix, glm::vec3(-1.0f, 1.0f, 1.0f));

    DrawTextureEx(*texture, (Vector2){0,0}, 0, 1.0f, WHITE);
    DrawTexturePro(*texture, (Rectangle){0, 0, texture->width, texture->height}, (Rectangle){0, 0, 0, 0}, (Vector2){0, 0}, 0.0f, WHITE);
    
    // Draw Texture
    ImGui::Image((ImTextureID)texture, ImVec2(windowWidth, windowHeight), ImVec2(0,1), ImVec2(1,0));
}


Vector2 GetMouseMove()
{
    static Vector2 lastMousePosition = { 0 };

    Vector2 mousePosition = GetMousePosition();
    Vector2 mouseMove = { mousePosition.x - lastMousePosition.x, mousePosition.y - lastMousePosition.y };

    lastMousePosition = mousePosition;
    
    return mouseMove;
}


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
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
    {
        float distance = Vector3Distance(scene_camera.position, scene_camera.target);
        Vector3 newTarget = {0};

        if (distance > 5) // Means the target is running away from the camera
        {
            Vector3 rayDirection = Vector3Subtract(scene_camera.position, scene_camera.target);
            rayDirection = Vector3Normalize(rayDirection);
            float distanceFromPlayer = 5;
            Vector3 finalPosition = Vector3Add(scene_camera.position, Vector3Scale(rayDirection, distanceFromPlayer));
            newTarget = finalPosition;

            float lerpSpeed = 0.01f;
            scene_camera.target = Vector3Lerp(scene_camera.target, newTarget, lerpSpeed);
        }
        else
        {
            Vector2 mouseMove = GetMouseMove();
            newTarget.x = scene_camera.target.x - cosf(mouseMove.x*0.1f)*cosf(mouseMove.y*0.1f);
            newTarget.y = scene_camera.target.y - sinf(mouseMove.y*0.1f);
            newTarget.z = scene_camera.target.z - sinf(mouseMove.x*0.1f)*cosf(mouseMove.y*0.1f);

            float lerpSpeed = 0.1f;
            scene_camera.target = Vector3Lerp(scene_camera.target, newTarget, lerpSpeed);   
        }

    }

}


// Dragging state variables
bool dragging_gizmo = false;
Vector2 mouse_drag_start = { 0, 0 };


float GetModelHeight(Model model) 
{
    BoundingBox modelBBox = GetMeshBoundingBox(model.meshes[0]);
    float modelHeight = modelBBox.max.y - modelBBox.min.y;
    return modelHeight;
}


bool IsMouseInRectangle(Vector2 mousePos, Rectangle rectangle)
{
    if (mousePos.x >= windowX && mousePos.x <= windowX + windowWidth &&
        mousePos.y >= windowY && mousePos.y <= windowY + windowHeight)
    {
        return true;
    }
    return false;
}




float GetExtremeValue(const Vector3& a) {
    return std::max(std::max(std::abs(a.x), std::abs(a.y)), std::abs(a.z));
}


bool IsMouseHoveringModel(Model model, Camera camera, Vector3 position, Vector3 rotation)
{
    float x = position.x;
    float y = position.y;
    float z = position.z;

    //std::cout << "X: " << x << " y: " << y << " z: " << z << std::endl;

    float extreme_rotation = GetExtremeValue(rotation);

    Matrix matScale = MatrixScale(1, 1, 1);
    Matrix matRotation = MatrixRotate(rotation, extreme_rotation*DEG2RAD);
    Matrix matTranslation = MatrixTranslate(x, y, z);

    Matrix modelMatrix = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);



    if (IsMouseInRectangle(GetMousePosition(), rectangle))
    {
        Ray ray = { 0 };

        Vector2 pos = { GetMousePosition().x - windowX, GetMousePosition().y - windowY };
        Vector2 realPos = { pos.x * GetScreenWidth()/rectangle.width, pos.y * GetScreenHeight()/rectangle.height };        
        //std::cout << "Position: " << realPos.x << ", " << realPos.y << ", " << std::endl;
        ray = GetMouseRay(realPos, camera);
        RayCollision meshHitInfo = { 0 };


        for (int mesh_i = 0; mesh_i < model.meshCount; mesh_i++)
        {
            meshHitInfo = GetRayCollisionMesh(ray, model.meshes[mesh_i], modelMatrix);
            if (meshHitInfo.hit)
            {
                return true;
            }
        }

        if (meshHitInfo.hit) return true;

    }

    return false;
}

int gizmo_arrow_selected;
int gizmo_taurus_selected;
bool isHoveringGizmo;

void Gizmo()
{
    // Gizmo Arrow Up
    gizmo_arrows_position[0] = {entity_in_inspector->position.x, entity_in_inspector->position.y + 6, entity_in_inspector->position.z};
    gizmo_arrows_rotation[0] = {0, 0, 0};

    // Gizmo Arrow Down
    gizmo_arrows_position[1] = {entity_in_inspector->position.x, entity_in_inspector->position.y - 6, entity_in_inspector->position.z};
    gizmo_arrows_rotation[1] = {180, 0, 0};

    // Gizmo Arrow Right
    gizmo_arrows_position[2] = {entity_in_inspector->position.x, entity_in_inspector->position.y, entity_in_inspector->position.z + 6};
    gizmo_arrows_rotation[2] = {90, 0, 0};

    // Gizmo Arrow Left
    gizmo_arrows_position[3] = {entity_in_inspector->position.x, entity_in_inspector->position.y, entity_in_inspector->position.z - 6};
    gizmo_arrows_rotation[3] = {-90, 0, 0};

    // Gizmo Arrow Forward
    gizmo_arrows_position[4] = {entity_in_inspector->position.x + 6, entity_in_inspector->position.y, entity_in_inspector->position.z};
    gizmo_arrows_rotation[4] = {0, 0, -90};

    // Gizmo Arrow Backward
    gizmo_arrows_position[5] = {entity_in_inspector->position.x - 6, entity_in_inspector->position.y, entity_in_inspector->position.z};
    gizmo_arrows_rotation[5] = {0, 0, 90};


    // Position Update
    for (int arrow_i = 0; arrow_i < size(gizmo_arrows); arrow_i++)
    {
        Color color1;

        if (!dragging_gizmo)
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmo_arrows[arrow_i], scene_camera, gizmo_arrows_position[arrow_i], gizmo_arrows_rotation[arrow_i]);
            
            if (isHoveringGizmo)
            {
                color1 = GREEN;
                gizmo_arrow_selected = arrow_i;
            }
            else
            {
                color1 = RED;
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
                    gizmo_arrow_up_position.y -= delta_y;
                }

                else if ( gizmo_arrow_selected == 2 || gizmo_arrow_selected == 3 )
                {
                    float delta_z = ((mouse_drag_end.x - mouse_drag_start.x) + (mouse_drag_end.y - mouse_drag_start.y)) * gizmo_drag_sensitivity_factor;
                    gizmo_arrow_up_position.z -= delta_z;
                }
                
                else if ( gizmo_arrow_selected == 4 || gizmo_arrow_selected == 5 )
                {
                    float delta_x = (mouse_drag_end.x - mouse_drag_start.x) * gizmo_drag_sensitivity_factor;
                    gizmo_arrow_up_position.x += delta_x;
                }

                // Update drag start position
                mouse_drag_start = mouse_drag_end;
            }
        }
        else dragging_gizmo = false;

        float extreme_rotation = GetExtremeValue(gizmo_arrows_rotation[arrow_i]);
        DrawModelEx(gizmo_arrows[arrow_i], gizmo_arrows_position[arrow_i], gizmo_arrows_rotation[arrow_i], extreme_rotation, {1,1,1}, color1);
    }
    
    entity_in_inspector->position.x = gizmo_arrow_up_position.x-entity_in_inspector->scale.x*6;
    entity_in_inspector->position.y = gizmo_arrow_up_position.y-entity_in_inspector->scale.y*6;
    entity_in_inspector->position.z = gizmo_arrow_up_position.z-entity_in_inspector->scale.z*6;





    // Gizmo Arrow Left
    gizmo_taurus_position[0] = {entity_in_inspector->position.x, entity_in_inspector->position.y, entity_in_inspector->position.z};
    gizmo_taurus_rotation[0] = {-90, 0, 0};

    // Gizmo Arrow Forward
    gizmo_taurus_position[1] = {entity_in_inspector->position.x, entity_in_inspector->position.y, entity_in_inspector->position.z};
    gizmo_taurus_rotation[1] = {0, 0, -90};

    // Gizmo Arrow Backward
    gizmo_taurus_position[2] = {entity_in_inspector->position.x, entity_in_inspector->position.y, entity_in_inspector->position.z};
    gizmo_taurus_rotation[2] = {0, 90, 0};


    // Position Update
    for (int taurus_i = 0; taurus_i < size(gizmo_taurus); taurus_i++)
    {

        Color color2;


        isHoveringGizmo = false;
        if (!dragging_gizmo)
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmo_taurus[taurus_i], scene_camera, gizmo_taurus_position[taurus_i], gizmo_taurus_rotation[taurus_i]);
            if (isHoveringGizmo)
            {
                color2 = BLUE;
                gizmo_taurus_selected = taurus_i;
            }
            else
            {
                color2 = DARKBLUE;
                gizmo_taurus_selected == -1;
            }
        }
        else
        {
            color2 = DARKBLUE;
            gizmo_taurus_selected == -1;
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
                if ( gizmo_taurus_selected == 0 )
                {
                    float delta_y = (mouse_drag_end.y - mouse_drag_start.y) * gizmo_drag_sensitivity_factor;
                    gizmo_taurus_up_position.y -= delta_y;
                }

                else if ( gizmo_taurus_selected == 1 )
                {
                    float delta_z = ((mouse_drag_end.x - mouse_drag_start.x) + (mouse_drag_end.y - mouse_drag_start.y)) * gizmo_drag_sensitivity_factor;
                    gizmo_taurus_up_position.z -= delta_z;
                }
                
                else if ( gizmo_taurus_selected == 2 )
                {
                    float delta_x = (mouse_drag_end.x - mouse_drag_start.x) * gizmo_drag_sensitivity_factor;
                    gizmo_taurus_up_position.x += delta_x;
                }

                // Update drag start position
                mouse_drag_start = mouse_drag_end;
            }
        }
        else dragging_gizmo = false;

        float extreme_rotation = GetExtremeValue(gizmo_taurus_rotation[taurus_i]);
        DrawModelEx(gizmo_taurus[taurus_i], gizmo_taurus_position[taurus_i], gizmo_taurus_rotation[taurus_i], extreme_rotation, {1,1,1}, color2);
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
    rectangle.width = windowWidth;
    rectangle.height = windowHeight;

    SetCameraMode(scene_camera, CAMERA_FREE); // Set a free camera mode
    UpdateCamera(&scene_camera);


    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    ClearBackground(GRAY);


    for (const Entity& entity : entities_list_pregame)
    {
        entity.draw();
        //entity.setShader(shader);
    }

    Gizmo();

    // End 3D rendering
    EndMode3D();
    EndTextureMode();


    DrawTextureOnRectangle(&texture, rectangle);

    return 0;
}
