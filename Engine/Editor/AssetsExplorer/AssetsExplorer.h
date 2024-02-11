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
    std::string name;
    Texture2D texture;
    std::filesystem::path full_path = "";
    bool rename = false;

    FolderTextureItem(const std::string& name, const Texture2D& texture, const std::filesystem::path& full_path, bool rename = false)
        : name(name), texture(texture), full_path(full_path), rename(rename) {}
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

char rename_file_buffer[256];
char rename_folder_buffer[256];

#include "file_manipulation.h"

#endif // ASSETSEXPLORER_H
