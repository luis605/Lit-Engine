#include "../include_all.h"
#include "../globals.h"

void InitGameCamera()
{


    camera.position = { 10.0f, 5.0f, 0.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f }; // Set the up vector to point along the global +Y axis

    Vector3 front = Vector3Subtract(camera.target, camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}




void RunGame()
{

    rectangle.width = sceneEditorWindowWidth;
    rectangle.height = sceneEditorWindowHeight;

    BeginTextureMode(renderTexture);
    BeginMode3D(camera);

    ClearBackground(GRAY);

    for (Entity& entity : entities_list)
    {
        entity.render();
        if (first_time_gameplay)
        {
            py::gil_scoped_acquire acquire;

            std::thread scriptRunnerThread([&entity]() {
                entity.runScript(std::ref(entity));
            });

            scripts_thread_vector.push_back(std::move(scriptRunnerThread));
        }
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




