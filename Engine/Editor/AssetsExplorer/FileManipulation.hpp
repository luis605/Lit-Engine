/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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
extern int renameIndex;
extern int renameIndex;
extern std::string renameBuffer;
extern fs::path renameFolderName;
extern int fileIndex;
extern int folderIndex;

[[nodiscard]] std::string generateNumberedFileName(const fs::path& directoryPath, const std::string& extension);
[[nodiscard]] bool createNumberedFile(const fs::path& directoryPath, const std::string& extension);
[[nodiscard]] bool createNumberedFolder(const fs::path& directoryPath);
void EditFolderManipulation();
void EditFileManipulation();
void AddFileManipulation();

#endif // FILE_MANIPULATION_H