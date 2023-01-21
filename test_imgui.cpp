#include "raylib.h"
#include "raymath.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "rlImGui.h"

#include "stb_image.h" // Use stb_image.h to load the image data from a file

#include <stdio.h>
#include <iostream>
#include <dirent.h>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <algorithm>
#include <libgen.h>

#include <boost/filesystem.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


using namespace std;
#include "globals.h"

#include "Engine.cpp"
#include "GUI.cpp"


Entity& EntitiesList();
Entity *entity_in_inspector = &Entity();
std::vector<Entity> entities_list;


// Declare a static variable for the texture ID
static ImTextureID icon_texture;

namespace filesys = boost::filesystem;

std::string getFileExtension(std::string filePath)
{
    // Create a Path object from given string
    filesys::path pathObj(filePath);
    // Check if file name in the path object has extension
    if (pathObj.has_extension()) {
        // Fetch the extension from path object and return
        return pathObj.extension().string();
    }
    // In case of no extension return empty string
    return "no file extension";
}



/* std::string GetFileExtension(const std::string& file_name)
{
    const char* file_extension = file_name.substr(file_name.find_last_of('.') + 1).c_str();
    int position=file_name.find_last_of(".");

    std::cout << "Position: " << position << std::endl;

    std::cout << "file_extension" << std::endl;
	if (strlen(file_extension) == 0)
	{
        std::cout << "No extension" << std::endl;
		return "NOFILEEXTENSION";
	}
	else
	{
        std::cout << "file_extension" << std::endl;
		return file_extension;
	}
} */

// Windows


void CodeEditor(string code)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##Code", code.c_str(), code.size(), size);
}


bool nameBufferInitialized = false;
// Create character arrays for the text boxes
char modelPathBuffer[512] = {0};


string inspectorNameBuffer = "";


// Initialize variables to store the scale, position, and rotation values
Vector3 entity_scale = {1, 1, 1};
Vector3 entity_position = {0, 0, 0};
Vector3 entity_rotation = {0, 0, 0};
string entity_name = "";
Color entityColor = RED;
bool changeEntityColorPopup = false;
bool setEntityColor = false;

void Inspector()
{
    // Get Entity
    string entity_name;

    if (!entities_list.empty())
    {
        entity_name = entity_in_inspector->name;
    }
    else
    {
        string entity_name = "None";
    }


    // Update Values


    // Draw the title bar with a rounded rectangle and centered text

    std::stringstream title;
    title << "Inspecting '" << entity_name << "'";
    ImGui::Text(title.str().c_str());


    // Draw the main content area with a rectangle
    ImGui::BeginChild("MainContent", ImVec2(0, 250));

    // Set the color of the button background
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));

    if (ImGui::Button("DELETE"))
    {
        // delete *entity_in_inspector;
        // entityList.erase(it);
    }

    ImGui::PopStyleColor();


    // Add other GUI elements to the inspector as needed
    ImGui::Text("Model Path:");
    ImGui::InputText("##ModelPathInputBox", modelPathBuffer, 512);

    ImGui::Text("Scale:");
    ImGui::InputFloat("X##ScaleX", &entity_scale.x);
    ImGui::InputFloat("Y##ScaleY", &entity_scale.y);
    ImGui::InputFloat("Z##ScaleZ", &entity_scale.z);
    entity_in_inspector->scale = entity_scale;


    ImGui::Text("Position:");
    ImGui::InputFloat("X##PositionX", &entity_position.x);
    ImGui::InputFloat("Y##PositionY", &entity_position.y);
    ImGui::InputFloat("Z##PositionZ", &entity_position.z);
    entity_in_inspector->position = entity_position;

    ImGui::EndChild();
}





// Variables
int listViewExScrollIndex = 0;
int listViewExActive = 0;
int listViewExFocus = 0;
std::vector<char*> listViewExList;
bool canAddEntity = false;


std::vector<std::string> entityNames;


char name[256] = { 0 };

float scale = 1;
ImVec4 color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

bool showNextTime = true;
bool create = false;


void updateListViewExList(vector<Entity>& entities) {
    // Clear the current values of listViewExList
    listViewExList.clear();

    // Clear the current values of entityNames
    entityNames.clear();

    // Store the names of the entities in entityNames
    for (int i = 0; i < entities.size(); i++) {
        entityNames.push_back(entities[i].getName());
    }

    
    // Resize listViewExList to match the size of entityNames
    listViewExList.reserve(100000000);
    listViewExList.resize(listViewExList.size()+1);

    // Set the values of listViewExList to the character pointers to the names in entityNames
    for (int i = 0; i < entityNames.size(); i++) {
        listViewExList[i] = entityNames[i].c_str();
    }
}




void AddEntity(void)
{
    // Define the layout of the popup
    const int POPUP_WIDTH = 600;
    const int POPUP_HEIGHT = 650;

    // Calculate the position of the popup
    int popupX = GetScreenWidth() / 4.5;
    int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 6;

    if (create) {
        // Conversion imgui color to raylib color
        Color entity_color_raylib = (Color){ (unsigned char)(color.x*255), (unsigned char)(color.y*255), (unsigned char)(color.z*255), (unsigned char)(color.w*255) };

        // Create Entity
        Entity entity_create;
        entity_create.setColor(entity_color_raylib);
        entity_create.setScale(Vector3 { scale, scale, scale, });
        entity_create.setName(name);
        entity_create.setModel("assets/models/tree.obj");
        entities_list.push_back(entity_create);
        
        create = false;
        canAddEntity = false;

        std::cout << "\n-----------------------\nEntities:\n---------\n";
        for (int i = 0; i < entityNames.size(); i++) {
            std::cout << listViewExList[i] << std::endl;
        }
        
    }
    else if (canAddEntity)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.6f, 0.6f, 0.6f, 0.6f)); // light gray

        ImGui::Begin("Entities");

        ImVec2 size = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 50.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

        ImGui::SetCursorPosX(size.x / 2 - 250);
        ImGui::SetCursorPosY(size.y / 4);
        ImGui::Button("Entity Add Menu", ImVec2(500,100));

        ImGui::PopStyleColor(4);

        /* Scale Slider */
        ImGui::Text("Scale: ");
        ImGui::SameLine();
        ImGui::SliderFloat(" ", &scale, 0.1f, 100.0f);

        /* Name Input */

        ImGui::InputText("##text_input_box", name, sizeof(name));
        
        /* Color Picker */
        ImGui::Text("Color: ");
        ImGui::SameLine();
        ImGui::ColorEdit4(" ", (float*)&color, ImGuiColorEditFlags_NoInputs);

        /* Create BTN */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14, 0.37, 0.15, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18, 0.48, 0.19, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

        ImGui::SetCursorPosX(size.x / 2);
        ImGui::SetCursorPosY(size.y / 1.1);
        bool create_entity_btn = ImGui::Button("Create", ImVec2(200,50));
        if (create_entity_btn)
        {
            canAddEntity = false;
            create = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::End();
    }
}



int ImGuiListViewEx(std::vector<std::string>& items, int& focus, int& scroll, int& active) {
    ImGui::SetNextWindowSize(ImVec2(400, 200)); // set the container size

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImVec2 childSize = ImVec2(.2 * screen.x, .7 * screen.y);
    ImGui::BeginChild("Entities List", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray

    ImGui::PushItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10,10)); // adding padding to the buttons

    // Buttons
    for (int i = 0; i < items.size(); i++)
    {
        if (i == active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.9f)); // dark gray
        
        int old_active = active;
        std::string button_name = items[i] + "##" + std::to_string(i);
        if (ImGui::Button(button_name.c_str(), ImVec2(120,40))) {
            std::cout << "Button Selected is at index: " << i << std::endl;
            focus = i;
            active = i;
        }

        if (i == old_active) ImGui::PopStyleColor();
        old_active = active;
        

    }

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();

    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    return active;
}


#include <iostream>
#include <exception>
#include <signal.h>
using namespace std;

void segfault_sigaction(int signal, siginfo_t *si, void *arg)
{
    printf("Caught segfault at address %p\n", si->si_addr);
}



// Widgets
Entity& EntitiesList()
{
/* 
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags   = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
 */


    updateListViewExList(entities_list);

    // Translate the rectangles coordinates to ImGui coordinates
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    Rectangle rec = { pos.x, pos.y, size.x, size.y };

    listViewExActive = ImGuiListViewEx(entityNames, listViewExFocus, listViewExScrollIndex, listViewExActive);


    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray


    bool add_entity = ImGui::Button("Add Entity", ImVec2(120,40));
    if (add_entity)
    {
        sprintf(name, "Entity Name");
        canAddEntity = true;
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if (!entities_list.empty()) std::cout << entities_list[listViewExActive].name << std::endl;
    
    
    return entities_list[listViewExActive];
}






// EDITOR CAMERA


/* Editor Camera */

// Declare the render texture and texture objects as global variables
RenderTexture2D renderTexture;
Texture2D texture;

// Declare the Rectangle
Rectangle rectangle = { screenWidth*.2, screenHeight*.2, texture.width, texture.height };

// Create a new Camera3D struct
Camera3D scene_camera;

// Camera Movement
// Define a speed value to control how fast the camera moves
const float camera_speed = 1.0f;

// Define a weighting factor to control how smoothly the camera moves
const float lerp_factor = 0.5f;

float movementSpeed = 1.0f;

// Define front
Vector3 front;

// Set up variables for dragging
bool dragging = false;
Vector2 mousePosition;
Vector2 mousePositionPrev = GetMousePosition();


void InitEditorCamera()
{
    // Create the render texture and texture objects once
    renderTexture = LoadRenderTexture( GetScreenWidth()*.4, GetScreenHeight()*.3 );
    texture = renderTexture.texture;

    // Set the position, target, and up vector of the camera
    scene_camera.position = { 10.0f, 0.0f, 0.0f };
    scene_camera.target = { 0.0f, 0.0f, 0.0f };
    scene_camera.up = { 0.0f, 1.0f, 0.0f }; // Set the up vector to point along the global +Y axis

    // Calculate the front vector by subtracting the position from the target
    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    // Set the fovy angle of the scene_camera
    scene_camera.fovy = 60.0f;
    scene_camera.projection = CAMERA_PERSPECTIVE;

}


void DrawTextureOnRectangle(const Texture *texture, Rectangle rectangle)
{

    // Set the position, size, and rotation of the texture
    Vector2 position = { rectangle.x*2, rectangle.y*1.8 };
    Vector2 size = { rectangle.width, rectangle.height };
    float rotation = 0.0f;

    // Create a 4x4 matrix
    glm::mat4 matrix;
    // Set the scale factor to (-1, 1) to flip the texture vertically
    matrix = glm::scale(matrix, glm::vec3(-1.0f, 1.0f, 1.0f));


    // Draw the texture with the flipped matrix
    DrawTextureEx(*texture, (Vector2){0,0}, 0, 1.0f, WHITE);
    DrawTexturePro(*texture, (Rectangle){0, 0, texture->width, texture->height}, (Rectangle){0, 0, 0, 0}, (Vector2){0, 0}, 0.0f, WHITE);
    
    // Draw the texture onto the rectangle using DrawTexturePro
    ImGui::Image((ImTextureID)texture, ImVec2(texture->width, texture->height), ImVec2(0,1), ImVec2(1,0));
}


void EditorCameraMovement(void)
{

    // Calculate the front vector by subtracting the position from the target
    Vector3 front = Vector3Subtract(scene_camera.target, scene_camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    Vector3 forward = Vector3Subtract(scene_camera.target, scene_camera.position);

    Vector3 right = Vector3CrossProduct(front, scene_camera.up);

    if (IsKeyDown(KEY_W))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed); // Calculate the movement vector based on the front vector and movement speed
        scene_camera.position = Vector3Add(scene_camera.position, movement); // Update the camera position

        scene_camera.target = Vector3Add(scene_camera.target, movement);

    }

    if (IsKeyDown(KEY_S))
    {
        Vector3 movement = Vector3Scale(front, movementSpeed); // Calculate the movement vector based on the front vector and movement speed
        scene_camera.position = Vector3Subtract(scene_camera.position, movement); // Update the camera position

        scene_camera.target = Vector3Subtract(scene_camera.target, movement);
    }

    if (IsKeyDown(KEY_A))
    {
        Vector3 movement = Vector3Scale(right, -movementSpeed); // Calculate the movement vector based on the right vector and negative movement speed
        scene_camera.position = Vector3Add(scene_camera.position, movement); // Update the camera position
        scene_camera.target = Vector3Add(scene_camera.target, movement); // Update the camera target
    }

    if (IsKeyDown(KEY_D))
    {
        Vector3 movement = Vector3Scale(right, -movementSpeed); // Calculate the movement vector based on the right vector and negative movement speed
        scene_camera.position = Vector3Subtract(scene_camera.position, movement); // Update the camera position
        scene_camera.target = Vector3Subtract(scene_camera.target, movement); // Update the camera target        
    }
    scene_camera.target = Vector3Add(scene_camera.position, forward);


    // Camera Rotation



    // Update input
    mousePosition = GetMousePosition();

    // Check if right mouse button is being held
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
    {
        dragging = true;
    }
    else
    {
        dragging = false;
    }

    // Update camera target position based on dragging direction
    if (dragging)
    {
        // Calculate the change in mouse position
        float dx = mousePosition.x - mousePositionPrev.x;
        float dy = mousePosition.y - mousePositionPrev.y;

        // Calculate the distance that the mouse was dragged
        float distance = sqrt(dx*dx + dy*dy);

        float acceleration = distance / 50.0f; // Adjust this value to control the rate of acceleration

        if (mousePosition.x < mousePositionPrev.x)
        {
            // Mouse is being dragged to the left, so move camera to the left
            scene_camera.target.z += 1 * acceleration;
        }
        else if (mousePosition.x > mousePositionPrev.x)
        {
            // Mouse is being dragged to the right, so move camera to the right
            scene_camera.target.z -= 1 * acceleration;
        }

        if (mousePosition.y > mousePositionPrev.y)
        {
            // Mouse is being dragged up, so move camera up
            scene_camera.target.y -= 1 * acceleration;
        }
        else if (mousePosition.y < mousePositionPrev.y)
        {
            // Mouse is being dragged down, so move camera down
            scene_camera.target.y += 1 * acceleration;
        }
    }


    mousePositionPrev = mousePosition;



    // Interpolate between the current position and the target position based on the lerp_factor
    //scene_camera.position = Vector3Lerp(scene_camera.position, target_position, lerp_factor);


//    cout << "Camera position: (" << scene_camera.position.x << ", " << scene_camera.position.y << ", " << scene_camera.position.z << ")" << endl;
//    cout << "Camera target: (" << scene_camera.target.x << ", " << scene_camera.target.y << ", " << scene_camera.target.z << ")" << endl;

}



void EditorCamera(void)
{
    // Define the rectangle where the texture will be drawn
    rectangle = { screenWidth*.2f, screenHeight*.2f, texture.width*1.0f, texture.height*1.0f };

    // Draw the rectangle with a gray outline
    DrawRectangleLinesEx(rectangle, 2, BLACK);


    // Editor Camera Movement
    EditorCameraMovement();


    // Begin rendering the 3D scene using the camera
    BeginTextureMode(renderTexture);
    BeginMode3D(scene_camera);

    ClearBackground(GRAY);


    // Loop through the vector and draw the Entity objects
    for (const Entity& entity : entities_list)
    {
        entity.draw();
        //std::cout << entity.getName() << std::endl;
    }


    // End 3D rendering
    EndMode3D();
    EndTextureMode();

    // Draw the texture onto the rectangle
    DrawTextureOnRectangle(&texture, rectangle);
}





int main(int argc, char* argv[])
{
    int screenWidth1 = 1900;
    int screenHeight1 = 900;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth1, screenHeight1, "raylib [ImGui] example - ImGui Demo");
    SetTargetFPS(144);
    rlImGuiSetup(true);



    Texture2D texture = LoadTexture("assets/images/gray_folder.png");
    Texture2D image_texture = LoadTexture("assets/images/image_file_type.png");
    Texture2D cpp_texture = LoadTexture("assets/images/cpp_file_type.png");
    Texture2D empty_texture = LoadTexture("assets/images/empty_file_file_type.png");

    struct FolderTextureItem {
        std::string name;
        Texture2D texture;
    };
    std::vector<FolderTextureItem> folders_texture;

    struct FileTextureItem {
        std::string name;
        Texture2D texture;
    };
    std::vector<FileTextureItem> files_texture;
    
    DIR *dir;
    struct dirent *ent;
    struct stat st;

    vector<string> files;
    vector<string> folders;

    string dir_path = "game";


    // show ImGui Content
    std::string code;
    code.resize(100000);



    // Init Docking
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking

    InitEditorCamera();
    // Main game loop
    while (!WindowShouldClose())
    {

        folders_texture.clear();
        files_texture.clear();
        folders.clear();
        files.clear();

        BeginDrawing();
        ClearBackground(DARKGRAY);

        // start the GUI
        rlImGuiBegin();




        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
        
        // Start docking area for the first window
        ImGui::Begin("Right Window", NULL);
        ImGui::SetNextWindowPos(ImVec2(0,1), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(200,200), ImGuiCond_Once);
        // Insert the contents of the first window here
    



        if ((dir = opendir(dir_path.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                string file = ent->d_name;
                if (file == ".") {
                    continue;
                }
                string path = dir_path.c_str();
                path += "/" + file;

                if (stat(path.c_str(), &st) == -1) {
                    cout << "Error: " << strerror(errno) << endl;
                }
                if (S_ISDIR(st.st_mode)) {
                    FolderTextureItem item = {file, texture};
                    folders_texture.push_back(item);

                    folders.push_back(file);
                } else {    
                    string file_extension = getFileExtension(basename(file.c_str()));
                    if (file_extension == "no file extension") {
                        FileTextureItem item = {file, empty_texture};
                        files_texture.push_back(item);
                    } else if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg") {
                        FileTextureItem item = {file, image_texture};
                        files_texture.push_back(item);
                    }
                    else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx") {
                        FileTextureItem item = {file, cpp_texture};
                        files_texture.push_back(item);
                    }

                }
            }
            // sort(files.begin(), files.end());
            // sort(folders.begin(), folders.end());
            // for (int i = 0; i < folders.size(); i++) {
            //     cout << folders[i] << " (folder)" << endl;
            // }
            // for (int i = 0; i < files.size(); i++) {
            //     cout << files[i] << " (file)" << endl;
            // }
            closedir(dir);
        } else {
            cout << "Error: " << strerror(errno) << endl;
        }







/*
        DIR* dir = opendir(dir_path.c_str());
        if (dir == NULL) {
            std::cout << "Error: Unable to open directory " << dir_path << std::endl;
            return 1;
        }

        // Iterate over the directory and print the names of subdirectories
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR && entry->d_name, ".") != 0 ) {
                
                FolderTextureItem item = {entry->d_name, texture};
                folders_texture.push_back(item);
            }
            else if (entry->d_type == DT_REG) {
                std::string file_extension = GetFileExtension(entry->d_name);
                if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg") {
                    FileTextureItem item = {entry->d_name, image_texture};
                    files_texture.push_back(item);
                }
                else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx") {
                    FileTextureItem item = {entry->d_name, cpp_texture};
                    files_texture.push_back(item);
                }
            }
        }

        closedir(dir);

*/

        int numButtons = folders_texture.size();
        ImTextureID imageIds[numButtons] = { 0 };

        // Set the number of columns in the layout
        ImGui::Columns(2);



        ImGui::Columns(5, "##imageListColumns", false);
        ImGui::SetColumnWidth(0, 128.0f);
        ImGui::SetColumnWidth(1, 128.0f);
        ImGui::SetColumnWidth(2, 128.0f);
        ImGui::SetColumnWidth(3, 128.0f);
        ImGui::SetColumnWidth(4, 128.0f);

        // FOLDERS List
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        for (int i = 0; i < numButtons; i++)
        {
            ImGui::PushID(i);
            if (ImGui::ImageButton((ImTextureID)&texture, ImVec2(128, 128)))
            {
                // Handle button press
                dir_path += "/" + folders_texture[i].name;

            }
            ImGui::Text(folders_texture[i].name.c_str());

            ImGui::PopID();
            ImGui::NextColumn();
        }


        // FILES List
        int numFileButtons = files_texture.size();
        //ImTextureID imageIds[numFileButtons] = { 0 };

        for (int i = 0; i < numFileButtons; i++)
        {
            ImGui::PushID(i);
            bool button = ImGui::ImageButton((ImTextureID)&files_texture[i].texture, ImVec2(128, 128));

            // Check if the button was double clicked
            if (ImGui::IsMouseDoubleClicked(button))
            {
                    // Handle double click
                    std::cout << "OPENING FILE" << std::endl;
                    std::ifstream file_content (dir_path + "/" + files_texture[i].name.c_str()); // this is equivalent to the above method
                    if ( file_content.is_open() ) { // always check whether the file is open
                        code.reserve(100000);
                        file_content >> code; // pipe file's content into stream
                        std::cout << code; // pipe stream's content to standard output
                        code.resize(100000);
                        }
                    else std::cout << "FILE NOT FOUND" << std::endl << dir_path + "/" + files_texture[i].name.c_str() << std::endl;
                    
            }

            ImGui::Text(files_texture[i].name.c_str());

            ImGui::PopID();
            ImGui::NextColumn();
        }

        ImGui::PopStyleVar();
        ImGui::End();

        // Code Editor
        ImGui::Begin("Code Editor Window", NULL);
        CodeEditor(code);
        ImGui::End();

        // Entities List
        ImGui::Begin("Entities List Window", NULL);

        if (!entities_list.empty())
        {
            entity_in_inspector = &EntitiesList();
        }
        else
        {
            EntitiesList();
            entity_in_inspector = &Entity();
        }
        
        
        ImGui::End();


        // Inspector
        ImGui::Begin("Inspector Window", NULL);
        Inspector();
        ImGui::End();

        // Scene Editor
        ImGui::Begin("Scene Editor Window", NULL);
        EditorCamera();
        ImGui::End();



        AddEntity();

        


        // end ImGui
        rlImGuiEnd();

        DrawFPS(screenWidth1*.9, screenHeight1*.1);
        // Finish drawing
        EndDrawing();
    }



    rlImGuiShutdown();
    CloseWindow(); 

    return 0;
}