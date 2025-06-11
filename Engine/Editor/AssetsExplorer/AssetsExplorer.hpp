/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef ASSETSEXPLORER_H
#define ASSETSEXPLORER_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <raylib.h>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <Engine/Scripting/functions.hpp>
#include <Engine/Lighting/lights.hpp>

namespace fs = std::filesystem;

struct DirectoryEntry {
    fs::path path;
    fs::path name;
    std::string extension;
    Texture2D texture;
    bool isFolder;
    bool isModel;

    DirectoryEntry() {}

    DirectoryEntry(const fs::path& path, const fs::path& name, const std::string& extension, Texture2D& texture, const bool isFolder, const bool isModel)
        : path(path), name(name), extension(extension), texture(texture), isFolder(isFolder), isModel(isModel) {}
};

extern Texture2D folderTexture;
extern Texture2D imageTexture;
extern Texture2D cppTexture;
extern Texture2D emptyTexture;
extern Texture2D pythonTexture;
extern Texture2D modelTexture;
extern Texture2D materialTexture;
extern RenderTexture2D modelPreviewRT;

extern std::unordered_map<fs::path, Texture2D> modelsIcons;
extern std::unordered_map<fs::path, fs::file_time_type> directoriesLastModify;

extern fs::path dirPath;
extern float padding;
extern float thumbnailSize;
extern float cellSize;

extern ImVec2 assetsExplorerWindowSize;
extern LitCamera modelPreviewerCamera;
extern SurfaceMaterial assetsMaterial;

extern std::vector<DirectoryEntry> allItems;

void InitRenderModelPreviewer();
void AssetsExplorer();

#endif // ASSETSEXPLORER_H
