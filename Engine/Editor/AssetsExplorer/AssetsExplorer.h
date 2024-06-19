// Engine/Ui/AssetsExplorer.h
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
    std::string name;
    Texture2D texture;
    fs::path full_path;
    bool rename;

    FolderTextureItem(const std::string& name, const Texture2D& texture, const fs::path& full_path, bool rename = false)
        : name(name), texture(texture), full_path(full_path), rename(rename) {}
};
std::vector<FolderTextureItem> folderStruct;

struct FileTextureItem {
    std::string name;
    Texture2D texture;
    fs::path path;
    fs::path full_path;
    bool rename;
};
std::vector<FileTextureItem> fileStruct;

std::unordered_map<std::string, Texture2D> modelsIcons;

fs::path dirPath = "project/game";
float padding = 10.0f;
float thumbnailSize = 128.0f;
float cellSize = thumbnailSize + padding;

ImVec2 assetsExplorerWindowSize = {cellSize, cellSize};

Camera3D modelPreviewerCamera = { 0 };

#endif // ASSETSEXPLORER_H
