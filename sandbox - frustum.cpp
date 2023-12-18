#include <iostream>
#include <vector>
#include "raylib.h"
#include "raymath.h"

#include "include/rlFrustum.cpp"

#include "include/rlImGui.h"
#include "imgui.h"


const int screenWidth = 1200;
const int screenHeight = 450;


RLFrustum cameraFrustum;

void InitFrustum()
{
    cameraFrustum = RLFrustum();
}

void UpdateFrustum()
{
    cameraFrustum.Extract();
}

bool PointInFrustum(const Vector3& point)
{
    return cameraFrustum.PointIn(point);
}

bool SphereInFrustum(const Vector3& position, float radius)
{
    return cameraFrustum.SphereIn(position, radius);
}

bool AABBoxInFrustum(const Vector3& min, const Vector3& max)
{
    return cameraFrustum.AABBoxIn(min, max);
}

class Entity {
public:
    Vector3 position = { 0, 0, 0 };
    Model model = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    BoundingBox bounds;

public:
    Entity(Vector3 position = {0, 0, 0})
        : position(position)
    {   

    }


    bool inFrustum()
    {
        return AABBoxInFrustum(bounds.min, bounds.max);
    }




    void render() {
        bounds = GetMeshBoundingBox(model.meshes[0]);

        model.transform = MatrixIdentity();
        model.transform = MatrixMultiply(model.transform, MatrixTranslate(position.x, position.y, position.z));

        bounds.min = Vector3Transform(bounds.min, model.transform);
        bounds.max = Vector3Transform(bounds.max, model.transform);

        if (inFrustum())
            DrawCube(position, 1, 1, 1, GREEN);
        else
            DrawCube(position, 1, 1, 1, RED);
    }

};


int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Frustum Culling");

    InitFrustum();

    std::vector<Entity> entities;

    for (int index = 0; index < 5000; index++) {
        Entity entity = Entity({ GetRandomValue(-50, 50), GetRandomValue(-50, 50), GetRandomValue(-50, 50) });
        entities.push_back(entity);
    }

    SetTargetFPS(60);

    Camera3D camera;
    camera.position = { 0, 0, -10 };
    camera.target = { 0, 0, 0 };
    camera.up = { 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;


    Camera3D render_camera;
    render_camera.position = { 0, 10, 0 };
    render_camera.target = { 0, 0, 0 };
    render_camera.up = { 0, 0, 1 };
    render_camera.fovy = 45.0f;
    render_camera.projection = CAMERA_PERSPECTIVE;

    RenderTexture renderTexture = LoadRenderTexture(screenWidth, screenHeight);

    DisableCursor();
    while (!WindowShouldClose()) {
        UpdateCamera(&camera, CAMERA_FREE);
        BeginDrawing();

            BeginTextureMode(renderTexture);
            ClearBackground(BLACK);
            BeginMode3D(render_camera);
                for (Entity& entity : entities) {
                    entity.render();
                }
            EndMode3D();
            EndTextureMode();


            ClearBackground(GRAY);            
            BeginMode3D(camera);
                for (Entity& entity : entities) {
                    UpdateFrustum();
                    entity.render();
                }
            EndMode3D();


        DrawTexturePro(renderTexture.texture, 
                       { 0.0f, 0.0f, (float)renderTexture.texture.width, (float)-renderTexture.texture.height },
                       { 0, 0, screenWidth/2, screenHeight/2 }, 
                       { 0, 0 }, 0.0f, WHITE);

        EndDrawing();
    }

    CloseWindow();
}