void AssetsExplorer(string code);

#include "../../include_all.h"

void InitRenderModelPreviewer() {
    model_previewer_camera.position = (Vector3){ 10.0f, 10.0f, 2.0f };
    model_previewer_camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    model_previewer_camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    model_previewer_camera.fovy = 60.0f;
    model_previewer_camera.projection = CAMERA_PERSPECTIVE;

    target = LoadRenderTexture(thumbnailSize, thumbnailSize);
}

Texture2D RenderModelPreview(const char* modelFile) {
    Model model = LoadModel(modelFile);

    BeginTextureMode(target);
    ClearBackground(GRAY);

    BeginMode3D(model_previewer_camera);
    DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
    EndMode3D();

    EndTextureMode();
    UnloadModel(model);

    return target.texture;
}

void AssetsExplorer() {
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

    ImGui::Begin((std::string(ICON_FA_FOLDER_OPEN) + " Assets Explorer").c_str(), NULL);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5);

    if (!dir_path.empty()) {
        fs::path dirPath = dir_path;
        if (fs::exists(dirPath)) {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                fs::path file = entry.path().filename();
                if (file == "." || file == "..") {
                    continue;
                }

                string path = file.string();
                if (!fs::exists(entry)) {
                    cout << "File Error: " << strerror(errno) << endl;
                }

                if (fs::is_directory(entry)) {
                    folders_texture_struct.emplace_back(file.string(), folder_texture, entry);
                    folders.emplace_back(file.string());
                } else {
                    string file_extension = getFileExtension(file.filename().string());
                    FileTextureItem fileTextureItem;

                    if (file_extension == "no file extension") {
                        fileTextureItem = {file.string(), empty_texture, file, entry};
                    } else if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".jpeg" ||
                            file_extension == ".avi" || file_extension == ".mp4" || file_extension == ".mov" ||
                            file_extension == ".mkv" || file_extension == ".webm" || file_extension == ".gif") {
                        fileTextureItem = {file.string(), image_texture, file, entry};
                    } else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" ||
                            file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" ||
                            file_extension == ".hh" || file_extension == ".hxx") {
                        fileTextureItem = {file.string(), cpp_texture, file, entry};
                    } else if (file_extension == ".py") {
                        fileTextureItem = {file.string(), python_texture, file, entry};
                    } else if (file_extension == ".mtl" || file_extension == ".mat") {
                        fileTextureItem = {file.string(), material_texture, file, entry};
                    } else if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" ||
                            file_extension == ".ply") {
                        fileTextureItem = {file.string(), model_texture, file, entry};

                        auto iter = models_icons.find(file.string());

                        if (iter != models_icons.end()) {
                            fileTextureItem = {file.string(), iter->second, file, entry};
                        } else {
                            Texture2D icon = RenderModelPreview(entry.path().string().c_str());
                            RenderTexture2D target = LoadRenderTexture(icon.width, icon.height);

                            BeginTextureMode(target);
                            DrawTextureEx(icon, (Vector2){0, 0}, 0.0f, 1.0f, RAYWHITE);
                            EndTextureMode();

                            Texture2D flippedIcon = target.texture;
                            models_icons[file.string()] = flippedIcon;
                            fileTextureItem = {file.string(), flippedIcon, file, entry};
                        }
                    } else {
                        fileTextureItem = {file.string(), empty_texture, file, entry};
                    }

                    files_texture_struct.emplace_back(std::move(fileTextureItem));
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
    float columnWidth = cellSize + 40.0f;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    ImGui::Columns(columnCount, "##AssetsExplorerListColumns", false);

    for (int i = 0; i < columnCount; ++i) {
        ImGui::SetColumnWidth(i, columnWidth);
    }

    // Window Size Readjust
    AssetsExplorer_window_size = ImGui::GetWindowSize();

    bool isFolderHovered = false;

    // FOLDERS List
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    for (int i = 0; i < numButtons; i++)
    {
        ImGui::PushID(i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&folder_texture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor(); 

        bool isButtonHovered = ImGui::IsItemHovered();
        
        if (isButtonHovered) isFolderHovered = true;

        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && isButtonHovered)
        {
            folder_index = i;
            showEditFolderPopup = true;
        }

        float textWidth = ImGui::CalcTextSize(folders_texture_struct[i].name.c_str()).x;
        float buttonWidth = thumbnailSize;
        float offset = (buttonWidth - textWidth) * 0.5f;

        float centerPosX = (ImGui::GetCursorPosX() + offset);

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            dir_path += "/" + folders_texture_struct[i].name;
        }


        if (rename_folder_index == i)
            folders_texture_struct[i].rename = true;
            
        if (folders_texture_struct[i].rename)
        {
            ImGui::InputText("##RenameFolder", (char*)rename_folder_buffer, 256);

            if (IsKeyDown(KEY_ENTER))
            {
                if (fs::exists(rename_folder_name)) {
                    fs::path newFolderPath = rename_folder_name.parent_path() / rename_folder_buffer;
                    try {
                        if (fs::exists(newFolderPath)) {
                            std::cerr << "Target directory already exists: " << newFolderPath.string() << std::endl;
                        } else {
                            fs::rename(rename_folder_name, newFolderPath);
                            std::cout << "Folder renamed successfully to " << newFolderPath.string() << std::endl;
                        }
                    } catch (const std::filesystem::filesystem_error& e) {
                        std::cerr << "Error renaming folder: " << e.what() << std::endl;
                    }
                } else {
                    std::cout << "Folder not found: " << rename_folder_name.string() << std::endl;
                }
                folders_texture_struct[i].rename = false;
                rename_folder_index = -1;
            }
        }
        else
        {
            ImGui::SetCursorPosX(centerPosX);
            ImGui::Text(folders_texture_struct[i].name.c_str());
        }



        ImGui::PopID();
        ImGui::NextColumn();
    }

    // FILES List
    int numFileButtons = files_texture_struct.size();

    if (numFileButtons <= 0)
    {
        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
        {
            showAddFilePopup = true;
        }
    }

    for (int i = 0; i < numFileButtons; i++)
    {
        ImGui::PushID(i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&files_texture_struct[i].texture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor();
    
        float textWidth = ImGui::CalcTextSize(files_texture_struct[i].name.c_str()).x;
        float buttonWidth = thumbnailSize;
        float offset = (buttonWidth - textWidth) * 0.5f;

        float centerPosX = (ImGui::GetCursorPosX() + offset);
        
        bool isButtonHovered = ImGui::IsItemHovered(); // Check if the button is hovered

        string file_extension = getFileExtension(files_texture_struct[i].path.filename().string());

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            const char* drag_type;

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
            else
                drag_type = "UNSUPORTED TYPE";
            
            ImGui::SetDragDropPayload(drag_type, &i, sizeof(int));
            ImGui::Image((void *)(intptr_t)(ImTextureID)&files_texture_struct[i].texture, ImVec2(64, 64));
            ImGui::EndDragDropSource();
        }


        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && isButtonHovered && !isFolderHovered)
        {
            file_index = i;
            showEditFilePopup = true;
        }

        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && !isButtonHovered && !isFolderHovered)
        {
            showAddFilePopup = true;
        }

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

        if (rename_file_index == i)
            files_texture_struct[i].rename = true;

        if (files_texture_struct[i].rename)
        {
            ImGui::InputText("##RenameFile", (char*)rename_file_buffer, 256);

            if (IsKeyDown(KEY_ENTER))
            {
                if (fs::exists(rename_file_name)) {
                    fs::path newFilePath = files_texture_struct[i].full_path.parent_path() / rename_file_buffer;
                    fs::rename(rename_file_name, newFilePath);
                    std::cout << "File renamed successfully to " << newFilePath.string() << std::endl;
                } else {
                    std::cout << "File not found: " << rename_file_name.string() << std::endl;
                }
                files_texture_struct[i].rename = false;
                rename_file_index = -1;
            }
        }
        else
        {
            ImGui::SetCursorPosX(centerPosX);
            ImGui::Text(files_texture_struct[i].name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    AddFileManipulation();
    EditFolderManipulation();
    EditFileManipulation();

    ImGui::PopStyleVar(3);
    ImGui::EndChild();
    ImGui::End();
}