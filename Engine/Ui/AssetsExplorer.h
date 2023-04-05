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
Texture2D python_texture;
Texture2D model_texture;

struct FolderTextureItem {
    string name;
    Texture2D texture;
};
vector<FolderTextureItem> folders_texture_struct;

struct FileTextureItem {
    string name;
    Texture2D texture;
};
vector<FileTextureItem> files_texture_struct;

DIR *dir;
struct dirent *ent;
struct stat st;

vector<string> files;
vector<string> folders;

string dir_path = "game";


#endif // ASSETSEXPLORER_H
