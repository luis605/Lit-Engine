void InitGameCamera() {
    camera.position = { 10.0f, 5.0f, 0.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };

    Vector3 front = Vector3Subtract(camera.target, camera.position);
    front = Vector3Normalize(front);

    camera.fovy = sceneCamera.fovy;
    camera.projection = CAMERA_PERSPECTIVE;
}

void RenderAndRunEntity(Entity& entity, LitCamera* rendering_camera = &camera) {
    entity.calcPhysics = true;
    entity.setupScript(rendering_camera);
}

#ifndef GAME_SHIPPING
void RunGame() {
    BeginTextureMode(viewportRenderTexture);
    BeginMode3D(camera);

    BeginShaderMode(shader);

        ClearBackground(GRAY);

        DrawSkybox();

        physics.Update(GetFrameTime());

        if (firstTimeGameplay) {
            #pragma omp parallel for
            for (Entity& entity : entitiesList) {
                entity.running_first_time = true;

                #pragma omp critical
                RenderAndRunEntity(entity);
            }
        }

        #pragma omp parallel for
        for (Entity& entity : entitiesList) {
            entity.render();

            #pragma omp critical
            entity.runScript(&camera);
        }

        
        firstTimeGameplay = false;

        UpdateLightsBuffer();
        UpdateInGameGlobals();

    EndShaderMode();
    EndMode3D();

    DrawTextElements();
    DrawButtons();

    EndTextureMode();

    if (bloomEnabled) {
        BeginTextureMode(downsamplerTexture);
        BeginShaderMode(downsamplerShader);
            SetShaderValueTexture(downsamplerShader, GetShaderLocation(downsamplerShader, "srcTexture"), viewportTexture);
            Vector2 screenResolution = { static_cast<float>(viewportTexture.width), static_cast<float>(viewportTexture.height) };
            SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "srcResolution"), &screenResolution, SHADER_UNIFORM_VEC2);

            DrawTexture(viewportTexture,0,0,WHITE);
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

        DrawTextureOnViewportRectangle(&upsamplerTexture.texture);
    } else {
        DrawTextureOnViewportRectangle(&viewportTexture);
    }
}
#endif



