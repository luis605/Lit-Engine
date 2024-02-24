// Engine/Ui/AssetsExplorer.h
#ifndef ASSETSEXPLORER_H
#define ASSETSEXPLORER_H
#include "../../../include_all.h"

Texture2D folderTexture;
Texture2D imageTexture;
Texture2D cppTexture;
Texture2D emptyTexture;
Texture2D pythonTexture;
Texture2D modelTexture;
Texture2D materialTexture;

RenderTexture2D target;

struct FolderTextureItem {
    std::string name;
    Texture2D texture;
    std::filesystem::path full_path = "";
    bool rename = false;

    FolderTextureItem(const std::string& name, const Texture2D& texture, const std::filesystem::path& full_path, bool rename = false)
        : name(name), texture(texture), full_path(full_path), rename(rename) {}
};
vector<FolderTextureItem> foldersTextureStruct;

struct FileTextureItem {
    string name;
    Texture2D texture;
    fs::path path;
    fs::path full_path = "";
    bool rename = false;
};
vector<FileTextureItem> filesTextureStruct;

std::unordered_map<string, Texture2D> modelsIcons;

struct dirent *ent;
struct stat st;

vector<string> files;
vector<string> folders;

fs::path dirPath = "project/game";
float padding = 10.0f;
float thumbnailSize = 128.0f;
float cellSize = thumbnailSize + padding;

ImVec2 assetsExplorerWindowSize = {cellSize, cellSize};

Camera3D modelPreviewerCamera = { 0 };

#include "file_manipulation.h"

#endif // ASSETSEXPLORER_H
