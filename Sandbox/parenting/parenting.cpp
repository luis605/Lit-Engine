#include <iostream>
#include <string>
#include <vector>
#include "raylib.h"
#include "raymath.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "rlImGui.h"

using namespace std;

/* Entity */
class Entity {
public:
    bool initialized = false;
    string name = "Entity";
    Color color;
    float size = 1;
    Vector3 position = { 0, 0, 0 };
    Vector3 rotation;
    Vector3 scale = { 1, 1, 1 };

    Vector3 relative_position = { 0, 0, 0 };
    Vector3 relative_rotation;
    Vector3 relative_scale = { 1, 1, 1 };

    Model model;

    bool isChildren = false;
    bool isParent = false;

    vector<Entity*>* children;

    Entity(Color color = { 255, 100, 100, 100 }, Vector3 scale = { 1, 1, 1 }, Vector3 rotation = { 0, 0, 0 }, string name = "entity", Vector3 position = {0, 0, 0})
        : color(color), scale(scale), rotation(rotation), name(name), position(position)
    {
    }

    void addChild(Entity* child) {
    }




    void update()
    {

    }

    void initializeModel() {
        Mesh mesh = GenMeshCube(scale.x, scale.y, scale.z);
        model = LoadModelFromMesh(mesh);
    }


    bool hasModel()
    {
        if (model.meshCount > 0)
            return true;
        else
            return false;
    }


    // Draw the model
    void draw() {
        if (!hasModel())
        {
            initializeModel();
        }

        update();
        
        model.transform = MatrixScale(scale.x, scale.y, scale.z);
        DrawModel(model, {position.x, position.y, position.z}, 1.0f, color);
    }


};



void DebugWindow()
{
    ImGui::Begin("DebugWindow");
    ImGui::Text("Debug Window");
    ImGui::End();
}



int main()
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    
    InitWindow(500, 500, "raylib [core] example - 3d camera free");
    rlImGuiSetup(true);
    
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Entity entity = Entity();







    while (!WindowShouldClose())
    {
        BeginDrawing();


        ClearBackground(GRAY);

        BeginMode3D(camera);

            rlImGuiBegin();

            float deltaTime = GetFrameTime();

            ImGui::GetIO().DeltaTime = 1+deltaTime;
            ImGui::NewFrame();

            

//            DebugWindow();

            DrawGrid(10, 1.0f);
            entity.draw();


            ImGui::EndFrame();

        EndMode3D();

        EndDrawing();
    }










    rlImGuiShutdown();

    return 0;
}
