void AssetsExplorer(string code);

#include "../../include_all.h"

void InitRenderModelPreviewer() {
    modelPreviewerCamera.position = (Vector3){ 10.0f, 10.0f, 2.0f };
    modelPreviewerCamera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    modelPreviewerCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    modelPreviewerCamera.fovy = 60.0f;
    modelPreviewerCamera.projection = CAMERA_PERSPECTIVE;

    target = LoadRenderTexture(thumbnailSize, thumbnailSize);
}

Texture2D RenderModelPreview(const char* modelFile) {
    Model model = LoadModel(modelFile);

    BeginTextureMode(target);
    ClearBackground(GRAY);

    BeginMode3D(modelPreviewerCamera);
    DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
    EndMode3D();

    EndTextureMode();
    UnloadModel(model);

    return target.texture;
}

void AssetsExplorer() {
    std::chrono::high_resolution_clock::time_point assetsExplorer_start = std::chrono::high_resolution_clock::now();

    foldersTextureStruct.clear();
    filesTextureStruct.clear();
    folders.clear();
    files.clear();

    if (assetsExplorerWindowSize.x < cellSize)
        ImGui::SetNextWindowSize({cellSize, assetsExplorerWindowSize.y});
    else if (assetsExplorerWindowSize.y < cellSize)
        ImGui::SetNextWindowSize({assetsExplorerWindowSize.y, cellSize});

    ImGui::Begin((std::string(ICON_FA_FOLDER_OPEN) + " Assets Explorer").c_str(), NULL);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5);

    if (!dirPath.empty()) {
        fs::path dirPath = dirPath;
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
                    foldersTextureStruct.emplace_back(file.string(), folderTexture, entry);
                    folders.emplace_back(file.string());
                } else {
                    string file_extension = getFileExtension(file.filename().string());
                    FileTextureItem fileTextureItem;

                    if (file_extension == "no file extension") {
                        fileTextureItem = {file.string(), emptyTexture, file, entry};
                    } else if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".hdr" || file_extension == ".jpeg" ||
                            file_extension == ".avi" || file_extension == ".mp4" || file_extension == ".mov" ||
                            file_extension == ".mkv" || file_extension == ".webm" || file_extension == ".gif") {
                        fileTextureItem = {file.string(), imageTexture, file, entry};
                    } else if (file_extension == ".cpp" || file_extension == ".c++" || file_extension == ".cxx" ||
                            file_extension == ".hpp" || file_extension == ".cc" || file_extension == ".h" ||
                            file_extension == ".hh" || file_extension == ".hxx") {
                        fileTextureItem = {file.string(), cppTexture, file, entry};
                    } else if (file_extension == ".py") {
                        fileTextureItem = {file.string(), pythonTexture, file, entry};
                    } else if (file_extension == ".mtl" || file_extension == ".mat") {
                        fileTextureItem = {file.string(), materialTexture, file, entry};
                    } else if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" ||
                            file_extension == ".ply") {
                        fileTextureItem = {file.string(), modelTexture, file, entry};

                        auto iter = modelsIcons.find(file.string());

                        if (iter != modelsIcons.end()) {
                            fileTextureItem = {file.string(), iter->second, file, entry};
                        } else {
                            Texture2D icon = RenderModelPreview(entry.path().string().c_str());
                            RenderTexture2D target = LoadRenderTexture(icon.width, icon.height);

                            BeginTextureMode(target);
                            DrawTextureEx(icon, (Vector2){0, 0}, 0.0f, 1.0f, RAYWHITE);
                            EndTextureMode();

                            Texture2D flippedIcon = target.texture;
                            modelsIcons[file.string()] = flippedIcon;
                            fileTextureItem = {file.string(), flippedIcon, file, entry};
                        }
                    } else
                        fileTextureItem = {file.string(), emptyTexture, file, entry};

                    filesTextureStruct.emplace_back(std::move(fileTextureItem));
                }
            }
        }
    }
    else cout << "Error: " << strerror(errno) << endl;

    if (dirPath != "project")
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

        if (ImGui::Button("<--")) dirPath = dirPath.parent_path();

        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");
            if (payload || (payload = ImGui::AcceptDragDropPayload("SCRIPT_PAYLOAD"))
                || (payload = ImGui::AcceptDragDropPayload("MODEL_PAYLOAD"))
                || (payload = ImGui::AcceptDragDropPayload("MATERIAL_PAYLOAD"))
                || (payload = ImGui::AcceptDragDropPayload("UNSUPPORTED_TYPE")))
            {
                if (payload)
                {
                    if (payload->DataSize == sizeof(int))
                    {
                        int payload_n = *(const int*)payload->Data;

                        std::filesystem::path currentPath = std::filesystem::current_path();

                        std::string path = dirPath.string();
                        path += "/" + filesTextureStruct[payload_n].name;

                        fs::path sourceFilePath = currentPath / path;
                        fs::path destinationFilePath = dirPath.parent_path() / filesTextureStruct[payload_n].name;

                        fs::rename(sourceFilePath, destinationFilePath);
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }
        
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

    ImGui::Button((std::string(dirPath.string()) + "##AssetsExplorerPath").c_str());

    ImGui::PopStyleColor(3);

    ImGui::BeginChild("Assets Explorer Child", ImVec2(-1,-1), true);

    int numButtons = foldersTextureStruct.size();

    // Collumns
    float panelWidth = ImGui::GetContentRegionAvail().x;
    float columnWidth = cellSize + 40.0f;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    ImGui::Columns(columnCount, "##AssetsExplorerListColumns", false);

    for (int i = 0; i < columnCount; ++i) {
        if (columnCount > 1)
            ImGui::SetColumnWidth(i, columnWidth);
    }

    assetsExplorerWindowSize = ImGui::GetWindowSize();
    bool isFolderHovered = false;

    // FOLDERS List
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    for (int i = 0; i < numButtons; i++)
    {
        ImGui::PushID(i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&folderTexture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor(); 

        bool isButtonHovered = ImGui::IsItemHovered();
        
        if (isButtonHovered) isFolderHovered = true;

        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && isButtonHovered)
        {
            folderIndex = i;
            showEditFolderPopup = true;
        }

        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");
            if (payload || (payload = ImGui::AcceptDragDropPayload("SCRIPT_PAYLOAD"))
                || (payload = ImGui::AcceptDragDropPayload("MODEL_PAYLOAD"))
                || (payload = ImGui::AcceptDragDropPayload("MATERIAL_PAYLOAD"))
                || (payload = ImGui::AcceptDragDropPayload("UNSUPPORTED_TYPE")))
            {
                if (payload)
                {
                    if (payload->DataSize == sizeof(int))
                    {
                        int payload_n = *(const int*)payload->Data;

                        std::filesystem::path currentPath = std::filesystem::current_path();

                        std::string path = dirPath.string();
                        path += "/" + filesTextureStruct[payload_n].name;

                        fs::path sourceFilePath = currentPath / path;
                        fs::path destinationFilePath = currentPath / foldersTextureStruct[i].full_path / filesTextureStruct[payload_n].name;

                        fs::rename(sourceFilePath, destinationFilePath);
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }

        float textWidth = ImGui::CalcTextSize(foldersTextureStruct[i].name.c_str()).x;
        float buttonWidth = thumbnailSize;
        float offset = (buttonWidth - textWidth) * 0.5f;

        float centerPosX = (ImGui::GetCursorPosX() + offset);

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            dirPath += "/" + foldersTextureStruct[i].name;

        if (renameFolderIndex == i)
            foldersTextureStruct[i].rename = true;
            
        if (foldersTextureStruct[i].rename)
        {
            ImGui::InputText("##RenameFolder", (char*)renameFolderBuffer, 256);

            if (IsKeyDown(KEY_ENTER))
            {
                if (fs::exists(renameFolderName)) {
                    fs::path newFolderPath = renameFolderName.parent_path() / renameFolderBuffer;
                    try {
                        if (fs::exists(newFolderPath)) {
                            std::cerr << "Target directory already exists: " << newFolderPath.string() << std::endl;
                        } else {
                            fs::rename(renameFolderName, newFolderPath);
                            std::cout << "Folder renamed successfully to " << newFolderPath.string() << std::endl;
                        }
                    } catch (const std::filesystem::filesystem_error& e) {
                        std::cerr << "Error renaming folder: " << e.what() << std::endl;
                    }
                } else {
                    std::cout << "Folder not found: " << renameFolderName.string() << std::endl;
                }
                foldersTextureStruct[i].rename = false;
                renameFolderIndex = -1;
            }
        }
        else
        {
            ImGui::SetCursorPosX(centerPosX);
            ImGui::Text(foldersTextureStruct[i].name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    // FILES List
    int numFileButtons = filesTextureStruct.size();

    if (numFileButtons <= 0)
    {
        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
            showAddFilePopup = true;
    }

    for (int i = 0; i < numFileButtons; i++)
    {
        ImGui::PushID(i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&filesTextureStruct[i].texture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor();
    
        float textWidth = ImGui::CalcTextSize(filesTextureStruct[i].name.c_str()).x;
        float buttonWidth = thumbnailSize;
        float offset = (buttonWidth - textWidth) * 0.5f;
        float centerPosX = (ImGui::GetCursorPosX() + offset);
        bool isButtonHovered = ImGui::IsItemHovered();

        string file_extension = getFileExtension(filesTextureStruct[i].path.filename().string());

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            const char* drag_type;

            if (file_extension == ".png" || file_extension == ".jpg" || file_extension == ".hdr" || file_extension == ".jpeg" ||
                file_extension == ".avi" || file_extension == ".mp4" || file_extension == ".mov" ||
                file_extension == ".mkv" || file_extension == ".webm" || file_extension == ".gif")
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
            ImGui::Image((void *)(intptr_t)(ImTextureID)&filesTextureStruct[i].texture, ImVec2(64, 64));
            ImGui::EndDragDropSource();
        }

        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && isButtonHovered && !isFolderHovered)
        {
            fileIndex = i;
            showEditFilePopup = true;
        }
        else if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && !isButtonHovered && !isFolderHovered)
        {
            showAddFilePopup = true;
        }

        if (file_extension == ".mat" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            selectedGameObjectType = "material";
            selectedMaterial = filesTextureStruct[i].full_path;
        }
        else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            code = read_file_to_string((dirPath / filesTextureStruct[i].name).string());

            editor.SetText(code);
            codeEditorScriptPath = (dirPath / filesTextureStruct[i].name).string();
        }

        if (renameFileIndex == i)
            filesTextureStruct[i].rename = true;

        if (filesTextureStruct[i].rename)
        {
            ImGui::InputText("##RenameFile", (char*)renameFileBuffer, 256);

            if (IsKeyDown(KEY_ENTER))
            {
                if (fs::exists(renameFileName)) {
                    fs::path newFilePath = filesTextureStruct[i].full_path.parent_path() / renameFileBuffer;
                    fs::rename(renameFileName, newFilePath);
                    std::cout << "File renamed successfully to " << newFilePath.string() << std::endl;
                } else {
                    std::cout << "File not found: " << renameFileName.string() << std::endl;
                }
                filesTextureStruct[i].rename = false;
                renameFileIndex = -1;
            }
        }
        else
        {
            ImGui::SetCursorPosX(centerPosX);
            ImGui::Text(filesTextureStruct[i].name.c_str());
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

    std::chrono::high_resolution_clock::time_point assetsExplorer_end = std::chrono::high_resolution_clock::now();
    assetsExplorerProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(assetsExplorer_end - assetsExplorer_start);
}