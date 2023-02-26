#include "../../include_all.h"


/* Scene Camera */

// Declare the render texture and texture objects as global variables
RenderTexture2D renderTexture;
Texture2D texture;
Rectangle rectangle = { screenWidth*.2, screenHeight*.2, texture.width, texture.height };

Camera3D scene_camera;
const float lerp_factor = 0.5f;
float movementSpeed = 0.5f;
Vector3 front;
bool dragging = false;
Vector2 mousePosition;
Vector2 mousePositionPrev = GetMousePosition();


void InitEditorCamera()
{
    renderTexture = LoadRenderTexture( GetScreenWidth(), GetScreenHeight() );
    texture = renderTexture.texture;

    scene_camera.position = { 10.0f, 0.0f, 0.0f };
    scene_camera.target = { 0.0f, 0.0f, 0.0f };
    scene_camera.up = { 0.0f, 1.0f, 0.0f }; // Set the up vector to point along the global +Y axis

    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    scene_camera.fovy = 60.0f;
    scene_camera.projection = CAMERA_PERSPECTIVE;
}


void DrawTextureOnRectangle(const Texture *texture, Rectangle rectangle)
{
    Vector2 position = { rectangle.x*2, rectangle.y*1.8 };
    Vector2 size = { rectangle.width, rectangle.height };
    float rotation = 0.0f;

    glm::mat4 matrix;
    // Set the scale factor to (-1, 1) to flip the texture vertically
    matrix = glm::scale(matrix, glm::vec3(-1.0f, 1.0f, 1.0f));

    DrawTextureEx(*texture, (Vector2){0,0}, 0, 1.0f, WHITE);

    ImVec2 childSize = ImGui::GetContentRegionAvail();
    float windowWidth = childSize.x;
    float windowHeight = childSize.y;

    DrawTexturePro(*texture, (Rectangle){0, 0, texture->width, texture->height}, (Rectangle){0, 0, 0, 0}, (Vector2){0, 0}, 0.0f, WHITE);
    
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
    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
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

    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
    {
        Vector2 mouseMove = GetMouseMove();
        Vector3 newTarget = {0};
        newTarget.x = scene_camera.target.x - cosf(mouseMove.x*0.1f)*cosf(mouseMove.y*0.1f);
        newTarget.y = scene_camera.target.y - sinf(mouseMove.y*0.1f);
        newTarget.z = scene_camera.target.z - sinf(mouseMove.x*0.1f)*cosf(mouseMove.y*0.1f);
        scene_camera.target = newTarget;

        float distance = Vector3Distance(scene_camera.position, scene_camera.target);
        if (distance > 5)
        {
            Vector3 forward = Vector3Normalize(Vector3Subtract(scene_camera.target, scene_camera.position));
            Vector3 newTarget = Vector3Add(scene_camera.position, Vector3Scale(forward, 5));
            scene_camera.target = newTarget;
        }
        std::cout << "Target Position: Vector3(" << scene_camera.target.x << ", " << scene_camera.target.y << ", " << scene_camera.target.z << ")" << std::endl;
    }

}



void EditorCamera(void)
{
    EditorCameraMovement();


    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    ClearBackground(GRAY);

   for (const Entity& entity : entities_list_pregame)
    {
        entity.draw();
    }


    // End 3D rendering
    EndMode3D();
    EndTextureMode();

    DrawTextureOnRectangle(&texture, rectangle);
}
