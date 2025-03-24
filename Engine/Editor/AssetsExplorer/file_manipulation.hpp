#ifndef FILE_MANIPULATION_H
#define FILE_MANIPULATION_H

#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/CodeEditor/CodeEditor.hpp>
#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include <filesystem>
#include <fstream>
#include <raylib.h>
#include <string>

namespace fs = std::filesystem;

extern bool showAddFilePopup;
extern bool showEditFilePopup;
extern bool showEditFolderPopup;
extern int renameFileIndex;
extern int renameFolderIndex;
extern char renameFileBuffer[256];
extern char renameFolderBuffer[256];
extern fs::path renameFolderName;
extern int fileIndex;
extern int folderIndex;

std::string generateNumberedFileName(const fs::path& directoryPath,
                                     const std::string& extension);
bool createNumberedFile(const fs::path& directoryPath,
                        const std::string& extension);
bool createNumberedFolder(const fs::path& directoryPath);
void EditFolderManipulation();
void EditFileManipulation();
void AddFileManipulation();

#endif // FILE_MANIPULATION_H