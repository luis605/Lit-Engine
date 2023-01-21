#include "raylib.h"
#include "raymath.h"

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
            

            if(ImGui::Begin("Right Window", NULL))
            {
             
            
                ImGui::SetNextWindowPos(ImVec2(0,1), ImGuiCond_Once);
                ImGui::SetNextWindowSize(ImVec2(200,200), ImGuiCond_Once);
                ImGui::End();
            }


            if (ImGui::Begin("Left Window", NULL))
            {
                ImVec2 size = ImGui::GetContentRegionAvail();
                ImGui::InputTextMultiline("##Code", code.c_str(), code.size(), size);
                std::cout << code.size() << std::endl;
                ImGui::End();
            }
            

        }



        // end ImGui
        rlImGuiEnd();

        // Finish drawing
        EndDrawing();
    }



    rlImGuiShutdown();
    CloseWindow(); 

    return 0;
}