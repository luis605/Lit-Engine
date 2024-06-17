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

std::unordered_map<std::string, Texture2D&> extensionToTextureMap = {
    {"", emptyTexture},
    {".png", imageTexture},
    {".jpg", imageTexture},
    {".jpeg", imageTexture},
    {".hdr", imageTexture},
    {".avi", imageTexture},
    {".mp4", imageTexture},
    {".mov", imageTexture},
    {".mkv", imageTexture},
    {".webm", imageTexture},
    {".gif", imageTexture},
    {".cpp", cppTexture},
    {".c++", cppTexture},
    {".cxx", cppTexture},
    {".hpp", cppTexture},
    {".cc", cppTexture},
    {".h", cppTexture},
    {".hh", cppTexture},
    {".hxx", cppTexture},
    {".py", pythonTexture},
    {".mtl", materialTexture},
    {".mat", materialTexture}
};

std::unordered_map<std::string, const char*> dragTypeMap = {
    {".py",  "SCRIPT_PAYLOAD"},
    {".cpp", "SCRIPT_PAYLOAD"},
    {".c++", "SCRIPT_PAYLOAD"},
    {".cxx", "SCRIPT_PAYLOAD"},
    {".hpp", "SCRIPT_PAYLOAD"},
    {".cc",  "SCRIPT_PAYLOAD"},
    {".h",   "SCRIPT_PAYLOAD"},
    {".hh",  "SCRIPT_PAYLOAD"},
    {".hxx", "SCRIPT_PAYLOAD"},
    {".fbx", "MODEL_PAYLOAD"},
    {".obj", "MODEL_PAYLOAD"},
    {".gltf", "MODEL_PAYLOAD"},
    {".ply", "MODEL_PAYLOAD"},
    {".mtl", "MODEL_PAYLOAD"},
    {".mat", "MATERIAL_PAYLOAD"},
};

const char* filesPayloadTypes[] = { "FILE_PAYLOAD", "TEXTURE_PAYLOAD", "SCRIPT_PAYLOAD", "MODEL_PAYLOAD", "MATERIAL_PAYLOAD" };

const char* getDragType(const std::string& file_extension) {
    auto it = dragTypeMap.find(file_extension);
    if (it != dragTypeMap.end()) {
        return it->second;
    }

    return "FILE_PAYLOAD";
}

Texture2D& getTextureForFileExtension(const std::string& extension) {
    auto it = extensionToTextureMap.find(extension);
    if (it != extensionToTextureMap.end()) return it->second;

    return emptyTexture;
}

FileTextureItem createFileTextureItem(const fs::path& file, const fs::directory_entry& entry) {
    std::string file_extension = getFileExtension(file.filename().string());

    if (file_extension == ".fbx" || file_extension == ".obj" || file_extension == ".gltf" || file_extension == ".ply") {
        auto iter = modelsIcons.find(file.string());
        if (iter != modelsIcons.end()) {
            return {file.string(), iter->second, file};
        } else {
            Texture2D icon = RenderModelPreview(entry.path().string().c_str());
            RenderTexture2D target = LoadRenderTexture(icon.width, icon.height);

            BeginTextureMode(target);
            DrawTextureEx(icon, {0, 0}, 0.0f, 1.0f, RAYWHITE);
            EndTextureMode();

            Texture2D flippedIcon = target.texture;
            modelsIcons[file.string()] = flippedIcon;
            return {file.string(), flippedIcon, file};
        }
    }

    return {file.string(), getTextureForFileExtension(file_extension), file};
}

void UpdateFileFolderStructures() {
    if (dirPath.empty()) return;
    if (!fs::exists(dirPath)) return;

    for (const fs::directory_entry& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file() && !entry.is_directory()) continue;

        fs::path file = entry.path().filename();
        if (fs::is_directory(entry)) {
            folderStruct.emplace_back(file.string(), folderTexture, entry.path());
            folders.emplace_back(file.string());
        } else {
            FileTextureItem fileTextureItem = createFileTextureItem(file, entry);
            fileStruct.emplace_back(std::move(fileTextureItem));
        }
    }
}

void AssetsExplorerTopBar() {
    if (dirPath == "project") return;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    if (ImGui::Button("<--")) dirPath = dirPath.parent_path();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = nullptr;

        for (const char* payload_type : filesPayloadTypes) {
            payload = ImGui::AcceptDragDropPayload(payload_type);
            if (payload) break;
        }

        if (payload) {
            if (payload->DataSize == sizeof(int)) {
                int payload_n = *(const int*)payload->Data;

                fs::path path = dirPath.string() + "/" + fileStruct[payload_n].name;

                fs::path sourceFilePath = fs::current_path() / path;
                fs::path destinationFilePath = dirPath.parent_path() / fileStruct[payload_n].name;

                if (fs::is_regular_file(destinationFilePath)) {
                    std::cerr << "Error: Cannot move a file to a folder where a file with the same name already exists." << std::endl;
                } else {
                    try {
                        fs::rename(sourceFilePath, destinationFilePath);
                    } catch (const fs::filesystem_error& e) {
                        std::cerr << "Error renaming file: " << e.what() << std::endl;
                    }
                }
            }
        }

        ImGui::EndDragDropTarget();
    }
    
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
}

void AssetsExplorer() {
    std::chrono::high_resolution_clock::time_point assetsExplorer_start = std::chrono::high_resolution_clock::now();

    folderStruct.clear();
    fileStruct.clear();
    folders.clear();
    files.clear();

    if (assetsExplorerWindowSize.x < cellSize)        ImGui::SetNextWindowSize({cellSize, assetsExplorerWindowSize.y});
    else if (assetsExplorerWindowSize.y < cellSize)   ImGui::SetNextWindowSize({assetsExplorerWindowSize.y, cellSize});

    ImGui::Begin((std::string(ICON_FA_FOLDER_OPEN) + " Assets Explorer").c_str(), NULL);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5);

    UpdateFileFolderStructures();
    AssetsExplorerTopBar();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

        ImGui::Button((std::string(dirPath.string()) + "##AssetsExplorerPath").c_str());

    ImGui::PopStyleColor(3);

    ImGui::BeginChild("Assets Explorer Child", ImVec2(-1,-1), true);
    assetsExplorerWindowSize = ImGui::GetWindowSize();

    bool isFolderHovered = false;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    float columnWidth = cellSize + 40.0f;

    int numFolders = folderStruct.size();
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    ImGui::Columns(columnCount, "##AssetsExplorerListColumns", false);

    for (int i = 0; i < columnCount; ++i) {
        if (columnCount > 1)
            ImGui::SetColumnWidth(i, columnWidth);
    }

    // FOLDERS List
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(cellSize / 8, cellSize / 8));

    float buttonWidth = thumbnailSize;
    float halfButton = buttonWidth / 2.0f;

    for (int index = 0; index < numFolders; index++) {
        ImGui::PushID(index);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&folderTexture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor(); 
        
        bool isButtonHovered = ImGui::IsItemHovered();

        if (isButtonHovered) isFolderHovered = true;
        if (renameFolderIndex == index) folderStruct[index].rename = true;
        if (isButtonHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            dirPath += "/" + folderStruct[index].name;

        if (ImGui::IsWindowHovered() && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && isButtonHovered) {
            folderIndex = index;
            showEditFolderPopup = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = nullptr;

            for (const char* payload_type : filesPayloadTypes) {
                payload = ImGui::AcceptDragDropPayload(payload_type);
                if (payload) break;
            }

            if (payload) {
                if (payload->DataSize == sizeof(int)) {
                    int payload_n = *(const int*)payload->Data;

                    fs::path path = dirPath.string() + "/" + fileStruct[payload_n].name;
                    
                    fs::path sourceFilePath = fs::current_path() / path;
                    fs::path destinationFilePath = fs::current_path() / dirPath.string() / folderStruct[index].name / fileStruct[payload_n].name;

                    try {
                        fs::rename(sourceFilePath, destinationFilePath);
                    } catch (const fs::filesystem_error& e) {
                        std::cerr << "Error renaming file: " << e.what() << std::endl;
                    }

                }
            }
 
            ImGui::EndDragDropTarget();
        }

        if (folderStruct[index].rename) {
            ImGui::InputText("##RenameFolder", (char*)renameFolderBuffer, 256);

            if (IsKeyPressed(KEY_ENTER)) {
                fs::path newFolderPath = renameFolderName.parent_path() / renameFolderBuffer;

                if (fs::exists(newFolderPath)) {
                    std::cerr << "Error: Directory already exists!" << std::endl;
                } else {
                    try {
                        fs::rename(renameFolderName, newFolderPath);
                    } catch (const fs::filesystem_error& e) {
                        std::cerr << "Error renaming folder: " << e.what() << std::endl;
                    }
                }

                folderStruct[index].rename = false;
                renameFolderIndex = -1;
            }
        } else {
            float textWidth = ImGui::CalcTextSize(folderStruct[index].name.c_str()).x;
            float buttonWidth = thumbnailSize;
            float offset = (buttonWidth - textWidth / 2) / 2;
            float centerPosX = (ImGui::GetCursorPosX() + offset);

            ImGui::SetCursorPosX(centerPosX);
            ImGui::Text(folderStruct[index].name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    // FILES List
    int numFileButtons = fileStruct.size();

    if (numFileButtons <= 0) {
        if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
            showAddFilePopup = true;
    }

    for (int index = 0; index < numFileButtons; index++) {
        ImGui::PushID(index);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
        ImGui::ImageButton((ImTextureID)&fileStruct[index].texture, ImVec2(thumbnailSize, thumbnailSize));
        ImGui::PopStyleColor();
    
        bool isButtonHovered = ImGui::IsItemHovered();

        std::string file_extension = getFileExtension(fileStruct[index].path.filename().string());

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            const char* drag_type = getDragType(file_extension);

            ImGui::SetDragDropPayload(drag_type, &index, sizeof(int));
            ImGui::Image((void *)(intptr_t)(ImTextureID)&fileStruct[index].texture, ImVec2(64, 64));
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsWindowHovered() && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && isButtonHovered && !isFolderHovered) {
            fileIndex = index;
            showEditFilePopup = true;
        } else if (ImGui::IsWindowHovered() && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && !isButtonHovered && !isFolderHovered)
            showAddFilePopup = true;

        if (file_extension == ".mat" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            selectedGameObjectType = "material";
            selectedMaterial = fileStruct[index].full_path;
        } else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            code = readFileToString((dirPath / fileStruct[index].name).string());
            codeEditorScriptPath = (dirPath / fileStruct[index].name).string();

            editor.SetText(code);
        }

        if (renameFileIndex == index) fileStruct[index].rename = true;

        if (fileStruct[index].rename) {
            ImGui::InputText("##RenameFile", (char*)renameFileBuffer, 256);

            if (IsKeyDown(KEY_ENTER)) {
                fs::path newFilename = dirPath / renameFileBuffer;

                if (fs::exists(newFilename)) {
                    std::cerr << "Error: Directory already exists!" << std::endl;
                } else {
                    try {
                        fs::rename(dirPath / fileStruct[index].name, newFilename);
                    } catch (const fs::filesystem_error& e) {
                        std::cerr << "Error renaming file: " << e.what() << std::endl;
                    }
                }

                fileStruct[index].rename = false;
                renameFileIndex = -1;
            }
        } else {
            float textWidth = ImGui::CalcTextSize(fileStruct[index].name.c_str()).x;
            float buttonWidth = thumbnailSize;
            float offset = (buttonWidth - textWidth / 2) / 2;
            float centerPosX = (ImGui::GetCursorPosX() + offset);

            ImGui::SetCursorPosX(centerPosX);
            ImGui::Text(fileStruct[index].name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    AddFileManipulation();
    EditFolderManipulation();
    EditFileManipulation();

    ImGui::PopStyleVar(4);
    ImGui::EndChild();
    ImGui::End();

    std::chrono::high_resolution_clock::time_point assetsExplorer_end = std::chrono::high_resolution_clock::now();
    assetsExplorerProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(assetsExplorer_end - assetsExplorer_start);
}