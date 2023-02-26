void AssetsExplorer(string code);

#include "../../include_all.h"







void AssetsExplorer(string code)
{
    

    folders_texture_struct.clear();
    files_texture_struct.clear();
    folders.clear();
    files.clear();
  
    ImGui::Begin("Right Window", NULL);
    ImGui::SetNextWindowPos(ImVec2(0,1), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(200,200), ImGuiCond_Once);

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
                FolderTextureItem item = {file, folder_texture};
                folders_texture_struct.push_back(item);

                folders.push_back(file);
            } else {    
                string file_extension = getFileExtension(basename(file.c_str()));
                if (file_extension == "no file extension") {
                    FileTextureItem item = {file, empty_texture};
                    files_texture_struct.push_back(item);
                } else if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg") {
                    FileTextureItem item = {file, image_texture};
                    files_texture_struct.push_back(item);
                }
                else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx") {
                    FileTextureItem item = {file, cpp_texture};
                    files_texture_struct.push_back(item);
                }
                else
                {
                    FileTextureItem item = {file, empty_texture};
                    files_texture_struct.push_back(item);
                }

            }
        }

        closedir(dir);
    } else {
        cout << "Error: " << strerror(errno) << endl;
    }


    int numButtons = folders_texture_struct.size();
    ImTextureID imageIds[numButtons] = { 0 };

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
        if (ImGui::ImageButton((ImTextureID)&folder_texture, ImVec2(128, 128)))
        {
            dir_path += "/" + folders_texture_struct[i].name;
            //std::cout << dir_path << std::endl;

        }
        ImGui::Text(folders_texture_struct[i].name.c_str());

        ImGui::PopID();
        ImGui::NextColumn();
    }


    // FILES List
    int numFileButtons = files_texture_struct.size();

    for (int i = 0; i < numFileButtons; i++)
    {
        ImGui::PushID(i);
        bool button = ImGui::ImageButton((ImTextureID)&files_texture_struct[i].texture, ImVec2(128, 128));

        // Our buttons are both drag sources and drag targets here!
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            // Set payload to carry the index of our item (could be anything)
            ImGui::SetDragDropPayload("DND_DEMO_CELL", &i, sizeof(int));

            ImGui::EndDragDropSource();
        }



        // Check if the button was double clicked
        if (ImGui::IsMouseDoubleClicked(button))
        {
                // Handle double click
                std::cout << "OPENING FILE" << std::endl;
                std::ifstream file_content (dir_path + "/" + files_texture_struct[i].name.c_str());
                if ( file_content.is_open() ) {
                    code.reserve(100000);
                    file_content >> code;
                    //std::cout << code;
                    code.resize(100000);
                    }
                else std::cout << "FILE NOT FOUND" << std::endl << dir_path + "/" + files_texture_struct[i].name.c_str() << std::endl;
                
        }

        ImGui::Text(files_texture_struct[i].name.c_str());

        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::PopStyleVar();
    ImGui::End();

}