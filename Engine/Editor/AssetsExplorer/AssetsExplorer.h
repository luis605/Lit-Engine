// Engine/Ui/AssetsExplorer.h
#ifndef ASSETSEXPLORER_H
#define ASSETSEXPLORER_H
#include "../../../include_all.h"

Texture2D folder_texture;
Texture2D image_texture;
Texture2D cpp_texture;
Texture2D empty_texture;
Texture2D python_texture;
Texture2D model_texture;
Texture2D material_texture;

RenderTexture2D target;

struct FolderTextureItem {
    string name;
    Texture2D texture;

    FolderTextureItem(const string& name, const Texture2D& texture)
        : name(name), texture(texture) {}
};
vector<FolderTextureItem> folders_texture_struct;

struct FileTextureItem {
    string name;
    Texture2D texture;
    fs::path path;
    fs::path full_path = "";
    bool rename = false;
};
vector<FileTextureItem> files_texture_struct;

std::unordered_map<string, Texture2D> models_icons;

struct dirent *ent;
struct stat st;

vector<string> files;
vector<string> folders;

fs::path dir_path = "project/game";
float padding = 10.0f;
float thumbnailSize = 128.0f;
float cellSize = thumbnailSize + padding;

ImVec2 AssetsExplorer_window_size = {cellSize, cellSize};

Camera3D model_previewer_camera = { 0 };

#include "file_manipulation.h"

#endif // ASSETSEXPLORER_H
