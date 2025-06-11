/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/AssetsExplorer/FileManipulation.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialBlueprints.hpp>
#include <Engine/Editor/MaterialNodeEditor/Editor.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <Engine/Lighting/BRDF.hpp>
#include <extras/IconsFontAwesome6.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <raymath.h>

Texture2D folderTexture;
Texture2D imageTexture;
Texture2D cppTexture;
Texture2D emptyTexture;
Texture2D pythonTexture;
Texture2D modelTexture;
Texture2D materialTexture;
RenderTexture2D modelPreviewRT;

std::unordered_map<fs::path, Texture2D> modelsIcons;
std::unordered_map<fs::path, fs::file_time_type> directoriesLastModify;

fs::path dirPath = "project/game";
float padding = 30.0f;
float thumbnailSize = 64.0f;
float cellSize = thumbnailSize + padding;

ImVec2 assetsExplorerWindowSize = {cellSize, cellSize};
LitCamera modelPreviewerCamera = {0};
SurfaceMaterial assetsMaterial;

std::vector<DirectoryEntry> allItems;

namespace {
    constexpr struct CardMetrics {
        float horizontalPadding = 12.0f;
        float verticalPadding = 8.0f;
        float hoverRounding = 8.0f;
    } metrics;

    float GetCardHeight() noexcept {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float textHeight = ImGui::GetTextLineHeight();
        const float textBlockHeight = (textHeight * 2) + style.ItemSpacing.y;
        return thumbnailSize + style.ItemSpacing.y + textBlockHeight + (2.0f * metrics.verticalPadding);
    }
}

void InitRenderModelPreviewer() {
    modelPreviewerCamera.position = {10.0f, 10.0f, 2.0f};
    modelPreviewerCamera.target = {0.0f, 0.0f, 0.0f};
    modelPreviewerCamera.up = {0.0f, 1.0f, 0.0f};
    modelPreviewerCamera.fovy = 60.0f;
    modelPreviewerCamera.projection = CAMERA_PERSPECTIVE;

    LightStruct lightStructA;
    lightStructA.light.type = LIGHT_POINT;
    lightStructA.light.position = {11.0f, 6.0f, 15.0f};
    lightStructA.light.color = {1.0f, 0.75f, 0.75f, 1.0f};
    lightStructA.light.params.y = 10.0f;

    LightStruct lightStructB;
    lightStructB.light.type = LIGHT_POINT;
    lightStructB.light.position = {20.0f, 10.0f, -25.0f};
    lightStructB.light.color = {1.0f, 0.16f, 0.16f, 1.0f};
    lightStructB.light.params.y = 6.0f;

    renderModelPreviewerLights.push_back(lightStructA);
    renderModelPreviewerLights.push_back(lightStructB);

    modelPreviewRT = LoadRenderTexture(thumbnailSize, thumbnailSize);

    glUseProgram((*shaderManager.m_defaultShader).id);
    UpdateLightsBuffer(true, renderModelPreviewerLights, renderPrevierLightsBuffer);
}

Texture2D RenderModelPreview(const char* modelFile) {
    Model model = LoadModel(modelFile);
    if (!IsModelReady(model)) {
        TraceLog(LOG_WARNING, "Failed to load model: %s", modelFile);
        UnloadModel(model);
        return emptyTexture;
    }

    model.materials[0].shader = *shaderManager.m_defaultShader;

    BoundingBox bounds = GetModelBoundingBox(model);
    Vector3 center = {(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f};
    Vector3 size = {bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z};
    float maxDim = fmaxf(fmaxf(size.x, size.y), size.z);
    if (maxDim <= 0.0f) maxDim = 1.0f;

    modelPreviewerCamera.target = {0.0f, 0.0f, 0.0f};
    modelPreviewerCamera.up = {0.0f, 1.0f, 0.0f};
    modelPreviewerCamera.fovy = 45.0f;
    modelPreviewerCamera.projection = CAMERA_PERSPECTIVE;

    float distance = (maxDim * 0.5f) / tanf((modelPreviewerCamera.fovy * DEG2RAD) * 0.5f);
    distance *= 2.0f;
    modelPreviewerCamera.position = {distance, distance * 0.6f, distance};

    BeginTextureMode(modelPreviewRT);
    ClearBackground(GRAY);
    BeginMode3D(modelPreviewerCamera);
    skybox.drawSkybox(modelPreviewerCamera);

    BeginShaderMode(*shaderManager.m_defaultShader);
    UpdateLightsBuffer(true, renderModelPreviewerLights, renderPrevierLightsBuffer);
    float cameraPos[3] = {modelPreviewerCamera.position.x, modelPreviewerCamera.position.y, modelPreviewerCamera.position.z};
    SetShaderValue(*shaderManager.m_defaultShader, shaderManager.GetUniformLocation((*shaderManager.m_defaultShader).id, "viewPos"), cameraPos, SHADER_UNIFORM_VEC3);
    SetShaderValueTexture(*shaderManager.m_defaultShader, shaderManager.GetUniformLocation(shaderManager.m_defaultShader->id, "irradiance"), skybox.irradianceTex);
    SetShaderValueTexture(*shaderManager.m_defaultShader, shaderManager.GetUniformLocation(shaderManager.m_defaultShader->id, "brdfLUT"), brdf.m_brdfLut);
    const int lightsCount = 2;
    SetShaderValue(*shaderManager.m_defaultShader, shaderManager.GetUniformLocation(shaderManager.m_defaultShader->id, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
    DrawModel(model, {0, 0, 0}, 1.0f, WHITE);
    EndShaderMode();

    EndMode3D();
    EndTextureMode();
    UnloadModel(model);

    // This is needed or the texture cache may become invalidated
    Image image = LoadImageFromTexture(modelPreviewRT.texture);
    Texture2D newTexture = LoadTextureFromImage(image);
    UnloadImage(image);

    return newTexture;
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

const char* getDragType(const std::string& fileExtension) noexcept {
    auto it = dragTypeMap.find(fileExtension);
    return (it != dragTypeMap.end()) ? it->second : "FILE_PAYLOAD";
}

Texture2D& getTextureForFileExtension(const std::string& fileExtension) noexcept {
    auto it = extensionToTextureMap.find(fileExtension);
    return (it != extensionToTextureMap.end()) ? it->second : emptyTexture;
}

DirectoryEntry createFileEntry(const fs::path& entryPath) {
    const std::string extension = entryPath.extension().string();
    const bool isModel = (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb" || extension == ".ply");

    Texture2D texture = emptyTexture;
    if (isModel) {
        if (auto it = modelsIcons.find(entryPath); it != modelsIcons.end()) {
            texture = it->second;
        } else {
            texture = RenderModelPreview(entryPath.string().c_str());
            modelsIcons[entryPath] = texture;
        }
    } else {
        texture = getTextureForFileExtension(extension);
    }
    return {entryPath, entryPath.filename(), extension, texture, false, isModel};
}

bool isDirectoryModified(const fs::path& path) {
    if (!fs::exists(path)) return false;

    auto it = directoriesLastModify.find(path);
    const fs::file_time_type currentTime = fs::last_write_time(path);

    if (it != directoriesLastModify.end()) {
        if (it->second == currentTime) {
            return false;
        }
        it->second = currentTime;
        return true;
    }

    directoriesLastModify[path] = currentTime;
    return true;
}

void UpdateDirectoryCache() {
    if (dirPath.empty() || !fs::exists(dirPath)) return;

    if (isDirectoryModified(dirPath)) {
        std::unordered_set<fs::path> modelsBeforeUpdate;
        for (const auto& item : allItems) {
            if (item.isModel && item.path.parent_path() == dirPath) {
                modelsBeforeUpdate.insert(item.path);
            }
        }

        allItems.clear();
        for (const auto& entry : fs::directory_iterator(dirPath)) {
            DirectoryEntry newEntry;
            if (entry.is_directory()) {
                newEntry = {entry.path(), entry.path().filename(), "", folderTexture, true, false};
            } else if (entry.is_regular_file()) {
                newEntry = createFileEntry(entry.path());
            }

            if (!newEntry.path.empty()) {
                if (newEntry.isModel) {
                    modelsBeforeUpdate.erase(newEntry.path);
                }
                allItems.push_back(std::move(newEntry));
            }
        }

        for (const auto& deletedModelPath : modelsBeforeUpdate) {
            if (auto it = modelsIcons.find(deletedModelPath); it != modelsIcons.end()) {
                UnloadTexture(it->second);
                modelsIcons.erase(it);
            }
        }

        std::sort(allItems.begin(), allItems.end(), [](const auto& a, const auto& b) {
            if (a.isFolder != b.isFolder) return a.isFolder;
            return a.name < b.name;
        });
    }
}

inline void MoveEntryOnDrop(const fs::path& destinationDirectory) {
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = nullptr;
        for (const char* payloadType : filesPayloadTypes) {
            if ((payload = ImGui::AcceptDragDropPayload(payloadType))) break;
        }
        if (!payload) payload = ImGui::AcceptDragDropPayload("FOLDER_PAYLOAD");

        if (payload) {
            const int sourceIndex = *(const int*)payload->Data;
            const DirectoryEntry& sourceEntry = allItems[sourceIndex];
            const bool isDroppingOnSelf = sourceEntry.isFolder && !destinationDirectory.empty() && (sourceEntry.path == destinationDirectory);

            if (!isDroppingOnSelf) {
                fs::path destPath = destinationDirectory / sourceEntry.name;
                try {
                    fs::rename(sourceEntry.path, destPath);
                } catch (const fs::filesystem_error& e) {
                    TraceLog(LOG_ERROR, "Failed to move item: %s", e.what());
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void AssetsExplorerTopBar() {
    if (dirPath.string() == "project") return;

    if (ImGui::Button(ICON_FA_ARROW_LEFT)) {
        dirPath = dirPath.parent_path();

        auto it = directoriesLastModify.find(dirPath);
        if (it != directoriesLastModify.end()) directoriesLastModify.erase(it);
    }

    MoveEntryOnDrop(dirPath.parent_path());

    ImGui::SameLine();
    ImGui::TextDisabled("%s", dirPath.string().c_str());
}

static std::string FormatStringToTwoLines(const std::string& text, float maxWidth) noexcept {
    const char* ellipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;

    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }

    int ideal_split_point = -1;
    for (int i = 1; i <= static_cast<int>(text.length()); ++i) {
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + i).x > maxWidth) {
            ideal_split_point = i > 0 ? i - 1 : 0;
            break;
        }
    }

    int split_pos = -1;
    if (ideal_split_point > 0) {
        for (int i = ideal_split_point; i > 0; --i) {
            if (isspace(text[i])) {
                split_pos = i;
                break;
            }
        }
    }
    if (split_pos == -1) split_pos = ideal_split_point > 0 ? ideal_split_point : 0;

    std::string line1 = text.substr(0, split_pos);
    std::string line2 = text.substr(split_pos);

    if (size_t first_char = line2.find_first_not_of(" \t\n\r"); first_char != std::string::npos) {
        line2 = line2.substr(first_char);
    } else {
        line2.clear();
    }

    if (ImGui::CalcTextSize(line2.c_str()).x > maxWidth - ellipsisWidth) {
        int fit_chars_l2 = 0;
        for (int i = line2.length(); i > 0; --i) {
            if (ImGui::CalcTextSize(line2.c_str(), line2.c_str() + i).x <= maxWidth - ellipsisWidth) {
                fit_chars_l2 = i;
                break;
            }
        }
        line2 = line2.substr(0, fit_chars_l2) + ellipsis;
    }

    return line1 + "\n" + line2;
}

void CardButton(const DirectoryEntry& item, const int index, const bool isRenaming) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(index);

    const std::string nameStr  = item.name.string();
    const char*       nameCStr = nameStr.c_str();

    const float cardTotalWidth  = ImGui::GetColumnWidth() - style.ItemSpacing.x;
    const float cardTotalHeight = GetCardHeight();

    const ImVec2 uv0 = ImVec2(0, item.isModel ? 1 : 0);
    const ImVec2 uv1 = ImVec2(1, item.isModel ? 0 : 1);

    const ImTextureID textureID = (ImTextureID)&item.texture.id;

    ImGui::BeginGroup();

    // The current screen position of the top-left of our card.
    const ImVec2 cardScreenPos = ImGui::GetCursorScreenPos();

    // Common positioning for the icon.
    const float iconDrawPosX = cardScreenPos.x + (cardTotalWidth - thumbnailSize) * 0.5f;
    const float iconDrawPosY = cardScreenPos.y + metrics.verticalPadding;

    // --- The main logic is now split based on the 'isRenaming' state ---
    if (isRenaming) [[unlikely]] {
        // --- RENAMING MODE ---
        // We explicitly draw the icon first, then the input text below it.

        // 1. Draw the icon. It's just a visual element here.
        ImGui::SetCursorScreenPos({iconDrawPosX, iconDrawPosY});
        ImGui::Image(textureID, {thumbnailSize, thumbnailSize}, uv0, uv1);

        // 2. Position and draw the InputText widget below the icon.
        const float textDrawPosY = iconDrawPosY + thumbnailSize + style.ItemSpacing.y;
        ImGui::SetCursorScreenPos({cardScreenPos.x + metrics.horizontalPadding, textDrawPosY});

        ImGui::PushItemWidth(cardTotalWidth - 2.0f * metrics.horizontalPadding);
        ImGui::SetKeyboardFocusHere(); // Auto-focus the input text

        if (ImGui::InputText("##RenameItem", &renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
            fs::path newPath = dirPath / renameBuffer;

            if (fs::exists(newPath)) {
                TraceLog(LOG_ERROR, "Directory already exists.");
            } else {
                try {
                    fs::rename(dirPath / item.name, newPath);
                } catch (const fs::filesystem_error& e) {
                    TraceLog(LOG_ERROR, (std::string("Failed to rename directory: ") + std::string(e.what())).c_str());
                }
            }
            renameIndex = -1; // Exit renaming mode
        }
        ImGui::PopItemWidth();

        // If the user clicks away from the input text, also cancel renaming.
        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            renameIndex = -1;
        }

    } else [[likely]] {
        // --- NORMAL DISPLAY MODE ---
        // This is the original logic, which is correct for this mode.
        // The InvisibleButton covers the whole area for interaction (hover/drag).
        // The Image and Text are then drawn on top as non-interactive visuals.

        const ImVec2 cardEffectiveSize(cardTotalWidth, cardTotalHeight);
        ImGui::InvisibleButton("##CardInteraction", cardEffectiveSize);

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(getDragType(item.extension), &index, sizeof(int));
            ImGui::BeginGroup();
            const ImVec2 textSize = ImGui::CalcTextSize(nameCStr);
            constexpr float imageWidth = 64.0f;
            const     float maxWidth = std::max(imageWidth, textSize.x);
            const     float initialCursorX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(initialCursorX + (maxWidth - imageWidth) * 0.5f);
            ImGui::Image(textureID, ImVec2(imageWidth, imageWidth));
            ImGui::SetCursorPosX(initialCursorX + (maxWidth - textSize.x) * 0.5f);
            ImGui::TextUnformatted(nameCStr);
            ImGui::EndGroup();
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::GetWindowDrawList()->AddRectFilled(cardScreenPos, {cardScreenPos.x + cardTotalWidth, cardScreenPos.y + cardTotalHeight}, ImGui::GetColorU32(ImVec4(58/255.0f, 58/255.0f, 63/255.0f, 1.0f)), metrics.hoverRounding);
        }

        // Draw the icon and text on top of the invisible button
        ImGui::SetCursorScreenPos({iconDrawPosX, iconDrawPosY});
        ImGui::Image(textureID, {thumbnailSize, thumbnailSize}, uv0, uv1);

        const float maxTextWidth = cardTotalWidth - 2.0f * metrics.horizontalPadding;
        const std::string textToRender = FormatStringToTwoLines(nameStr, maxTextWidth);
        // ... (rest of text drawing logic is unchanged and correct)
        std::string line1 = textToRender;
        std::string line2;
        if (size_t newline_pos = textToRender.find('\n'); newline_pos != std::string::npos) {
            line1 = textToRender.substr(0, newline_pos);
            line2 = textToRender.substr(newline_pos + 1);
        }
        float textDrawPosY = iconDrawPosY + thumbnailSize + style.ItemSpacing.y;
        ImVec2 line1Size = ImGui::CalcTextSize(line1.c_str());
        float line1DrawX = cardScreenPos.x + (cardTotalWidth - line1Size.x) * 0.5f;
        ImGui::SetCursorScreenPos({line1DrawX, textDrawPosY});
        ImGui::TextUnformatted(line1.c_str());

        if (!line2.empty()) {
            ImVec2 line2Size = ImGui::CalcTextSize(line2.c_str());
            float line2DrawX = cardScreenPos.x + (cardTotalWidth - line2Size.x) * 0.5f;
            textDrawPosY += ImGui::GetTextLineHeight() + style.ItemSpacing.y;
            ImGui::SetCursorScreenPos({line2DrawX, textDrawPosY});
            ImGui::TextUnformatted(line2.c_str());
        }
    }

    // Since we're doing manual layout, we need to manually advance the cursor past our card.
    // We place a dummy item to occupy the space of the card.
    ImGui::SetCursorPos({ImGui::GetCursorPos().x, cardScreenPos.y - ImGui::GetWindowPos().y + cardTotalHeight});
    ImGui::Dummy({0,0}); // This advances the Y cursor for the next item in the column.

    ImGui::EndGroup();
    ImGui::PopID();
}

void AssetsExplorer() {
    auto assetsExplorer_start = std::chrono::high_resolution_clock::now();

    ImGui::SetNextWindowSizeConstraints({cellSize * 2, cellSize * 2}, {FLT_MAX, FLT_MAX});
    ImGui::Begin(ICON_FA_FOLDER_OPEN " Assets Explorer", nullptr);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(cellSize * .12f, cellSize * .12f));

    UpdateDirectoryCache();
    AssetsExplorerTopBar();

    ImGui::Separator();
    ImGui::BeginChild("AssetsScrollingRegion", ImVec2(0, 0), false);

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered()) {
        showAddFilePopup = true;
    }

    const float panelWidth = ImGui::GetContentRegionAvail().x;
    const int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));
    const int totalItemCount = allItems.size();

    ImGui::Columns(columnCount, 0, false);

    const int numRows = (totalItemCount + columnCount - 1) / columnCount;
    const float rowHeight = GetCardHeight();

    ImGuiListClipper clipper;
    clipper.Begin(numRows, rowHeight);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int col = 0; col < columnCount; ++col) {
                const int index = row * columnCount + col;
                if (index >= totalItemCount) break;

                const DirectoryEntry& item = allItems[index];
                const bool shouldRename = (renameIndex == index);

                if (shouldRename && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered() && (dirPath != item.path.parent_path())) {
                    renameIndex = -1;
                }

                CardButton(item, index, shouldRename);

                if (item.isFolder) MoveEntryOnDrop(item.path);

                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (item.isFolder) {
                            dirPath = item.path;

                            auto it = directoriesLastModify.find(dirPath);
                            if (it != directoriesLastModify.end()) directoriesLastModify.erase(it);
                        } else if (item.extension == ".matblueprint") {
                            fs::path blueprintPath = item.path;
                            auto it = materialBlueprints.find(blueprintPath);
                            if (it == materialBlueprints.end()) {
                                LoadMaterialBlueprint(blueprintPath);
                                it = materialBlueprints.find(blueprintPath);
                            }
                            if (it != materialBlueprints.end()) {
                                selectedMaterialBlueprintPath = it->first;
                                isMaterialEditorOpen = true;
                            } else {
                                TraceLog(LOG_ERROR, "Failed to load material blueprint: %s", blueprintPath.string().c_str());
                            }
                        } else {
                            code = readFileToString(item.path.string());
                            codeEditorScriptPath = item.path.string();
                            editor.SetText(code);
                        }
                    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        if (item.isFolder) {
                            folderIndex = index;
                            showEditFolderPopup = true;
                        } else {
                            fileIndex = index;
                            showEditFilePopup = true;
                        }
                    }
                }
                ImGui::NextColumn();
            }
        }
    }
    clipper.End();
    ImGui::Columns(1);

    EditFileManipulation();
    EditFolderManipulation();
    AddFileManipulation();

    ImGui::PopStyleVar(1);
    ImGui::EndChild();
    ImGui::End();

    auto assetsExplorer_end = std::chrono::high_resolution_clock::now();
    assetsExplorerProfilerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(assetsExplorer_end - assetsExplorer_start);
}