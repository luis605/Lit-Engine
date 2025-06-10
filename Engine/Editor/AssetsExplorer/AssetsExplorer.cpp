/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/AssetsExplorer/FileManipulation.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <extras/IconsFontAwesome6.h>
#include <Engine/Editor/MaterialNodeEditor/MaterialBlueprints.hpp>
#include <Engine/Editor/MaterialNodeEditor/Editor.hpp>
#include <imgui_stdlib.h>
#include <algorithm>

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
float padding = 30.0f;
float thumbnailSize = 64.0f;
float cellSize = thumbnailSize + padding;

ImVec2 assetsExplorerWindowSize = {cellSize, cellSize};
LitCamera modelPreviewerCamera = {0};
SurfaceMaterial assetsMaterial;

std::vector<FolderTextureItem> folderStruct;
std::vector<FileTextureItem> fileStruct;

void InitRenderModelPreviewer() {
    modelPreviewerCamera.position = {10.0f, 10.0f, 2.0f};
    modelPreviewerCamera.target = {0.0f, 0.0f, 0.0f};
    modelPreviewerCamera.up = {0.0f, 1.0f, 0.0f};
    modelPreviewerCamera.fovy = 60.0f;
    modelPreviewerCamera.projection = CAMERA_PERSPECTIVE;

    LightStruct lightStructA;
    LightStruct lightStructB;

    lightStructA.light.type = LIGHT_POINT;
    lightStructA.light.position = {11.0f, 6.0f, 15.0f};
    lightStructA.light.color = {1.0f, 0.75f, 0.75f, 1.0f};
    lightStructA.light.params.y = 10.0f;

    lightStructB.light.type = LIGHT_POINT;
    lightStructB.light.position = {20.0f, 10.0f, -25.0f};
    lightStructB.light.color = {1.0f, 0.16f, 0.16f, 1.0f};
    lightStructB.light.params.y = 6.0f;

    renderModelPreviewerLights.push_back(lightStructA);
    renderModelPreviewerLights.push_back(lightStructB);

    modelPreviewRT = LoadRenderTexture(thumbnailSize, thumbnailSize);
}

Texture2D RenderModelPreview(const char* modelFile) {
    Model model = LoadModel(modelFile);
    model.materials[0].shader = *shaderManager.m_defaultShader;
    if (!IsModelReady(model)) {
        TraceLog(LOG_WARNING, "Failed to load model.");
        return {0};
    }

    modelPreviewRT = LoadRenderTexture(thumbnailSize, thumbnailSize);

    BeginTextureMode(modelPreviewRT);
    BeginMode3D(modelPreviewerCamera);
    BeginShaderMode(*shaderManager.m_defaultShader);
    ClearBackground(GRAY);
    skybox.drawSkybox(modelPreviewerCamera);

    DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
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
    {".mat", materialTexture},{".matblueprint", materialTexture}};

std::unordered_map<std::string, const char*> dragTypeMap = {
    {".py", "SCRIPT_PAYLOAD"},    {".cpp", "SCRIPT_PAYLOAD"},
    {".c++", "SCRIPT_PAYLOAD"},   {".cxx", "SCRIPT_PAYLOAD"},
    {".hpp", "SCRIPT_PAYLOAD"},   {".cc", "SCRIPT_PAYLOAD"},
    {".h", "SCRIPT_PAYLOAD"},     {".hh", "SCRIPT_PAYLOAD"},
    {".hxx", "SCRIPT_PAYLOAD"},   {".fbx", "MODEL_PAYLOAD"},
    {".obj", "MODEL_PAYLOAD"},    {".glb", "MODEL_PAYLOAD"},
    {".gltf", "MODEL_PAYLOAD"},   {".ply", "MODEL_PAYLOAD"},
    {".mtl", "MODEL_PAYLOAD"},    {".mat", "MATERIAL_PAYLOAD"},
    {".matblueprint", "MASTER_MATERIAL_PAYLOAD"},
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

    glUseProgram((*shaderManager.m_defaultShader).id);

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

inline void HandleDropMove(const fs::path& destinationDirectory) {
    if (ImGui::BeginDragDropTarget()) {
        fs::path sourceItemName;
        bool isFolderPayload = false;

        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("FOLDER_PAYLOAD")) {
            isFolderPayload = true;
            sourceItemName = folderStruct[*(const int*)p->Data].name;
        } else {
            for (const char* payloadType : filesPayloadTypes) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(payloadType)) {
                    sourceItemName = fileStruct[*(const int*)p->Data].name;
                    break;
                }
            }
        }

        if (!sourceItemName.empty()) {
            if (isFolderPayload && !destinationDirectory.empty() && sourceItemName == destinationDirectory) {
                TraceLog(LOG_WARNING, "Cannot move folder into itself.");
            } else {
                fs::path sourcePath = dirPath / sourceItemName;
                fs::path destPath = destinationDirectory / sourceItemName;
                try {
                    fs::rename(sourcePath, destPath);
                } catch (const fs::filesystem_error& e) {
                    TraceLog(LOG_ERROR, "Failed to move item: %s", e.what());
                }
            }
        }

        ImGui::EndDragDropTarget();
    }
}

void AssetsExplorerTopBar() {
    if (dirPath == "project") return;

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    if (ImGui::Button("<--")) {
        dirPath = dirPath.parent_path();
        auto it = directoriesLastModify.find(dirPath);
        if (it != directoriesLastModify.end()) directoriesLastModify.erase(it);
    }

    HandleDropMove(dirPath.parent_path());

    ImGui::PopStyleColor(3);
    ImGui::SameLine();

    ImGui::BeginDisabled();
    const std::string assetsExplorerPathText = (std::string(dirPath.string()) + "##AssetsExplorerPath");
    const char* assetsExplorerPathText_cstr = assetsExplorerPathText.c_str();
    ImGui::Button(assetsExplorerPathText_cstr);
    ImGui::EndDisabled();
}


void CardButton(const ImTextureID& textureID, const int thumbnailSize, const std::string& itemName, const bool isRenaming, const char* dragDropType, const void* dragDropPayload, size_t dragDropPayloadSize) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(itemName.c_str());

    const char* nameStr = itemName.c_str();
    const float iconRenderSize = static_cast<float>(thumbnailSize);

    const float horizontalPadding = 12.0f;
    const float verticalPadding = 8.0f;
    const float iconTextSpacing = style.ItemSpacing.y;
    const float hoverRounding = 8.0f;
    const float cardTotalWidth = ImGui::GetColumnWidth() - style.ItemSpacing.x;

    const ImU32 hoverBackgroundColor = ImGui::GetColorU32(ImVec4(58/255.0f, 58/255.0f, 63/255.0f, 1.0f));

    ImVec2 textSize = ImGui::CalcTextSize(nameStr);

    float coreContentHeight = iconRenderSize + iconTextSpacing + textSize.y;
    float cardTotalHeight = coreContentHeight + 2.0f * verticalPadding;
    ImVec2 cardEffectiveSize(cardTotalWidth, cardTotalHeight);

    ImGui::BeginGroup();

    ImVec2 cardScreenPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##CardInteraction", cardEffectiveSize);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(dragDropType, dragDropPayload, dragDropPayloadSize);

        ImGui::BeginGroup();

        constexpr float imageWidth = 64.0f;
        const     float maxWidth = std::max(imageWidth, textSize.x);
        const     float initialCursorX = ImGui::GetCursorPosX();

        ImGui::SetCursorPosX(initialCursorX + (maxWidth - imageWidth) * 0.5f);
        ImGui::Image(textureID, ImVec2(imageWidth, imageWidth));

        ImGui::SetCursorPosX(initialCursorX + (maxWidth - textSize.x) * 0.5f);
        ImGui::TextUnformatted(nameStr);

        ImGui::EndGroup();

        ImGui::EndDragDropSource();
    }

    bool isItemHovered = ImGui::IsItemHovered();

    if (isItemHovered) {
        ImVec2 cardBottomRightScreenPos = ImVec2(cardScreenPos.x + cardTotalWidth, cardScreenPos.y + cardTotalHeight);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(cardScreenPos, cardBottomRightScreenPos, hoverBackgroundColor, hoverRounding);
    }

    float iconDrawPosX = cardScreenPos.x + (cardTotalWidth - iconRenderSize) * 0.5f;
    float iconDrawPosY = cardScreenPos.y + verticalPadding;
    ImGui::SetCursorScreenPos(ImVec2(iconDrawPosX, iconDrawPosY));
    ImGui::Image(textureID, ImVec2(iconRenderSize, iconRenderSize));

    if (isRenaming) [[unlikely]] {
        const char* textInputRenameLabel = "##RenameItem";
        ImGui::InputText(textInputRenameLabel, &renameBuffer);

        if (IsKeyPressed(KEY_ENTER)) {
            fs::path newPath = dirPath / renameBuffer;

            if (fs::exists(newPath)) {
                TraceLog(LOG_ERROR, "Directory already exists.");
            } else {
                try {
                    fs::rename(dirPath / itemName, newPath);
                } catch (const fs::filesystem_error& e) {
                    TraceLog(LOG_ERROR, (std::string("Failed to rename directory: ") + std::string(e.what())).c_str());
                }
            }

            renameIndex = -1;
        }
    } else [[likely]] {
        float maxTextWidth = cardTotalWidth - 2.0f * horizontalPadding;
        std::string textToRender = itemName;

        if (textSize.x > maxTextWidth) {
            const char* ellipsis = "...";
            float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
            int fittingChars = 0;
            for (int i = (int)itemName.length(); i > 0; --i) {
                std::string sub = itemName.substr(0, i);
                if (ImGui::CalcTextSize(sub.c_str()).x < maxTextWidth - ellipsisWidth) {
                    fittingChars = i;
                    break;
                }
            }
            textToRender = itemName.substr(0, fittingChars) + ellipsis;
            textSize = ImGui::CalcTextSize(textToRender.c_str());
        }

        float textDrawPosX = cardScreenPos.x + (cardTotalWidth - textSize.x) * 0.5f;
        float textDrawPosY = iconDrawPosY + iconRenderSize + iconTextSpacing;
        ImGui::SetCursorScreenPos(ImVec2(textDrawPosX, textDrawPosY));
        ImGui::TextUnformatted(textToRender.c_str());
    }

    ImGui::EndGroup();
    ImGui::PopID();
}


void AssetsExplorer() {
    auto assetsExplorer_start = std::chrono::high_resolution_clock::now();

    if (assetsExplorerWindowSize.x < cellSize) {
        ImGui::SetNextWindowSize({cellSize, assetsExplorerWindowSize.y});
    } else if (assetsExplorerWindowSize.y < cellSize) {
        ImGui::SetNextWindowSize({assetsExplorerWindowSize.y, cellSize});
    }

    constexpr const char* assetsExplorerName_cstr = ICON_FA_FOLDER_OPEN " Assets Explorer";
    ImGui::Begin(assetsExplorerName_cstr, nullptr);

    UpdateFileFolderStructures();
    AssetsExplorerTopBar();

    ImGui::BeginChild("Assets Explorer Child", ImVec2(-1, -1), true);
    assetsExplorerWindowSize = ImGui::GetWindowSize();

    bool isFolderHovered = false;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, static_cast<int>(panelWidth / (cellSize + padding)));

    ImGui::Columns(columnCount, 0, false);

    // FOLDERS List
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(cellSize * .12f, cellSize * .12f));

    ImGui::PushStyleColor(ImGuiCol_Button,          ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,   ImVec4(0.4, 0.4, 0.4, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,    ImVec4(0.35, 0.35, 0.35, 1));

    float buttonWidth = thumbnailSize;
    float halfButton = buttonWidth / 2.0f;
    int numFolders = folderStruct.size();

    for (size_t index = 0; index < numFolders; index++) {
        FolderTextureItem& folderItem = folderStruct[index];
        const bool shouldRename = renameIndex == index;

        ImGui::PushID(index);

        const char* textureDragType = "FOLDER_PAYLOAD";
        const int payloadIndex = index;

        CardButton(
            (ImTextureID)&folderTexture,
            thumbnailSize,
            folderItem.name.string(),
            shouldRename,
            textureDragType,
            &payloadIndex,
            sizeof(int)
        );

        HandleDropMove(dirPath / folderItem.name);

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

        const bool shouldRename = renameIndex == numFolders + index;

        ImGui::PushID(numFolders + index);

        const char* dragType = getDragType(fileItem.extension);
        const int payloadIndex = index;

        CardButton(
            (ImTextureID)&fileItem.texture,
            thumbnailSize,
            fileItem.name.string(),
            shouldRename,
            dragType,
            &payloadIndex,
            sizeof(int)
        );

        bool isButtonHovered = ImGui::IsItemHovered();

        if (isButtonHovered) {
            if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && !isFolderHovered) {
                fileIndex = index;
                showEditFilePopup = true;
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (fileItem.extension == ".matblueprint") {
                    fs::path blueprintPath = fileItem.full_path;

                    auto it = materialBlueprints.find(blueprintPath);

                    if (it != materialBlueprints.end()) {
                        selectedMaterialBlueprintPath = it->first;
                        isMaterialEditorOpen = true;
                    } else {
                        LoadMaterialBlueprint(blueprintPath);

                        it = materialBlueprints.find(blueprintPath);

                        if (!materialBlueprints.empty() && materialBlueprints.contains(blueprintPath)) {
                            selectedMaterialBlueprintPath = it->first;
                            isMaterialEditorOpen = true;
                        } else {
                            TraceLog(LOG_ERROR, "Failed to load material blueprint: %s",
                                     blueprintPath.string().c_str());
                        }
                    }
                }

                code = readFileToString((dirPath / fileItem.name).string());
                codeEditorScriptPath = (dirPath / fileItem.name).string();
                editor.SetText(code);
            }
        } else if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && ImGui::IsWindowHovered() && !isFolderHovered && !ImGui::IsAnyItemHovered()) {
             showAddFilePopup = true;
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

    std::chrono::high_resolution_clock::time_point assetsExplorer_end = std::chrono::high_resolution_clock::now();
    assetsExplorerProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(assetsExplorer_end - assetsExplorer_start);
}