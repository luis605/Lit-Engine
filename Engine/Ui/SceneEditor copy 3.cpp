#include "../../include_all.h"


/* Scene Camera */


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



void InitEditorCamera()
{
    // Set Textures
    renderTexture = LoadRenderTexture( GetScreenWidth(), GetScreenHeight() );
    texture = renderTexture.texture;

    // Camera Proprieties
    scene_camera.position = { 10.0f, 0.0f, 0.0f };
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


bool IsMouseHoveringModel(Model model, Camera camera)
{
    Ray ray = { 0 };

    ray = GetMouseRay(GetMousePosition(), camera);
    RayCollision meshHitInfo = { 0 };

    BoundingBox towerBBox = GetMeshBoundingBox(model.meshes[0]);
    RayCollision boxHitInfo = GetRayCollisionBox(ray, towerBBox);

    if (boxHitInfo.hit)
    {
        std::cout << GetModelHeight(model) << std::endl;

        for (int m = 0; m < model.meshCount; m++)
        {
            meshHitInfo = GetRayCollisionMesh(ray, model.meshes[m], model.transform);
            if (meshHitInfo.hit) break;
        }

        if (meshHitInfo.hit) return true;
    }

    return false;
}



void EditorCamera(void)
{
//    EditorCameraMovement();
    rectangle.width = windowWidth;
    rectangle.height = windowHeight;

    SetCameraMode(scene_camera, CAMERA_FREE); // Set a free camera mode
    UpdateCamera(&scene_camera);

    Model gizmo_arrow = LoadModel("assets/models/gizmo/arrow.obj");

    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    ClearBackground(GRAY);

    for (const Entity& entity : entities_list_pregame)
    {
        entity.draw();
    }




    Vector3 gizmo_arrow_position = {0, 0, 0};


    // Check mouse input

    Color color1;
    bool isHovering = IsMouseHoveringModel(gizmo_arrow, scene_camera);

    if (isHovering) color1 = GREEN;
    else color1 = RED;

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
    {
        if (isHovering)
        {
            if (!dragging_gizmo)
            {
                // Start dragging
                mouse_drag_start = GetMousePosition();
                dragging_gizmo = true;
            }
            else
            {
                // Update position
                Vector2 mouse_drag_end = GetMousePosition();
                float delta_y = (mouse_drag_end.y - mouse_drag_start.y);
                gizmo_arrow_position.y -= delta_y;

                // Update drag start position
                mouse_drag_start = mouse_drag_end;
            }
        }
    }

    DrawModel(gizmo_arrow, gizmo_arrow_position, 1.0f, color1);




    // End 3D rendering
    EndMode3D();
    EndTextureMode();

    UnloadModel(gizmo_arrow);

    DrawTextureOnRectangle(&texture, rectangle);
}
