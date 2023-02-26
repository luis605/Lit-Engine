#include <raylib.h>
#include "raymath.h"
#include <iostream>

/* Scene Camera */

Camera3D camera;
const float lerp_factor = 0.5f;
float movementSpeed = 0.5f;
Vector3 front;
bool dragging = false;
Vector2 mousePosition;
Vector2 mousePositionPrev = GetMousePosition();


void InitEditorCamera()
{
    camera.position = { 20.0f, 0.0f, 0.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f }; // Set the up vector to point along the global +Y axis

    Vector3 front = Vector3Subtract(camera.target, camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}


// Dragging state variables
bool dragging_gizmo = false;
Vector2 mouse_drag_start = { 0, 0 };




float GetModelHeight(Model model) 
{
    BoundingBox modelBBox = GetMeshBoundingBox(model.meshes[0]);
    return modelBBox.max.y - modelBBox.min.y;
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





int main(int argc, char* argv[])
{

    int screenWidth1 = 1900;
    int screenHeight1 = 900;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth1, screenHeight1, "Hovered Example");

    InitEditorCamera();
    Model gizmo_arrow = LoadModel("assets/models/gizmo/arrow.obj");

    SetCameraMode(camera, CAMERA_FREE); // Set a free camera mode

    while (!WindowShouldClose())
    {
        BeginDrawing();

        BeginMode3D(camera);
        ClearBackground(GRAY);

        UpdateCamera(&camera);

        Vector3 gizmo_arrow_position = {0, 0, 0};


        // Check mouse input

        Color color1;
        bool isHovering = IsMouseHoveringModel(gizmo_arrow, camera);

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
        EndDrawing();
    }

    UnloadModel(gizmo_arrow);


    return 0;
}
