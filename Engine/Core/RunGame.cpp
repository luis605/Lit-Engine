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

    camera.fovy = scene_camera.fovy;
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

    if (bloomEnabled)
    {
        BeginTextureMode(downsamplerTexture);
        BeginShaderMode(downsamplerShader);
            SetShaderValueTexture(downsamplerShader, GetShaderLocation(downsamplerShader, "srcTexture"), texture);
            Vector2 screenResolution = { static_cast<float>(texture.width), static_cast<float>(texture.height) };
            SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "srcResolution"), &screenResolution, SHADER_UNIFORM_VEC2);

            DrawTexture(texture,0,0,WHITE);
        EndShaderMode();
        EndTextureMode();

        BeginTextureMode(upsamplerTexture);
        BeginShaderMode(upsamplerShader);
            SetShaderValueTexture(downsamplerShader, GetShaderLocation(downsamplerShader, "srcTexture"), downsamplerTexture.texture);
            float filter = 100.0f;
            SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "filterRadius"), &filter, SHADER_UNIFORM_FLOAT);

            DrawTexture(downsamplerTexture.texture,0,0,WHITE);
        EndShaderMode();
        EndTextureMode();

        DrawTextureOnRectangle(&upsamplerTexture.texture);
    }
    else
    {
        DrawTextureOnRectangle(&texture);
    }
*}
#endif



