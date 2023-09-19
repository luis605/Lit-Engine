void AssetsExplorer(string code);

#include "../../include_all.h"

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

    if (!dir_path.empty())
    {
        for (fs::path entry : fs::directory_iterator(dir_path))
        {
            entry = entry.generic_string();
            fs::path file = entry.filename();
            if (file == "." || file == "..")
            {
                continue;
            }

            string path = file.string();
            if (!fs::exists(entry))
            {
                cout << "File Error: " << strerror(errno) << endl;
            }
            if (fs::is_directory(entry))
            {
                FolderTextureItem folderTextureItem = {file.string(), folder_texture};
                folders_texture_struct.push_back(folderTextureItem);

                folders.push_back(file.string());
            }
            else
            {
                string file_extension = getFileExtension(file.filename().string());
                if (file_extension == "no file extension")
                {
                    FileTextureItem fileTextureItem = {file.string(), empty_texture, file, entry};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg" ||
                         file_extension == ".avi" || file_extension == ".mp4" || file_extension == ".mov" ||
                         file_extension == ".mkv" || file_extension == ".webm" || file_extension == ".gif"
                )
                {
                    FileTextureItem fileTextureItem = {file.string(), image_texture, file, entry};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx")
                {
                    FileTextureItem fileTextureItem = {file.string(), cpp_texture, file, entry};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".py")
                {
                    FileTextureItem fileTextureItem = {file.string(), python_texture, file, entry};
                    files_texture_struct.push_back(fileTextureItem);
                }
                else if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" || file_extension == ".ply" || file_extension == ".mtl" || file_extension == ".mat")
                {
                    FileTextureItem fileTextureItem;

                    fileTextureItem = {file.string(), model_texture, file, entry};

                    auto iter = models_icons.find(file.string());

                    if (iter != models_icons.end()) {
                        Texture2D icon = iter->second;
                        fileTextureItem = {file.string(), icon, file, entry};
                    } else {
                        Texture2D icon = RenderModelPreview(entry.string().c_str());

                        RenderTexture2D target = LoadRenderTexture(icon.width, icon.height);

                        BeginTextureMode(target);

                        DrawTextureEx(icon, (Vector2){0, 0}, 0.0f, 1.0f, RAYWHITE);

                        EndTextureMode();

                        Texture2D flippedIcon = target.texture;
                        models_icons.insert({file.string(), flippedIcon});
                        fileTextureItem = {file.string(), flippedIcon, file, entry};
                    }

                    files_texture_struct.push_back(fileTextureItem);
                }
                else
                {
                    FileTextureItem fileTextureItem = {file.string(), empty_texture, file, entry};
                    files_texture_struct.push_back(fileTextureItem);
                }
            }
        }
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

    ImGui::BeginChild("Assets Explorer Child", ImVec2(-1,-1), true);

    int numButtons = folders_texture_struct.size();

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
        
        bool isButtonHovered = ImGui::IsItemHovered(); // Check if the button is hovered

        string file_extension = getFileExtension(files_texture_struct[i].path.filename().string());

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            const char *drag_type;

            if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg" ||
                file_extension == ".avi" || file_extension == ".mp4" || file_extension == ".mov" ||
                file_extension == ".mkv" || file_extension == ".webm" || file_extension == ".gif"
               )
            {
                drag_type = "TEXTURE_PAYLOAD";
            }
            else if (file_extension == ".py" || file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" || file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" || file_extension == ".hh" || file_extension == ".hxx")
                drag_type = "SCRIPT_PAYLOAD";
            else if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" || file_extension == ".ply" || file_extension == ".mtl")
                drag_type = "MODEL_PAYLOAD";
            else if (file_extension == ".mat")
                drag_type = "MATERIAL_PAYLOAD";
            
            ImGui::SetDragDropPayload(drag_type, &i, sizeof(int));
            ImGui::Image((void *)(intptr_t)(ImTextureID)&files_texture_struct[i].texture, ImVec2(64, 64));
            ImGui::EndDragDropSource();
        }


        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && isButtonHovered)
        {
            file_index = i;
            showEditFilePopup = true;
        }

        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && !isButtonHovered)
        {
            showAddFilePopup = true;
        }

        AddFileManipulation();
        EditFileManipulation();


        if (file_extension == ".mat" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            selected_game_object_type = "material";
            selected_material = files_texture_struct[i].full_path;
        }
        else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            code = read_file_to_string((dir_path / files_texture_struct[i].name).string());

            editor.SetText(code);
            code_editor_script_path = (dir_path / files_texture_struct[i].name).string();
        }
        else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
        {
            if (file_extension == ".py")
            {

            }
        }

        if (rename_file_index == i)
            files_texture_struct[i].rename = true;

        if (files_texture_struct[i].rename)
        {
            ImGui::InputText("##RenameFile", (char *)files_texture_struct[i].name.c_str(), files_texture_struct[i].name.size() + 100);
            if (ImGui::IsItemEdited() && IsKeyDown(KEY_ENTER))
            {
                if (fs::exists(rename_file_name)) {
                    fs::rename(rename_file_name, files_texture_struct[i].full_path);
                    std::cout << "File renamed successfully." << std::endl;
                } else {
                    std::cout << "File not found: " << files_texture_struct[i].full_path << std::endl;
                }
                files_texture_struct[i].rename = false;
                rename_file_index = -1;
            }
        }
        else
            ImGui::TextWrapped(files_texture_struct[i].name.c_str());


        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::PopStyleVar(3);
    ImGui::EndChild();
    ImGui::End();
}