#ifndef ASSETSEXPLORER_H
#define ASSETSEXPLORER_H

Texture2D folderTexture;
Texture2D imageTexture;
Texture2D cppTexture;
Texture2D emptyTexture;
Texture2D pythonTexture;
Texture2D modelTexture;
Texture2D materialTexture;

RenderTexture2D modelPreviewRT;

struct FolderTextureItem {
    fs::path name;
    Texture2D texture;
    fs::path full_path;
    bool rename;

    FolderTextureItem(const fs::path& name, const Texture2D& texture, const fs::path& full_path, bool rename = false)
        : name(name), texture(texture), full_path(full_path), rename(rename) {}
};
std::vector<FolderTextureItem> folderStruct;

struct FileTextureItem {
    fs::path name;
    Texture2D texture;
    fs::path path;
    fs::path full_path;
    std::string extension;
    bool rename;
};
std::vector<FileTextureItem> fileStruct;

struct FileInfo {
    fs::path path;
    std::string extension;
    fs::file_time_type lastModified;
};

std::unordered_map<fs::path, Texture2D> modelsIcons;
std::unordered_map<fs::path, Texture2D> textureCache;
std::unordered_map<fs::path, fs::file_time_type> directoriesLastModify;

fs::path dirPath = "project/game";
float padding = 10.0f;
float thumbnailSize = 64.0f;
float cellSize = thumbnailSize + padding;

ImVec2 assetsExplorerWindowSize = {cellSize, cellSize};

LitCamera modelPreviewerCamera = { 0 };

SurfaceMaterial assetsMaterial;

#endif // ASSETSEXPLORER_H
