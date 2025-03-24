#include "RunGame.hpp"
#include <Engine/Core/Entity.hpp>
#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include <Engine/GUI/Text/Text.hpp>
#include <Engine/Lighting/lights.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <Engine/Physics/PhysicsManager.hpp>
#include <Engine/Scripting/functions.hpp>

#define RAYMATH_IMPLEMENTATION
#include <raylib.h>
#include <raymath.h>

void InitGameCamera() {
    camera.pos = {10.0f, 5.0f, 0.0f};
    camera.position = {10.0f, 5.0f, 0.0f};
    camera.look_at = {0.0f, 0.0f, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up_vector = {0.0f, 1.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};

    Vector3 front = Vector3Subtract(camera.target, camera.position);
    front = Vector3Normalize(front);

    camera.fovy = sceneCamera.fovy;
    camera.projection = CAMERA_PERSPECTIVE;
}

void RenderAndRunEntity(Entity& entity, LitCamera* rendering_camera) {
    entity.calcPhysics = true;
    entity.setupScript(rendering_camera);
}

#ifndef GAME_SHIPPING
void RunGame() {
    BeginTextureMode(viewportRenderTexture);
    BeginMode3D(camera);

    BeginShaderMode(shader);

    ClearBackground(GRAY);

    skybox.drawSkybox(camera);

    physics.Update(GetFrameTime());
    UpdateLightsBuffer(true, lights);
    UpdateInGameGlobals();
    UpdateFrustum();

    if (firstTimeGameplay) {
        for (Entity& entity : entitiesList) {
            entity.running_first_time = true;
            RenderAndRunEntity(entity);
        }
        firstTimeGameplay = false;
    }

#pragma omp parallel for
    for (Entity& entity : entitiesList) {
        entity.render();
#pragma omp critical
        { entity.runScript(&camera); }
    }

    firstTimeGameplay = false;

    EndShaderMode();
    EndMode3D();

    DrawTextElements();
    DrawButtons();

    EndTextureMode();

    RenderViewportTexture();
}
#endif
