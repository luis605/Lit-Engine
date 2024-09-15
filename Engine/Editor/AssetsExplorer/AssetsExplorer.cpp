void InitRenderModelPreviewer() {
    modelPreviewerCamera.position = (Vector3){ 10.0f, 10.0f, 2.0f };
    modelPreviewerCamera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    modelPreviewerCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    modelPreviewerCamera.fovy = 60.0f;
    modelPreviewerCamera.projection = CAMERA_PERSPECTIVE;

    LightStruct lightStructA;
    LightStruct lightStructB;

    lightStructA.light.type = LIGHT_POINT;
    lightStructA.light.position = { 11.0f, 6.0f, 15.0f };
    lightStructA.light.color = { 1.0f, 0.75f, 0.75f, 1.0f };
    lightStructA.light.intensity = 10.0f;

    lightStructB.light.type = LIGHT_POINT;
    lightStructB.light.position = { 20.0f, 10.0f, -25.0f };
    lightStructB.light.color = { 1.0f, 0.16f, 0.16f, 1.0f };
    lightStructB.light.intensity = 6.0f;

    renderModelPreviewerLights.push_back(lightStructA);
    renderModelPreviewerLights.push_back(lightStructB);

    modelPreviewRT = LoadRenderTexture(thumbnailSize, thumbnailSize);

    assetsMaterial.DiffuseIntensity = 1.0f;
}

Texture2D RenderModelPreview(const char* modelFile) {
    Model model = LoadModel(modelFile);
    model.materials[0].shader = shader;
    if (!IsModelReady(model)) {
        TraceLog(LOG_WARNING, "Failed to load model.");
        return { 0 };
    }

    modelPreviewRT = LoadRenderTexture( thumbnailSize, thumbnailSize );

    BeginTextureMode(modelPreviewRT);
        BeginMode3D(modelPreviewerCamera);
        BeginShaderMode(shader);
            ClearBackground(GRAY);
            DrawSkybox();

            DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
        EndShaderMode();
        EndMode3D();
    EndTextureMode();

    UnloadModel(model);

    return modelPreviewRT.texture;
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
    {".glb", "MODEL_PAYLOAD"},
    {".gltf", "MODEL_PAYLOAD"},
    {".ply", "MODEL_PAYLOAD"},
    {".mtl", "MODEL_PAYLOAD"},
    {".mat", "MATERIAL_PAYLOAD"},
    {".png", "TEXTURE_PAYLOAD"},
    {".jpg", "TEXTURE_PAYLOAD"},
    {".jpeg", "TEXTURE_PAYLOAD"},
    {".hdr", "TEXTURE_PAYLOAD"},
    {".avi", "TEXTURE_PAYLOAD"},
    {".mp4", "TEXTURE_PAYLOAD"},
    {".mov", "TEXTURE_PAYLOAD"},
    {".mkv", "TEXTURE_PAYLOAD"},
    {".webm", "TEXTURE_PAYLOAD"},
    {".gif", "TEXTURE_PAYLOAD"}
};

const char* filesPayloadTypes[] = { "FILE_PAYLOAD", "TEXTURE_PAYLOAD", "SCRIPT_PAYLOAD", "MODEL_PAYLOAD", "MATERIAL_PAYLOAD" };

const char* getDragType(const std::string& fileExtension) {
    auto it = dragTypeMap.find(fileExtension);
    if (it != dragTypeMap.end()) return it->second;
    return "FILE_PAYLOAD";
}

Texture2D& getTextureForFileExtension(const std::string& fileExtension) {
    auto it = extensionToTextureMap.find(fileExtension);
    if (it != extensionToTextureMap.end()) return it->second;
    return emptyTexture;
}

FileTextureItem createFileTextureItem(const fs::path& file, const fs::path& entryPath, const std::string& fileExtension) {
    if (fileExtension == ".fbx" || fileExtension == ".obj" || fileExtension == ".gltf" || fileExtension == ".ply") {
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

    return {file, getTextureForFileExtension(fileExtension), file, entryPath, fileExtension};
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
    if (dirPath.empty() || !fs::exists(dirPath)) return;

    static GLuint lastSurfaceMaterialUBO = 0;
    if (surfaceMaterialUBO != lastSurfaceMaterialUBO) {
        if (surfaceMaterialUBO != 0) {
            glDeleteBuffers(1, &surfaceMaterialUBO);
        }

        glGenBuffers(1, &surfaceMaterialUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, surfaceMaterialUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(SurfaceMaterial), &assetsMaterial, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        constexpr GLuint bindingPoint = 0;
        glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, surfaceMaterialUBO);

        glUseProgram(shader.id);
        glUniform1i(glGetUniformLocation(shader.id, "normalMapReady"), false);
        glUniform1i(glGetUniformLocation(shader.id, "roughnessMapReady"), false);

        lastSurfaceMaterialUBO = surfaceMaterialUBO;
    }

    static bool lightsUpdated = false;
    if (!lightsUpdated) {
        UpdateLightsBuffer(true, renderModelPreviewerLights, renderPrevierLightsBuffer);
        lightsUpdated = true;
    }

    if (isDirectoryModified(dirPath)) {
        folderStruct.clear();
        fileStruct.clear();

        for (const fs::directory_entry& entry : fs::directory_iterator(dirPath)) {
            const fs::path& entryPath = entry.path();
            const fs::path& file = entryPath.filename();

            if (entry.is_directory()) {
                folderStruct.emplace_back(file, folderTexture, entryPath);
            } else if (entry.is_regular_file()) {
                fileStruct.emplace_back(createFileTextureItem(file, entryPath, entryPath.extension().string()));
            }
        }
    }
}

void AssetsExplorerTopBar() {
    if (dirPath == "project") return;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    if (ImGui::Button("<--")) {
        dirPath = dirPath.parent_path();
        auto it = directoriesLastModify.find(dirPath);
        if (it != directoriesLastModify.end()) directoriesLastModify.erase(it);
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = nullptr;

        for (const char* payload_type : filesPayloadTypes) {
            payload = ImGui::AcceptDragDropPayload(payload_type);
            if (payload) break;
        }

        if (payload && payload->DataSize == sizeof(int)) {
            int payload_n = *(const int*)payload->Data;
            fs::path sourceFilePath = dirPath / fileStruct[payload_n].name;
            fs::path destinationFilePath = dirPath.parent_path() / fileStruct[payload_n].name;

            if (!fs::is_regular_file(destinationFilePath)) {
                try {
                    fs::rename(sourceFilePath, destinationFilePath);
                } catch (const fs::filesystem_error& e) {
                    TraceLog(LOG_ERROR, ("Failed to rename file: " + std::string(e.what())).c_str());
                }
            } else {
                TraceLog(LOG_ERROR, "Failed to move file, because file already exists.");
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

    static const std::string assetsExplorerName = (std::string(ICON_FA_FOLDER_OPEN) + " Assets Explorer");
    static const char* assetsExplorerName_cstr = assetsExplorerName.c_str();
    ImGui::Begin(assetsExplorerName_cstr, nullptr);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5);

    UpdateFileFolderStructures();
    AssetsExplorerTopBar();


    ImGui::BeginDisabled();
    static std::string assetsExplorerPathText = (std::string(dirPath.string()) + "##AssetsExplorerPath");
    static const char* assetsExplorerPathText_cstr = assetsExplorerPathText.c_str();
    ImGui::Button(assetsExplorerPathText_cstr);
    ImGui::EndDisabled();

    ImGui::BeginChild("Assets Explorer Child", ImVec2(-1, -1), true);
    assetsExplorerWindowSize = ImGui::GetWindowSize();

    bool isFolderHovered = false;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, static_cast<int>(panelWidth / (cellSize + padding)));

    ImGui::Columns(columnCount, 0, false);

    // FOLDERS List
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(cellSize / 8, cellSize / 8));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    float buttonWidth = thumbnailSize;
    float halfButton = buttonWidth / 2.0f;
    int numFolders = folderStruct.size();

    for (size_t index = 0; index < numFolders; index++) {
        FolderTextureItem& folderItem = folderStruct[index];

        ImGui::PushID(index);

        ImGui::ImageButton((ImTextureID)&folderTexture, ImVec2(thumbnailSize, thumbnailSize));

        if (renameFolderIndex == index) folderItem.rename = true;

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = nullptr;

            for (const char* payload_type : filesPayloadTypes) {
                payload = ImGui::AcceptDragDropPayload(payload_type);
                if (payload) break;
            }

            if (payload) {
                if (payload->DataSize == sizeof(int)) {
                    int payload_n = *(const int*)payload->Data;

                    fs::path sourceFilePath = fs::current_path() / dirPath / fileStruct[payload_n].name;
                    fs::path destinationFilePath = fs::current_path() / dirPath / folderItem.name / fileStruct[payload_n].name;

                    try {
                        fs::rename(sourceFilePath, destinationFilePath);
                    } catch (const fs::filesystem_error& e) {
                        TraceLog(LOG_ERROR, (std::string("Failed to rename file: ") + std::string(e.what())).c_str());
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
                if (it != directoriesLastModify.end()) directoriesLastModify.erase(it);
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
                        TraceLog(LOG_ERROR, (std::string("Failed to rename directory: ") + std::string(e.what())).c_str());
                    }
                }

                folderItem.rename = false;
                renameFolderIndex = -1;
            }
        } else {
            static float averageCharWidth = 3.0f;
            float textWidth = averageCharWidth * folderItem.name.native().length();;
            float textXPos = ImGui::GetCursorPosX() + (thumbnailSize - textWidth) * 0.5f;

            ImGui::SetCursorPosX(textXPos);
            ImGui::Text("%s", folderItem.name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    // FILES List
    int numFiles = fileStruct.size();

    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && numFiles <= 0 && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
        showAddFilePopup = true;

    for (int index = 0; index < numFiles; index++) {
        FileTextureItem& fileItem = fileStruct[index];
        ImGui::PushID(index);

        ImGui::ImageButton((ImTextureID)&fileItem.texture, ImVec2(thumbnailSize, thumbnailSize));

        bool isButtonHovered = ImGui::IsItemHovered();

        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && ImGui::IsWindowHovered() && !isFolderHovered) {
            if (isButtonHovered) {
                fileIndex = index;
                showEditFilePopup = true;
            } else showAddFilePopup = true;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                const char* dragType = getDragType(fileItem.extension);

                ImGui::SetDragDropPayload(dragType, &index, sizeof(int));
                ImGui::Image((void *)(intptr_t)(ImTextureID)&fileItem.texture, ImVec2(64, 64));
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

        if (renameFileIndex == index) fileItem.rename = true;

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
                        TraceLog(LOG_ERROR, (std::string("Failed to rename file: ") + std::string(e.what())).c_str());
                    }
                }

                fileItem.rename = false;
                renameFileIndex = -1;
            }
        } else {
            static float averageCharWidth = 3.0f;
            float textWidth = averageCharWidth * fileItem.name.native().length();
            float textXPos = ImGui::GetCursorPosX() + (thumbnailSize - textWidth) * 0.5f;

            ImGui::SetCursorPosX(textXPos);
            ImGui::Text("%s", fileItem.name.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    AddFileManipulation();
    EditFolderManipulation();
    EditFileManipulation();

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::End();

    std::chrono::high_resolution_clock::time_point assetsExplorer_end = std::chrono::high_resolution_clock::now();
    assetsExplorerProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(assetsExplorer_end - assetsExplorer_start);
}