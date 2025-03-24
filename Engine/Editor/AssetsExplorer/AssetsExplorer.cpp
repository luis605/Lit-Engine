#include "AssetsExplorer.hpp"
#include "file_manipulation.hpp"
#include <Engine/Lighting/skybox.hpp>
#include <extras/IconsFontAwesome6.h>

Texture2D folderTexture;
Texture2D imageTexture;
Texture2D cppTexture;
Texture2D emptyTexture;
Texture2D pythonTexture;
Texture2D modelTexture;
Texture2D materialTexture;
RenderTexture2D modelPreviewRT;

std::unordered_map<fs::path, Texture2D> modelsIcons;
std::unordered_map<fs::path, Texture2D> textureCache;
std::unordered_map<fs::path, fs::file_time_type> directoriesLastModify;

fs::path dirPath = "project/game";
float padding = 10.0f;
float thumbnailSize = 64.0f;
float cellSize = thumbnailSize + padding;

ImVec2 assetsExplorerWindowSize = {cellSize, cellSize};
LitCamera modelPreviewerCamera = {0};
SurfaceMaterial assetsMaterial;

std::vector<FolderTextureItem> folderStruct;
std::vector<FileTextureItem> fileStruct;

// FILE MANIPULATION
fs::path renameFolderName;
bool showAddFilePopup = false;
bool showEditFilePopup = false;
bool showEditFolderPopup = false;
char renameFileBuffer[256];
char renameFolderBuffer[256];
int renameFileIndex = -1;
int renameFolderIndex = -1;
int fileIndex = -1;
int folderIndex = -1;

std::string generateNumberedFileName(const fs::path& directoryPath,
                                     const std::string& extension) {
    int fileNumber = 1;
    fs::path filePath;

    do {
        std::string fileName =
            "file" + std::to_string(fileNumber) + "." + extension;
        filePath = directoryPath / fileName;
        fileNumber++;
    } while (fs::exists(filePath));

    return filePath.string();
}

bool createNumberedFile(const fs::path& directoryPath,
                        const std::string& extension) {
    std::string filePathString =
        generateNumberedFileName(directoryPath, extension);

    std::ofstream outputFile(filePathString);

    if (outputFile.is_open()) {
        outputFile.close();
        return true;
    } else {
        TraceLog(LOG_ERROR, (std::string("Failed to create the file: ") +
                             std::string(filePathString))
                                .c_str());
        return false;
    }
}

bool createNumberedFolder(const fs::path& directoryPath) {
    int folderNumber = 1;

    while (true) {
        fs::path folderPath = directoryPath / std::to_string(folderNumber);

        try {
            if (!fs::exists(folderPath)) {
                fs::create_directory(folderPath);
                return true; // Indicate success
            } else {
                ++folderNumber;
            }
        } catch (const fs::filesystem_error& e) {
            TraceLog(LOG_ERROR, (std::string("Failed to create directory: ") +
                                 std::string(e.what()))
                                    .c_str());
            return false; // Indicate failure to create the directory
        }
    }
}

void EditFolderManipulation() {
    if (ImGui::IsWindowHovered() && showEditFolderPopup)
        ImGui::OpenPopup("edit_folder_popup");

    if (ImGui::BeginPopup("edit_folder_popup")) {
        if (!ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showEditFolderPopup = false;

        ImGui::Text("Edit Folder");

        ImGui::Separator();

        const float buttonWidth = ImGui::GetContentRegionAvail().x;

        if (ImGui::Button("Rename", ImVec2(buttonWidth, 0))) {
            if (folderIndex != -1) {
                fs::path currentPath = fs::current_path();

                renameFolderIndex = folderIndex;

                size_t bufferSize = sizeof(renameFolderBuffer);
                const char* source =
                    folderStruct[folderIndex].name.string().c_str();

                strncpy(renameFolderBuffer, source, bufferSize - 1);
                renameFolderBuffer[bufferSize - 1] = '\0';

                folderIndex = -1;
                showEditFolderPopup = false;
            }
        }

        if (ImGui::Button("Delete", ImVec2(buttonWidth, 0))) {
            if (folderIndex != -1) {
                fs::path folderPath =
                    (fs::current_path() / folderStruct[folderIndex].full_path);
                fs::remove_all(folderPath);

                folderIndex = -1;
                showEditFolderPopup = false;
            }
        }

        ImGui::EndPopup();
    }
}

void EditFileManipulation() {
    if (ImGui::IsWindowHovered() && showEditFilePopup)
        ImGui::OpenPopup("edit_file_popup");

    if (ImGui::BeginPopup("edit_file_popup")) {
        if (!ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showEditFilePopup = false;

        ImGui::Text("Edit File");

        ImGui::Separator();

        const float buttonWidth = ImGui::GetContentRegionAvail().x;

        if (ImGui::Button("Rename", ImVec2(buttonWidth, 0))) {
            if (fileIndex != -1) {
                fs::path currentPath = fs::current_path();

                renameFileIndex = fileIndex;

                size_t bufferSize = sizeof(renameFileBuffer);
                const char* source =
                    fileStruct[fileIndex].name.string().c_str();

                strncpy(renameFileBuffer, source, bufferSize - 1);
                renameFileBuffer[bufferSize - 1] = '\0';

                fileIndex = -1;
                showEditFilePopup = false;
            }
        }

        if (ImGui::Button("Delete", ImVec2(buttonWidth, 0))) {
            if (fileIndex != -1) {
                fs::path currentPath = fs::current_path();

                fs::path filePath =
                    currentPath / dirPath / fileStruct[fileIndex].name;
                fs::remove_all(filePath);

                fileIndex = -1;
                showEditFilePopup = false;
            }
        }

        if (ImGui::Button("Run", ImVec2(buttonWidth, 0))) {
            entitiesList.assign(entitiesListPregame.begin(),
                                entitiesListPregame.end());

            Entity runScriptEntity = Entity();
            runScriptEntity.script = fileStruct[fileIndex].full_path.string();
            runScriptEntity.scriptIndex = "script not defined";
            runScriptEntity.calcPhysics = true;

            LitCamera* sceneCamera_reference = &sceneCamera;
            runScriptEntity.setupScript(sceneCamera_reference);
            runScriptEntity.runScript(sceneCamera_reference);

            fileIndex = -1;
            showEditFilePopup = false;
        }

        ImGui::EndPopup();
    }
}

void AddFileManipulation() {
    if (ImGui::IsWindowHovered() && showAddFilePopup)
        ImGui::OpenPopup("popup");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

    if (ImGui::BeginPopup("popup", ImGuiWindowFlags_NoTitleBar)) {
        if (!ImGui::IsItemHovered() && !ImGui::IsItemHovered() &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showAddFilePopup = false;

        ImGui::Text("Add");
        ImGui::Separator();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Python")) {
                createNumberedFile(dirPath, "py");
                showAddFilePopup = false;
            }

            if (ImGui::MenuItem("Material")) {
                createNumberedFile(dirPath, "mat");
                showAddFilePopup = false;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Folder")) {
            if (ImGui::MenuItem("Folder")) {
                createNumberedFolder(dirPath);
                showAddFilePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
}
// END FILE MANIPULATION

void InitRenderModelPreviewer() {
    modelPreviewerCamera.position = (Vector3){10.0f, 10.0f, 2.0f};
    modelPreviewerCamera.target = (Vector3){0.0f, 0.0f, 0.0f};
    modelPreviewerCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
    modelPreviewerCamera.fovy = 60.0f;
    modelPreviewerCamera.projection = CAMERA_PERSPECTIVE;

    LightStruct lightStructA;
    LightStruct lightStructB;

    lightStructA.light.type = LIGHT_POINT;
    lightStructA.light.position = {11.0f, 6.0f, 15.0f};
    lightStructA.light.color = {1.0f, 0.75f, 0.75f, 1.0f};
    lightStructA.light.aisr.y = 10.0f;

    lightStructB.light.type = LIGHT_POINT;
    lightStructB.light.position = {20.0f, 10.0f, -25.0f};
    lightStructB.light.color = {1.0f, 0.16f, 0.16f, 1.0f};
    lightStructB.light.aisr.y = 6.0f;

    renderModelPreviewerLights.push_back(lightStructA);
    renderModelPreviewerLights.push_back(lightStructB);

    modelPreviewRT = LoadRenderTexture(thumbnailSize, thumbnailSize);
}

Texture2D RenderModelPreview(const char* modelFile) {
    Model model = LoadModel(modelFile);
    model.materials[0].shader = shader;
    if (!IsModelReady(model)) {
        TraceLog(LOG_WARNING, "Failed to load model.");
        return {0};
    }

    modelPreviewRT = LoadRenderTexture(thumbnailSize, thumbnailSize);

    BeginTextureMode(modelPreviewRT);
    BeginMode3D(modelPreviewerCamera);
    BeginShaderMode(shader);
    ClearBackground(GRAY);
    skybox.drawSkybox(modelPreviewerCamera);

    DrawModel(model, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
    EndShaderMode();
    EndMode3D();
    EndTextureMode();

    UnloadModel(model);

    return modelPreviewRT.texture;
}

std::unordered_map<std::string, Texture2D&> extensionToTextureMap = {
    {"", emptyTexture},       {".png", imageTexture}, {".jpg", imageTexture},
    {".jpeg", imageTexture},  {".hdr", imageTexture}, {".avi", imageTexture},
    {".mp4", imageTexture},   {".mov", imageTexture}, {".mkv", imageTexture},
    {".webm", imageTexture},  {".gif", imageTexture}, {".cpp", cppTexture},
    {".c++", cppTexture},     {".cxx", cppTexture},   {".hpp", cppTexture},
    {".cc", cppTexture},      {".h", cppTexture},     {".hh", cppTexture},
    {".hxx", cppTexture},     {".py", pythonTexture}, {".mtl", materialTexture},
    {".mat", materialTexture}};

std::unordered_map<std::string, const char*> dragTypeMap = {
    {".py", "SCRIPT_PAYLOAD"},    {".cpp", "SCRIPT_PAYLOAD"},
    {".c++", "SCRIPT_PAYLOAD"},   {".cxx", "SCRIPT_PAYLOAD"},
    {".hpp", "SCRIPT_PAYLOAD"},   {".cc", "SCRIPT_PAYLOAD"},
    {".h", "SCRIPT_PAYLOAD"},     {".hh", "SCRIPT_PAYLOAD"},
    {".hxx", "SCRIPT_PAYLOAD"},   {".fbx", "MODEL_PAYLOAD"},
    {".obj", "MODEL_PAYLOAD"},    {".glb", "MODEL_PAYLOAD"},
    {".gltf", "MODEL_PAYLOAD"},   {".ply", "MODEL_PAYLOAD"},
    {".mtl", "MODEL_PAYLOAD"},    {".mat", "MATERIAL_PAYLOAD"},
    {".png", "TEXTURE_PAYLOAD"},  {".jpg", "TEXTURE_PAYLOAD"},
    {".jpeg", "TEXTURE_PAYLOAD"}, {".hdr", "TEXTURE_PAYLOAD"},
    {".avi", "TEXTURE_PAYLOAD"},  {".mp4", "TEXTURE_PAYLOAD"},
    {".mov", "TEXTURE_PAYLOAD"},  {".mkv", "TEXTURE_PAYLOAD"},
    {".webm", "TEXTURE_PAYLOAD"}, {".gif", "TEXTURE_PAYLOAD"}};

const char* filesPayloadTypes[] = {"FILE_PAYLOAD", "TEXTURE_PAYLOAD",
                                   "SCRIPT_PAYLOAD", "MODEL_PAYLOAD",
                                   "MATERIAL_PAYLOAD"};

const char* getDragType(const std::string& fileExtension) {
    auto it = dragTypeMap.find(fileExtension);
    if (it != dragTypeMap.end())
        return it->second;
    return "FILE_PAYLOAD";
}

Texture2D& getTextureForFileExtension(const std::string& fileExtension) {
    auto it = extensionToTextureMap.find(fileExtension);
    if (it != extensionToTextureMap.end())
        return it->second;
    return emptyTexture;
}

FileTextureItem createFileTextureItem(const fs::path& file,
                                      const fs::path& entryPath,
                                      const std::string& fileExtension) {
    if (fileExtension == ".fbx" || fileExtension == ".obj" ||
        fileExtension == ".gltf" || fileExtension == ".ply") {
        auto iter = modelsIcons.find(entryPath);
        if (iter != modelsIcons.end()) {
            return {file, iter->second, file, fileExtension};
        } else {
            Texture2D icon = RenderModelPreview(entryPath.string().c_str());
            if (!IsTextureReady(icon)) {
                modelsIcons[entryPath] = emptyTexture;
                return {file, emptyTexture, file, entryPath, fileExtension};
            }
            BeginTextureMode(modelPreviewRT);
            DrawTextureEx(icon, {0, 0}, 0.0f, 1.0f, RAYWHITE);
            EndTextureMode();

            Texture2D flippedIcon = modelPreviewRT.texture;
            modelsIcons[entryPath] = flippedIcon;
            return {file, flippedIcon, file, entryPath, fileExtension};
        }
    }

    return {file, getTextureForFileExtension(fileExtension), file, entryPath,
            fileExtension};
}

bool isDirectoryModified(const fs::path& path) {
    auto result = directoriesLastModify.find(path);
    fs::file_time_type currentTime = fs::last_write_time(path);

    if (result != directoriesLastModify.end()) {
        if (result->second == currentTime) {
            return false;
        }
        result->second = currentTime;
        return true;
    } else {
        directoriesLastModify[path] = currentTime;
        return true;
    }
}

void UpdateFileFolderStructures() {
    if (dirPath.empty() || !fs::exists(dirPath))
        return;

    glUseProgram(shader.id);
    glUniform1i(glGetUniformLocation(shader.id, "normalMapReady"), false);
    glUniform1i(glGetUniformLocation(shader.id, "roughnessMapReady"), false);

    static bool lightsUpdated = false;
    if (!lightsUpdated) {
        UpdateLightsBuffer(true, renderModelPreviewerLights,
                           renderPrevierLightsBuffer);
        lightsUpdated = true;
    }

    if (isDirectoryModified(dirPath)) {
        folderStruct.clear();
        fileStruct.clear();

        for (const fs::directory_entry& entry :
             fs::directory_iterator(dirPath)) {
            const fs::path& entryPath = entry.path();
            const fs::path& file = entryPath.filename();

            if (entry.is_directory()) {
                folderStruct.emplace_back(file, folderTexture, entryPath);
            } else if (entry.is_regular_file()) {
                fileStruct.emplace_back(createFileTextureItem(
                    file, entryPath, entryPath.extension().string()));
            }
        }
    }
}

void AssetsExplorerTopBar() {
    if (dirPath == "project")
        return;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    if (ImGui::Button("<--")) {
        dirPath = dirPath.parent_path();
        auto it = directoriesLastModify.find(dirPath);
        if (it != directoriesLastModify.end())
            directoriesLastModify.erase(it);
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = nullptr;

        for (const char* payload_type : filesPayloadTypes) {
            payload = ImGui::AcceptDragDropPayload(payload_type);
            if (payload)
                break;
        }

        if (payload && payload->DataSize == sizeof(int)) {
            int payload_n = *(const int*)payload->Data;
            fs::path sourceFilePath = dirPath / fileStruct[payload_n].name;
            fs::path destinationFilePath =
                dirPath.parent_path() / fileStruct[payload_n].name;

            if (!fs::is_regular_file(destinationFilePath)) {
                try {
                    fs::rename(sourceFilePath, destinationFilePath);
                } catch (const fs::filesystem_error& e) {
                    TraceLog(LOG_ERROR,
                             ("Failed to rename file: " + std::string(e.what()))
                                 .c_str());
                }
            } else {
                TraceLog(LOG_ERROR,
                         "Failed to move file, because file already exists.");
            }
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::PopStyleColor(3);
    ImGui::SameLine();
}

void AssetsExplorer() {
    auto assetsExplorer_start = std::chrono::high_resolution_clock::now();

    if (assetsExplorerWindowSize.x < cellSize) {
        ImGui::SetNextWindowSize({cellSize, assetsExplorerWindowSize.y});
    } else if (assetsExplorerWindowSize.y < cellSize) {
        ImGui::SetNextWindowSize({assetsExplorerWindowSize.y, cellSize});
    }

    static const std::string assetsExplorerName =
        (std::string(ICON_FA_FOLDER_OPEN) + " Assets Explorer");
    static const char* assetsExplorerName_cstr = assetsExplorerName.c_str();
    ImGui::Begin(assetsExplorerName_cstr, nullptr);

    UpdateFileFolderStructures();
    AssetsExplorerTopBar();

    ImGui::BeginDisabled();
    static std::string assetsExplorerPathText =
        (std::string(dirPath.string()) + "##AssetsExplorerPath");
    static const char* assetsExplorerPathText_cstr =
        assetsExplorerPathText.c_str();
    ImGui::Button(assetsExplorerPathText_cstr);
    ImGui::EndDisabled();

    ImGui::BeginChild("Assets Explorer Child", ImVec2(-1, -1), true);
    assetsExplorerWindowSize = ImGui::GetWindowSize();

    bool isFolderHovered = false;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount =
        std::max(1, static_cast<int>(panelWidth / (cellSize + padding)));

    ImGui::Columns(columnCount, 0, false);

    // FOLDERS List
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(cellSize * .12f, cellSize * .12f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4, 0.4, 0.4, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35, 0.35, 0.35, 1));

    float buttonWidth = thumbnailSize;
    float halfButton = buttonWidth / 2.0f;
    int numFolders = folderStruct.size();

    for (size_t index = 0; index < numFolders; index++) {
        FolderTextureItem& folderItem = folderStruct[index];

        ImGui::PushID(index);

        ImGui::ImageButton("", (ImTextureID)&folderTexture,
                           ImVec2(thumbnailSize, thumbnailSize));

        if (renameFolderIndex == index)
            folderItem.rename = true;

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = nullptr;

            for (const char* payload_type : filesPayloadTypes) {
                payload = ImGui::AcceptDragDropPayload(payload_type);
                if (payload)
                    break;
            }

            if (payload) {
                if (payload->DataSize == sizeof(int)) {
                    int payload_n = *(const int*)payload->Data;

                    fs::path sourceFilePath = fs::current_path() / dirPath /
                                              fileStruct[payload_n].name;
                    fs::path destinationFilePath = fs::current_path() /
                                                   dirPath / folderItem.name /
                                                   fileStruct[payload_n].name;

                    try {
                        fs::rename(sourceFilePath, destinationFilePath);
                    } catch (const fs::filesystem_error& e) {
                        TraceLog(LOG_ERROR,
                                 (std::string("Failed to rename file: ") +
                                  std::string(e.what()))
                                     .c_str());
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }

        if (ImGui::IsItemHovered()) {
            isFolderHovered = true;

            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                dirPath = dirPath / folderItem.name;
                auto it = directoriesLastModify.find(dirPath);
                if (it != directoriesLastModify.end())
                    directoriesLastModify.erase(it);
            } else if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
                folderIndex = index;
                showEditFolderPopup = true;
            }
        }

        if (folderItem.rename) {
            ImGui::InputText("##RenameFolder", (char*)renameFolderBuffer, 256);

            if (IsKeyPressed(KEY_ENTER)) {
                fs::path newFolderPath = dirPath / renameFolderBuffer;

                if (fs::exists(newFolderPath)) {
                    TraceLog(LOG_ERROR, "Directory already exists.");
                } else {
                    try {
                        fs::rename(dirPath / folderItem.name, newFolderPath);
                    } catch (const fs::filesystem_error& e) {
                        TraceLog(LOG_ERROR,
                                 (std::string("Failed to rename directory: ") +
                                  std::string(e.what()))
                                     .c_str());
                    }
                }

                folderItem.rename = false;
                renameFolderIndex = -1;
            }
        } else {
            ImGui::Text("%s", folderItem.name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    // FILES List
    int numFiles = fileStruct.size();

    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) &&
        numFiles <= 0 && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
        showAddFilePopup = true;

    for (int index = 0; index < numFiles; index++) {
        FileTextureItem& fileItem = fileStruct[index];
        ImGui::PushID(numFolders + index);

        ImGui::ImageButton("", (ImTextureID)&fileItem.texture,
                           ImVec2(thumbnailSize, thumbnailSize));

        bool isButtonHovered = ImGui::IsItemHovered();

        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) &&
            ImGui::IsWindowHovered() && !isFolderHovered) {
            if (isButtonHovered) {
                fileIndex = index;
                showEditFilePopup = true;
            } else
                showAddFilePopup = true;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                const char* dragType = getDragType(fileItem.extension);

                ImGui::SetDragDropPayload(dragType, &index, sizeof(int));
                ImGui::Image((void*)(intptr_t)(ImTextureID)&fileItem.texture,
                             ImVec2(64, 64));
                ImGui::EndDragDropSource();
            }
        }

        if (isButtonHovered) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                code = readFileToString((dirPath / fileItem.name).string());
                codeEditorScriptPath = (dirPath / fileItem.name).string();
                editor.SetText(code);
            }
        }

        if (renameFileIndex == index)
            fileItem.rename = true;

        if (fileItem.rename) {
            ImGui::InputText("##RenameFile", (char*)renameFileBuffer, 256);

            if (IsKeyDown(KEY_ENTER)) {
                fs::path newFilename = dirPath / renameFileBuffer;

                if (fs::exists(newFilename)) {
                    TraceLog(LOG_ERROR, "File already exists.");
                } else {
                    try {
                        fs::rename(dirPath / fileItem.name, newFilename);
                    } catch (const fs::filesystem_error& e) {
                        TraceLog(LOG_ERROR,
                                 (std::string("Failed to rename file: ") +
                                  std::string(e.what()))
                                     .c_str());
                    }
                }

                fileItem.rename = false;
                renameFileIndex = -1;
            }
        } else {
            ImGui::Text("%s", fileItem.name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    AddFileManipulation();
    EditFolderManipulation();
    EditFileManipulation();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::EndChild();
    ImGui::End();

    std::chrono::high_resolution_clock::time_point assetsExplorer_end =
        std::chrono::high_resolution_clock::now();
    assetsExplorerProfilerDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            assetsExplorer_end - assetsExplorer_start);
}