// Include Headers
#include "raylib.h"
#include "Engine.cpp"

#include "globals.h"



// Declare the entities_list vector
std::vector<Entity> entities_list;



/* Editor Camera */

// Declare the render texture and texture objects as global variables
RenderTexture2D renderTexture;
Texture2D texture;

// Declare the Rectangle
Rectangle rectangle = { screenWidth*.2, screenHeight*.2, texture.width, texture.height };

// Create a new Camera3D struct
Camera3D scene_camera;

// Camera Movement
// Define a speed value to control how fast the camera moves
const float camera_speed = 1.0f;

// Define a weighting factor to control how smoothly the camera moves
const float lerp_factor = 0.5f;

float movementSpeed = 1.0f;

// Define front
Vector3 front;

// Set up variables for dragging
bool dragging = false;
Vector2 mousePosition;
Vector2 mousePositionPrev = GetMousePosition();


void InitEditorCamera()
{
    // Create the render texture and texture objects once
    renderTexture = LoadRenderTexture( GetScreenWidth()*.4, GetScreenHeight()*.3 );
    texture = renderTexture.texture;

    // Set the position, target, and up vector of the camera
    scene_camera.position = { 10.0f, 0.0f, 0.0f };
    scene_camera.target = { 0.0f, 0.0f, 0.0f };
    scene_camera.up = { 0.0f, 1.0f, 0.0f }; // Set the up vector to point along the global +Y axis

    // Calculate the front vector by subtracting the position from the target
    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    // Set the fovy angle of the scene_camera
    scene_camera.fovy = 60.0f;
    scene_camera.projection = CAMERA_PERSPECTIVE;

}


void DrawTextureOnRectangle(Texture2D texture, Rectangle rectangle)
{
    // Begin scissor testing using the rectangle as the clipping region
//    BeginScissorMode(rectangle.x, rectangle.y, rectangle.width, rectangle.height);

    // Set the position, size, and rotation of the texture
    Vector2 position = { rectangle.x*2, rectangle.y*1.8 };
    Vector2 size = { rectangle.width, rectangle.height };
    float rotation = 0.0f;

    // Draw the texture onto the rectangle using DrawTexturePro
    DrawTexturePro(texture, { 0.0f, 0.0f, (float)texture.width, -(float)texture.height }, { position.x, position.y, size.x, size.y }, { texture.width*0.5f, texture.height*0.5f }, rotation, WHITE);
    // End scissor testing
//    EndScissorMode();
}


void EditorCameraMovement(void)
{

    // Calculate the front vector by subtracting the position from the target
    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    Vector3 forward = Vector3Subtract(scene_camera.target, scene_camera.position);

    Vector3 right = Vector3CrossProduct(front, scene_camera.up);

    if (IsKeyDown(KEY_W))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed); // Calculate the movement vector based on the front vector and movement speed
        scene_camera.position = Vector3Add(scene_camera.position, movement); // Update the camera position

        scene_camera.target = Vector3Add(scene_camera.target, movement);

    }

    if (IsKeyDown(KEY_S))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed); // Calculate the movement vector based on the front vector and movement speed
        scene_camera.position = Vector3Subtract(scene_camera.position, movement); // Update the camera position

        scene_camera.target = Vector3Subtract(scene_camera.target, movement);
    }

    if (IsKeyDown(KEY_A))
    {
        Vector3 movement = Vector3Scale(right, -movementSpeed); // Calculate the movement vector based on the right vector and negative movement speed
        scene_camera.position = Vector3Add(scene_camera.position, movement); // Update the camera position
        scene_camera.target = Vector3Add(scene_camera.target, movement); // Update the camera target
    }

    if (IsKeyDown(KEY_D))
    {
        Vector3 movement = Vector3Scale(right, -movementSpeed); // Calculate the movement vector based on the right vector and negative movement speed
        scene_camera.position = Vector3Subtract(scene_camera.position, movement); // Update the camera position
        scene_camera.target = Vector3Subtract(scene_camera.target, movement); // Update the camera target        
    }
    scene_camera.target = Vector3Add(scene_camera.position, forward);


    // Camera Rotation



    // Update input
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

    // Update camera target position based on dragging direction
    if (dragging)
    {
        // Calculate the change in mouse position
        float dx = mousePosition.x - mousePositionPrev.x;
        float dy = mousePosition.y - mousePositionPrev.y;

        // Calculate the distance that the mouse was dragged
        float distance = sqrt(dx*dx + dy*dy);

        float acceleration = distance / 50.0f; // Adjust this value to control the rate of acceleration

        if (mousePosition.x < mousePositionPrev.x)
        {
            // Mouse is being dragged to the left, so move camera to the left
            scene_camera.target.z += 1 * acceleration;
        }
        else if (mousePosition.x > mousePositionPrev.x)
        {
            // Mouse is being dragged to the right, so move camera to the right
            scene_camera.target.z -= 1 * acceleration;
        }

        if (mousePosition.y > mousePositionPrev.y)
        {
            // Mouse is being dragged up, so move camera up
            scene_camera.target.y -= 1 * acceleration;
        }
        else if (mousePosition.y < mousePositionPrev.y)
        {
            // Mouse is being dragged down, so move camera down
            scene_camera.target.y += 1 * acceleration;
        }
    }


    mousePositionPrev = mousePosition;



    // Interpolate between the current position and the target position based on the lerp_factor
    //scene_camera.position = Vector3Lerp(scene_camera.position, target_position, lerp_factor);


//    cout << "Camera position: (" << scene_camera.position.x << ", " << scene_camera.position.y << ", " << scene_camera.position.z << ")" << endl;
//    cout << "Camera target: (" << scene_camera.target.x << ", " << scene_camera.target.y << ", " << scene_camera.target.z << ")" << endl;

}



void EditorCamera(void)
{
    // Define the rectangle where the texture will be drawn
    rectangle = { screenWidth*.2f, screenHeight*.2f, texture.width*1.0f, texture.height*1.0f };

    // Draw the rectangle with a gray outline
    DrawRectangleLinesEx(rectangle, 2, BLACK);


    // Editor Camera Movement
    EditorCameraMovement();


    // Begin rendering the 3D scene using the camera
    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    ClearBackground(GRAY);


    // Loop through the vector and draw the Entity objects
    for (const Entity& entity : entities_list)
    {
        entity.draw();
        //std::cout << entity.getName() << std::endl;
    }


    // End 3D rendering
    EndMode3D();
    EndTextureMode();

    // Draw the texture onto the rectangle
    DrawTextureOnRectangle(texture, rectangle);
}


