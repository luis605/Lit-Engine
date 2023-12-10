#include "../../include_all.h"

#include "../../globals.h"
#include "RunGame.h"

void CleanScriptThreads(std::vector<std::thread>& script_threads) {
    for (auto& script_thread : script_threads) {
        if (script_thread.joinable())
            script_thread.join();
    }
    script_threads.clear();
}

void InitGameCamera() {
    camera.position = { 10.0f, 5.0f, 0.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };

    Vector3 front = Vector3Subtract(camera.target, camera.position);
    front = Vector3Normalize(front);

    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}


void RenderAndRunEntity(Entity& entity, LitCamera* rendering_camera = &camera) {
    entity.calc_physics = true;

    entity.setupScript(rendering_camera);
}


#ifndef GAME_SHIPPING
void RunGame()
{
    rectangle.width = sceneEditorWindowWidth;
    rectangle.height = sceneEditorWindowHeight;

    BeginTextureMode(renderTexture);
    BeginMode3D(camera);

    BeginShaderMode(shader);

        ClearBackground(GRAY);

        DrawSkybox();

        SetShaderValueMatrix(shader, GetShaderLocation(shader, "cameraMatrix"), GetCameraMatrix(scene_camera));

        dynamicsWorld->stepSimulation(GetFrameTime(), 10);

        if (first_time_gameplay)
        {
            #pragma omp parallel for
            for (Entity& entity : entities_list)
            {
                entity.running_first_time = true;

                #pragma omp critical
                RenderAndRunEntity(entity);
            }
        }

        #pragma omp parallel for
        for (Entity& entity : entities_list)
        {
            entity.render();

            #pragma omp critical
            entity.runScript(&camera);
        }

        
        first_time_gameplay = false;

        UpdateLightsBuffer();
        UpdateInGameGlobals();

    EndShaderMode();
    EndMode3D();

    DrawTextElements();
    DrawButtons();

    EndTextureMode();

    DrawTextureOnRectangle(&texture);
}
#endif



