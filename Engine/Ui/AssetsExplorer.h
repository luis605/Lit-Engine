// Engine/Ui/AssetsExplorer.h
#ifndef ASSETSEXPLORER_H
#define ASSETSEXPLORER_H


#include <vector>
#include <string>
#include <dirent.h>
#include "../../include/raylib.h"

Texture2D folder_texture;
Texture2D image_texture;
Texture2D cpp_texture;
Texture2D empty_texture;

struct FolderTextureItem {
    std::string name;
    Texture2D texture;
};
std::vector<FolderTextureItem> folders_texture_struct;

struct FileTextureItem {
    std::string name;
    Texture2D texture;
};
std::vector<FileTextureItem> files_texture_struct;

DIR *dir;
struct dirent *ent;
struct stat st;

std::vector<std::string> files;
std::vector<std::string> folders;

std::string dir_path = "game";


#endif // ASSETSEXPLORER_H
