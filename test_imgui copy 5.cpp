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

using namespace std;
#include "globals.h"


#include "EditorCamera.cpp"
#include "GUI.cpp"


// Declare a static variable for the texture ID
static ImTextureID icon_texture;

std::string GetFileExtension(const std::string& file_name)
{
    size_t dot_pos = file_name.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return ""; // No extension found
    }
    return file_name.substr(dot_pos + 1);
}


// Windows


void CodeEditor(string code)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##Code", code.c_str(), code.size(), size);
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
    const int INPUT_FIELD_WIDTH = 360;
    const int INPUT_FIELD_HEIGHT = 20;

    // Calculate the position of the popup
    int popupX = GetScreenWidth() / 4.5;
    int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 6;

    if (create) {
        // Conversion
        Color entity_color_raylib = (Color){ (unsigned char)(color.x*255), (unsigned char)(color.y*255), (unsigned char)(color.z*255), (unsigned char)(color.w*255) };

        // Add the entity to the list
        Entity entity_create;
        entity_create.setColor(entity_color_raylib);
        entity_create.setScale(Vector3 { scale, scale, scale, });
        entity_create.setName("New Entity");
        entity_create.setModel("assets/models/tree.obj");
        entities_list.push_back(entity_create);

        // Update Entities List
        updateListViewExList(entities_list);
        
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

/* 
        // Scale
        scale = GuiSliderBar((Rectangle){ popupX + 140, popupY + 250, 200, 20 }, "Scale: ", TextFormat("%i", (int)scale), scale, 0.1, 100);
        
        // Color
        color = GuiColorPicker((Rectangle){ popupX + 100, popupY + 300, 360, 260 }, "Color: ", color);

        // Show Next Time
        showNextTime = GuiCheckBox(Rectangle{ popupX + 120, popupY + 600, 20, 20 }, "Show Next Time", showNextTime); 

        // Create Entity Button
        create = Button(popupX + 400, popupY + 600, 100, 30, "Create Entity");
 */

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
    for (int i = 0; i < items.size(); i++) {
        if (ImGui::Button(items[i].c_str(), ImVec2(120,40))) {
            std::cout << "Button Selected is at index: " << i << std::endl;
            focus = i;
            active = i;
        }
    }

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();

    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    return active;
}







// Widgets
void EntitiesList()
{

    // Translate the rectangles coordinates to ImGui coordinates
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    Rectangle rec = { pos.x, pos.y, size.x, size.y };

    listViewExActive = ImGuiListViewEx(entityNames, listViewExFocus, listViewExScrollIndex, listViewExActive);


    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray

    if (ImGui::Button("Add Entity", ImVec2(120,40)))
    {
        canAddEntity = true;
        AddEntity();
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    
    
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

    std::string dir_path = "game";


    // show ImGui Content
    std::string code;
    code.resize(100000);



    // Init Docking
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking

    // Main game loop
    while (!WindowShouldClose())
    {

        folders_texture.clear();
        files_texture.clear();

        BeginDrawing();
        ClearBackground(DARKGRAY);

        // start the GUI
        rlImGuiBegin();



        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
            
            // Start docking area for the first window
            ImGui::Begin("Right Window", NULL);
            ImGui::SetNextWindowPos(ImVec2(0,1), ImGuiCond_Once);
            ImGui::SetNextWindowSize(ImVec2(200,200), ImGuiCond_Once);
            // Insert the contents of the first window here
        
            DIR* dir = opendir(dir_path.c_str());
            if (dir == NULL) {
                std::cout << "Error: Unable to open directory " << dir_path << std::endl;
                return 1;
            }

            // Iterate over the directory and print the names of subdirectories
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 ) {
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
                    dir_path = dir_path + "/" + folders_texture[i].name;
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
            EntitiesList();
            ImGui::End();

            AddEntity();

        }

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