/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "RunGame.hpp"
#include <Engine/Core/Entity.hpp>
#include <Engine/Core/Textures.hpp>
#include <Engine/Core/global_variables.hpp>
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
    entity.setupScript(rendering_camera);
    entity.setFlag(Entity::Flag::CALC_PHYSICS, true);
}

#ifndef GAME_SHIPPING
void RunGame() {
    BeginTextureMode(viewportRT);
    BeginMode3D(camera);

    BeginShaderMode(*shaderManager.m_defaultShader);

    ClearBackground(GRAY);

    skybox.drawSkybox(camera);

    physics.Update(GetFrameTime());
    UpdateLightsBuffer(true, lights);
    UpdateInGameGlobals();
    UpdateFrustum();

    if (firstTimeGameplay) {
        for (Entity& entity : entitiesList) {
            RenderAndRunEntity(entity);
            entity.setFlag(Entity::Flag::RUNNING_FIRST_TIME, true);
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

    ComputeSceneLuminance();
    RenderViewportTexture(camera);
}
#endif
