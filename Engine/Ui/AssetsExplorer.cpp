void AssetsExplorer(string code);

#include "../../include_all.h"


std::unordered_map<string, Texture2D> models_icons;

Texture2D RenderModelPreview(const char* modelFile) {
    Model model = LoadModel(modelFile);

    model_previewer_camera.position = (Vector3){ 10.0f, 10.0f, 2.0f };
    model_previewer_camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    model_previewer_camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    model_previewer_camera.fovy = 60.0f;
    model_previewer_camera.projection = CAMERA_PERSPECTIVE;


    RenderTexture2D target = LoadRenderTexture(600, 600);

    BeginTextureMode(target);
    ClearBackground(GRAY);

    BeginMode3D(model_previewer_camera);
    DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
    EndMode3D();

    EndTextureMode();
    UnloadModel(model);

    return target.texture;
}



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

    if ((dir = opendir(dir_path.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            string file = ent->d_name;
            if (file == "." || file == "..")
            {
                continue;
            }

            string path = dir_path.c_str();
            path += "/" + file;

            if (stat(path.c_str(), &st) == -1)
            {
                cout << "Error: " << strerror(errno) << endl;
            }
            if (S_ISDIR(st.st_mode))
            {
                FolderTextureItem folderTextureItem = {file, folder_texture};
                folders_texture_struct.push_back(folderTextureItem);

                folders.push_back(file);
            }
            else
            {
                string file_extension = getFileExtension(basename(file.c_str()));
                if (file_extension == "no file extension")
                {
                    FileTextureItem fileTextureItem = {file, empty_texture, file};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg")
                {
                    FileTextureItem fileTextureItem = {file, image_texture, file};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx")
                {
                    FileTextureItem fileTextureItem = {file, cpp_texture, file};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".py")
                {
                    FileTextureItem fileTextureItem = {file, python_texture, file};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" || file_extension == ".ply" || file_extension == ".mtl")
                {
                    FileTextureItem fileTextureItem;

                    if (file_extension == ".mtl")
                        fileTextureItem = {file, model_texture, file};
                    else
                    {
                        auto iter = models_icons.find(file);

                        if (iter != models_icons.end()) {
                            Texture2D icon = iter->second;
                            fileTextureItem = {file, icon, file};
                        } else {
                            Texture2D icon = RenderModelPreview(path.c_str());
                            models_icons.insert({file, icon});
                            fileTextureItem = {file, icon, file};
                        }
                    }
                    files_texture_struct.push_back(fileTextureItem);
                }
                else
                {
                    FileTextureItem fileTextureItem = {file, empty_texture, file};
                    files_texture_struct.push_back(fileTextureItem);
                }
            }
        }

        closedir(dir);
    }
    else
    {
        cout << "Error: " << strerror(errno) << endl;
    }

    if (dir_path != "project")
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

        if (ImGui::Button("<--"))
        {
            dir_path = dir_path.parent_path();
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

    ImGui::Button((std::string(dir_path.string()) + "##AssetsExplorerPath").c_str());

    ImGui::PopStyleColor(3);

    ImGui::BeginChild("MyChildWindowID");

    int numButtons = folders_texture_struct.size();
    ImTextureID imageIds[numButtons] = {0};

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

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&folder_texture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor(); 

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
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

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&files_texture_struct[i].texture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor(); 

        


        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            string file_extension = getFileExtension(basename(files_texture_struct[i].path.c_str()));
            const char *drag_type;

            if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg")
                drag_type = "TEXTURE_PAYLOAD";
            else if (file_extension == ".py" || file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx")
                drag_type = "SCRIPT_PAYLOAD";
            else if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" || file_extension == ".ply" || file_extension == ".mtl")
                drag_type = "MODEL_PAYLOAD";

            ImGui::SetDragDropPayload(drag_type, &i, sizeof(int));
            ImGui::Image((void *)(intptr_t)(ImTextureID)&files_texture_struct[i].texture, ImVec2(64, 64));
            ImGui::EndDragDropSource();
        }




        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            code = read_file_to_string((dir_path / files_texture_struct[i].name).string());

            editor.SetText(code);
            code_editor_script_path = (dir_path / files_texture_struct[i].name).string();
        }

        ImGui::TextWrapped(files_texture_struct[i].name.c_str());


        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::PopStyleVar(3);
    ImGui::EndChild();
    ImGui::End();
}