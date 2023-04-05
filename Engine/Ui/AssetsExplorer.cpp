void AssetsExplorer(string code);

#include "../../include_all.h"




float padding = 10.0f;
float thumbnailSize = 128.0f;
float cellSize = thumbnailSize + padding;

ImVec2 AssetsExplorer_window_size = {cellSize, cellSize};


void AssetsExplorer()
{
    folders_texture_struct.clear();
    files_texture_struct.clear();
    folders.clear();
    files.clear();







    if (AssetsExplorer_window_size.x < cellSize)
    {
        ImGui::SetNextWindowSize({cellSize, AssetsExplorer_window_size.y});
    }
    else if (AssetsExplorer_window_size.y < cellSize)
        ImGui::SetNextWindowSize({AssetsExplorer_window_size.y, cellSize});

    ImGui::Begin("Assets Explorer", NULL);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5);

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
                FolderTextureItem folderTextureItem = {file, folder_texture};
                folders_texture_struct.push_back(folderTextureItem);

                folders.push_back(file);
            } else {    
                string file_extension = getFileExtension(basename(file.c_str()));
                if (file_extension == "no file extension") {
                    FileTextureItem fileTextureItem = {file, empty_texture};
                    files_texture_struct.push_back(fileTextureItem);
                } else if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg") {
                    FileTextureItem fileTextureItem = {file, image_texture};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx") {
                    FileTextureItem fileTextureItem = {file, cpp_texture};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".py") {
                    FileTextureItem fileTextureItem = {file, python_texture};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" || file_extension == ".ply" || file_extension == ".mtl") {
                    FileTextureItem fileTextureItem = {file, model_texture};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else
                {
                    FileTextureItem fileTextureItem = {file, empty_texture};
                    files_texture_struct.push_back(fileTextureItem);
                }

            }
        }

        closedir(dir);
    } else {
        cout << "Error: " << strerror(errno) << endl;
    }


    int numButtons = folders_texture_struct.size();
    ImTextureID imageIds[numButtons] = { 0 };

    /* Organization of content [ FOLDERS && FILES ] */
    /*
    ____________   ____________
    |          |   |          |
    |  FOLDER  |   |   File   |
    |          |   |          |
    ------------   ------------
    Folder Name    Filename.py

    */



    // Collumns
    float panelWidth = ImGui::GetContentRegionAvail().x;

    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    ImGui::Columns(columnCount, "##AssetsExplorerListColumns", false);
    
    // Window Size Readjust
    AssetsExplorer_window_size = ImGui::GetWindowSize();


    // FOLDERS List
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    for (int i = 0; i < numButtons; i++)
    {
        ImGui::PushID(i);
        bool folder_button = ImGui::ImageButton((ImTextureID)&folder_texture, ImVec2(thumbnailSize, thumbnailSize));
        if (folder_button)
        {
            dir_path += "/" + folders_texture_struct[i].name;
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
        bool file_button = ImGui::ImageButton((ImTextureID)&files_texture_struct[i].texture, ImVec2(thumbnailSize, thumbnailSize));

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload("DND_DEMO_CELL", &i, sizeof(int));
            ImGui::Image((void*)(intptr_t)(ImTextureID)&files_texture_struct[i].texture, ImVec2(64, 64));
            ImGui::EndDragDropSource();
        }

        if (file_button)
        {
            code = read_file_to_string(dir_path + "/" + files_texture_struct[i].name.c_str());
            code_editor_script_path = dir_path + "/" + files_texture_struct[i].name.c_str();
        }
        ImGui::Text(files_texture_struct[i].name.c_str());

        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::PopStyleVar(3);
    ImGui::End();

}