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

    rectangle.width = windowWidth;
    rectangle.height = windowHeight;



    BeginTextureMode(renderTexture);
    BeginMode3D(camera);

    ClearBackground(GRAY);



    for (const Entity& entity : entities_list)
    {
        entity.draw();
        entity.runScript();        
    }


    // End 3D rendering
    EndMode3D();
    EndTextureMode();


    DrawTextureOnRectangle(&texture, rectangle);


}




