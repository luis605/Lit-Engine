#ifndef GAME_SHIPPING
    #include "../include_all.h"
#endif

#include "../globals.h"
#include "RunGame.h"



void CleanScriptThreads(vector<std::thread>& script_threads)
{
    for (auto& script_thread : script_threads) {
        if (script_thread.joinable())
            script_thread.join();
    }

    script_threads.clear();
}

void InitGameCamera()
{
    camera.position = { 10.0f, 5.0f, 0.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };

    Vector3 front = Vector3Subtract(camera.target, camera.position);
    front = Vector3Normalize(front);

    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}






void RenderAndRunEntity(Entity& entity, vector<std::thread>& scripts_threads, bool first_time_flag = first_time_gameplay, LitCamera* rendering_camera = &camera)
{
    entity.calc_physics = true;
    entity.render();

    if (first_time_flag && !entity.script.empty())
    {
        py::gil_scoped_acquire acquire;

        std::thread scriptRunnerThread([&entity, rendering_camera]() {
            entity.runScript(std::ref(entity), rendering_camera);
        });


        scripts_threads.push_back(std::move(scriptRunnerThread));
    }

    entity.render();


}


#ifndef GAME_SHIPPING
void RunGame()
{

    rectangle.width = sceneEditorWindowWidth;
    rectangle.height = sceneEditorWindowHeight;

    BeginTextureMode(renderTexture);
    BeginMode3D(camera);

    ClearBackground(GRAY);

    dynamicsWorld->stepSimulation(1.0f / 60.0f);

    UpdateInGameGlobals();

    for (Entity& entity : entities_list)
    {
        RenderAndRunEntity(entity, scripts_thread_vector);
    }


    if (first_time_gameplay)
    {
        for (auto& script_thread : scripts_thread_vector) {
            if (script_thread.joinable())
                script_thread.detach();
        }
    }

    first_time_gameplay = false;






    EndMode3D();
    EndTextureMode();

    DrawTextureOnRectangle(&texture);
}
#endif



