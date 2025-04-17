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

struct FolderTextureItem {
    fs::path name;
    Texture2D texture;
    fs::path full_path;
    bool rename;

    FolderTextureItem(const fs::path& name, const Texture2D& texture, const fs::path& full_path, bool rename = false)
        : name(name), texture(texture), full_path(full_path), rename(rename) {}
};

struct FileTextureItem {
    fs::path name;
    Texture2D texture;
    fs::path path;
    fs::path full_path;
    std::string extension;
    bool rename;
};

struct FileInfo {
    fs::path path;
    std::string extension;
    fs::file_time_type lastModified;
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
extern std::unordered_map<fs::path, Texture2D> textureCache;
extern std::unordered_map<fs::path, fs::file_time_type> directoriesLastModify;

extern fs::path dirPath;
extern float padding;
extern float thumbnailSize;
extern float cellSize;

extern ImVec2 assetsExplorerWindowSize;
extern LitCamera modelPreviewerCamera;
extern SurfaceMaterial assetsMaterial;

extern std::vector<FolderTextureItem> folderStruct;
extern std::vector<FileTextureItem> fileStruct;

void InitRenderModelPreviewer();
void AssetsExplorer();

#endif // ASSETSEXPLORER_H
