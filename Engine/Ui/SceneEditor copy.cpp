#include "../../include_all.h"


/* Scene Camera */

// Declare the render texture and texture objects as global variables
RenderTexture2D renderTexture;
Texture2D texture;
Rectangle rectangle = { screenWidth*.2, screenHeight*.2, texture.width, texture.height };

Camera3D scene_camera;
const float camera_speed = 1.0f;
const float lerp_factor = 0.5f;
float movementSpeed = 1.0f;
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



    mousePosition = GetMousePosition();

    // Check if right mouse button is being held
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
    {
        dragging = true;
    }
    else
    {
        dragging = false;
    }

    if (dragging)
    {
        float dx = mousePosition.x - mousePositionPrev.x;
        float dy = mousePosition.y - mousePositionPrev.y;

        float distance = sqrt(dx*dx + dy*dy);
        float acceleration = distance / 50.0f;

        if (mousePosition.x < mousePositionPrev.x)
        {
            scene_camera.target.z += 1 * acceleration;
        }
        else if (mousePosition.x > mousePositionPrev.x)
        {
            scene_camera.target.z -= 1 * acceleration;
        }

        if (mousePosition.y > mousePositionPrev.y)
        {
            scene_camera.target.y -= 1 * acceleration;
        }
        else if (mousePosition.y < mousePositionPrev.y)
        {
            scene_camera.target.y += 1 * acceleration;
        }
    }

    mousePositionPrev = mousePosition;

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
