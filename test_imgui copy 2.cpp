
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
	// Initialization
	//--------------------------------------------------------------------------------------


	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
	InitWindow(screenWidth, screenHeight, "Testing ImGUI");
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
    static std::string selected_tab = "Assets";


    // show ImGui Content
    std::string code;
    code.resize(100000);

	// Main game loop
	while (!WindowShouldClose())
	{
        folders_texture.clear();
        files_texture.clear();
        
		BeginDrawing();
		ClearBackground(DARKGRAY);

		rlImGuiBegin();

        DrawFPS(screenWidth*.9, screenHeight*.1);

        

        // Begin tab bar
        ImGui::SetNextWindowSize(ImVec2(screenWidth*.4, screenHeight*.354));
        ImGui::Begin("Assets and Code");
        ImGui::BeginTabBar("MyTabs");


        // Assets tab
        if (ImGui::BeginTabItem("Assets"))
        {
            ImGui::BeginChild("Assets", ImVec2(screenWidth*.4-30, screenHeight*.3), true);

            
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
                            file_content >> code; // pipe file's content into stream
                            std::cout << code; // pipe stream's content to standard output
                        }
                        else std::cout << "FILE NOT FOUND" << std::endl << dir_path + "/" + files_texture[i].name.c_str() << std::endl;

                                        
                }

                ImGui::Text(files_texture[i].name.c_str());

                ImGui::PopID();
                ImGui::NextColumn();
            }

            ImGui::PopStyleVar();


            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // Code editor tab
        if (ImGui::BeginTabItem("Code Editor"))
        {
            // Code editor panel content goes here
            ImVec2 size = ImVec2(screenWidth*.4-30, screenHeight*.3);
            ImGui::InputTextMultiline("##Code", code.c_str(), code.size(), size);
            ImGui::EndTabItem();
        }



        
        // End tab bar
        ImGui::EndTabBar();

        ImGui::End();

		// end ImGui Content
		rlImGuiEnd();

		EndDrawing();
		//----------------------------------------------------------------------------------
	}
	rlImGuiShutdown();

	// De-Initialization
	//--------------------------------------------------------------------------------------   
	CloseWindow();        // Close window and OpenGL context
	//--------------------------------------------------------------------------------------

	return 0;
}