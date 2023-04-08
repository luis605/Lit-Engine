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

    float cameraPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);


    for (const Entity& entity : entities_list)
    {
        entity.draw();
        if (first_time_gameplay)
        {
            
            entity.runScript();
        }
    }




    first_time_gameplay = false;



    // for (const Entity& entity : entities_list)
    // {

    //         cout << "pass here 1" << endl;
    //         pybind11::gil_scoped_release release;
    //         thread scriptRunnerThread(&Entity::runScript, entity);
    //         cout << "pass here 2" << endl;
    //         scriptRunnerThread.join();
    //         cout << "pass here 3" << endl;

    //         pybind11::gil_scoped_acquire acquire;
    //     }

    //     first_time_gameplay = false;

    //     entity.draw();
    //     entity.setShader(shader);    
    // }

    first_time_gameplay = false;

    // End 3D rendering
    EndMode3D();
    EndTextureMode();


    DrawTextureOnRectangle(&texture, rectangle);


}




