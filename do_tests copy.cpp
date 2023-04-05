#include <iostream>
#include <string>
#include <vector>
#include "raylib.h"
#include "raymath.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"

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

    vector<Entity*> children;



    Entity(Color color = { 255, 0, 0, 255 }, Vector3 scale = { 1, 1, 1 }, Vector3 rotation = { 0, 0, 0 }, string name = "entity", Vector3 position = {0, 0, 0})
        : color(color), scale(scale), rotation(rotation), name(name), position(position)
    {
    }

    void addChild(Entity& child) {
        Entity* newChild = new Entity(child);
        newChild->relative_position = Vector3Subtract(newChild->position, this->position);
        children.push_back(newChild);
    }






    void update()
    {
        int a = 0;
        for (Entity* child : children)
        {
            child->position = Vector3Add(this->position, child->relative_position);
            child->draw();


            a++;
//            child->update();
        }
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


Entity mainEntity;


void DebugWindow()
{
    ImGui::Begin("DebugWindow");
    ImGui::Text("Debug Window");
    bool button = ImGui::Button("Add Child entity");
    if (button)
    {
        Entity child = Entity();
        mainEntity.addChild(child);

    }
    ImGui::End();
}











int main()
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    

    int screenWidth1 = 1900;
    int screenHeight1 = 900;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth1, screenHeight1, "Lit Engine");
    SetTargetFPS(100000);
    rlImGuiSetup(true);

    SetTraceLogLevel(LOG_WARNING);
    
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    mainEntity = Entity();







    while (!WindowShouldClose())
    {

        if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
        {
            UpdateCamera(&camera, CAMERA_FREE);
        }


        BeginDrawing();
        ClearBackground(GRAY);

        // start the GUI
        rlImGuiBegin();

        DebugWindow();

        BeginMode3D(camera);

            if (IsKeyDown(KEY_W))
                mainEntity.position.x += 1;
            if (IsKeyDown(KEY_S))
                mainEntity.position.x -= 1;
            if (IsKeyDown(KEY_A))
                mainEntity.position.z += 1;
            if (IsKeyDown(KEY_D))
                mainEntity.position.z -= 1;
            if (IsKeyDown(KEY_E))
                mainEntity.position.y += 1;
            if (IsKeyDown(KEY_R))
                mainEntity.position.y -= 1;
            
            DrawGrid(10, 1.0f);
            mainEntity.draw();

        EndMode3D();



    // end ImGui
    rlImGuiEnd();

    DrawFPS(screenWidth1*.9, screenHeight1*.1);
    // Finish drawing
    EndDrawing();

    }






    std::cout << "Exiting..." << std::endl;

    rlImGuiShutdown();
    CloseWindow(); 

    return 0;
}
