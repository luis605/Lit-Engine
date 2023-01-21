
#define RL_LOG_LEVEL RL_LOG_ERROR
#include "raylib.h"
#include "raymath.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <stdio.h>
#include <iostream>
#include <ncurses.h>
#include <string>
#include <vector>

#include <python3.11/Python.h>


// Include Lit Engine functionalities
#include "GUI.cpp"
#include "EditorCamera.cpp"
#include "EntitiesList.cpp"
#include "CodeEditor.cpp"
#include "Inspector.cpp"


using namespace std;

#define screenWidth    GetScreenWidth()
#define screenHeight   GetScreenHeight()
#define MAX_COLUMNS 20

#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION            330
#else   // PLATFORM_RPI, PLATFORM_ANDROID, PLATFORM_WEB -> Not supported at this moment
    #define GLSL_VERSION            100
#endif

void InitEditorCamera();
void EditorCamera(void);
bool Button(int x, int y, int scale_x, int scale_y, char *text);
void AddEntity(void);
Entity& EntitiesList();
static void codeEditor(void);


// Developer Options
bool DEGUG_MODE = true;



// COLORS
const Color colWhite = { 255, 255, 255, 255 };



Entity entity_in_inspector;



//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main()
{
    // Initialization
    //---------------------------------------------------------------------------------------


    // Initialize the vector with the desired number of Entity objects
    entities_list.reserve(10000000);

    // // Print the current capacity and maximum size of the vector
    // std::cout << "Vector capacity: " << entities_list.capacity() << std::endl;
    // std::cout << "Vector max size: " << entities_list.max_size() << std::endl;

    // Entity cube1;
    // cube1.setColor(RED);
    // cube1.setName("Hello World");
    // entities_list.push_back(cube1);
    // updateListViewExList(entities_list);

    // // Print the size of the Entity object
    std::cout << "Entity size: " << sizeof(Entity()) << " bytes" << std::endl;


    // entities_list.push_back(Entity());
    // // Create a new Entity object
    // Entity newEntity;

    // // Add the new Entity object to the end of the vector
    // entities_list.push_back(newEntity);






    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "Lively Engine");

    SetTraceLogLevel(LOG_ERROR);
    
    
    SetTargetFPS(60);
    //---------------------------------------------------------------------------------------

    const Color BACKGROUND_COLOR = { 1, 39, 49, 1 };

    InitEditorCamera();
    Shader shader = LoadShader(0, TextFormat("resources/shaders/glsl%i/raymarching.fs", GLSL_VERSION));

//    InitEntityManager();



    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {

        GuiSetStyle(BUTTON, BASE, ColorToInt(WHITE));

        // Define the layout of the popup
        const int POPUP_WIDTH = 400;
        const int POPUP_HEIGHT = 300;
        const int INPUT_FIELD_WIDTH = 360;
        const int INPUT_FIELD_HEIGHT = 20;

        // Calculate the position of the popup
        int popupX = (GetScreenWidth() - POPUP_WIDTH) / 2;
        int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 2;


        // Define the rectangle
        Rectangle rec = { popupX, popupY, POPUP_WIDTH, POPUP_HEIGHT };

        BeginDrawing();
            // Window Colors
            ClearBackground(BACKGROUND_COLOR);

            // FPS Info
            if (DEGUG_MODE)
            {
                DrawFPS(screenWidth*.9, screenHeight*.1);
            }
            
            // Entities Scroll Panel
            //Entity &entity_in_inspector = EntitiesList();
            //entity_in_inspector.color = GREEN;

            // Code Editor
            codeEditor();

            // panelContentRec.width = GuiSliderBar((Rectangle){ 590, 385, 145, 15}, "WIDTH", TextFormat("%i", (int)panelContentRec.width), panelContentRec.width, 1, 600);
            // panelContentRec.height = GuiSliderBar((Rectangle){ 590, 410, 145, 15 }, "HEIGHT", TextFormat("%i", (int)panelContentRec.height), panelContentRec.height, 1, 400);

            // Editor Camera
            EditorCamera();

            // Inspector
            Inspector();

            // Draw the rectangle
            if (canAddEntity)
            {
                AddEntity();
            }
        EndDrawing();
        //----------------------------------------------------------------------------------


        if (canAddEntity)
        {
            AddEntity();
        }

    }


    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}






