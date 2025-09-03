/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Editor/AssetsExplorer/FileManipulation.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

fs::path renameFolderName;
bool showAddFilePopup = false;
bool showEditFilePopup = false;
bool showEditFolderPopup = false;
std::string renameBuffer;
int renameIndex = -1;
int fileIndex = -1;
int folderIndex = -1;

[[nodiscard]] std::string generateNumberedFileName(const fs::path& directoryPath, const std::string& extension) {
    int fileNumber = 1;
    fs::path filePath;
    std::error_code ec;

    do {
        filePath = directoryPath / ("file" + std::to_string(fileNumber) + "." + extension);
        fileNumber++;
    } while (fs::exists(filePath, ec));

    return filePath.string();
}

[[nodiscard]] bool createNumberedFile(const fs::path& directoryPath, const std::string& extension) {
    const std::string filePathString = generateNumberedFileName(directoryPath, extension);
    std::ofstream outputFile(filePathString);

    if (outputFile.is_open()) {
        outputFile.close();
        return true;
    }

    TraceLog(LOG_ERROR, "Failed to create the file: %s", filePathString.c_str());
    return false;
}

[[nodiscard]] bool createNumberedFolder(const fs::path& directoryPath) {
    std::error_code ec;
    for (int folderNumber = 1; ; ++folderNumber) {
        const fs::path folderPath = directoryPath / std::to_string(folderNumber);
        if (!fs::exists(folderPath, ec)) {
            if (fs::create_directory(folderPath, ec)) {
                return true;
            } else {
                TraceLog(LOG_ERROR, "Failed to create directory '%s': %s", folderPath.string().c_str(), ec.message().c_str());
                return false;
            }
        }
    }
    return false;
}

void EditFolderManipulation() {
    if (showEditFolderPopup) {
        ImGui::OpenPopup("edit_folder_popup");
        showEditFolderPopup = false;
    }

    if (ImGui::BeginPopup("edit_folder_popup")) {
        ImGui::Text("Edit Folder");
        ImGui::Separator();
        const float buttonWidth = ImGui::GetContentRegionAvail().x;

        if (folderIndex != -1) {
            if (ImGui::Button("Rename", ImVec2(buttonWidth, 0))) {
                renameIndex = folderIndex;
                renameBuffer = allItems[folderIndex].name.string();
                folderIndex = -1;
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::Button("Delete", ImVec2(buttonWidth, 0))) {
                const fs::path pathToDelete = allItems[folderIndex].path;
                fs::remove_all(pathToDelete);
                folderIndex = -1;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditFileManipulation() {
    if (showEditFilePopup) {
        ImGui::OpenPopup("edit_file_popup");
        showEditFilePopup = false;
    }

    if (ImGui::BeginPopup("edit_file_popup")) {
        ImGui::Text("Edit File");
        ImGui::Separator();
        const float buttonWidth = ImGui::GetContentRegionAvail().x;

        if (fileIndex != -1) {
            if (ImGui::Button("Rename", ImVec2(buttonWidth, 0))) {
                renameIndex = fileIndex;
                renameBuffer = allItems[fileIndex].name.string();
                fileIndex = -1;
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::Button("Delete", ImVec2(buttonWidth, 0))) {
                const fs::path pathToDelete = allItems[fileIndex].path;
                fs::remove_all(pathToDelete);
                fileIndex = -1;
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::Button("Run", ImVec2(buttonWidth, 0))) {
                entitiesList.assign(entitiesListPregame.begin(), entitiesListPregame.end());

                Entity runScriptEntity;
                runScriptEntity.scriptPath = allItems[fileIndex].path;
                runScriptEntity.scriptIndex = "script not defined";
                runScriptEntity.setFlag(Entity::Flag::CALC_PHYSICS, true);

                LitCamera* sceneCamera_reference = &sceneEditor.sceneCamera;
                runScriptEntity.setupScript(sceneCamera_reference);
                runScriptEntity.runScript(sceneCamera_reference);

                fileIndex = -1;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AddFileManipulation() {
    if (showAddFilePopup) {
        ImGui::OpenPopup("popup");
        showAddFilePopup = false;
    }

    if (ImGui::BeginPopup("popup", ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("Add");
        ImGui::Separator();

        if (ImGui::MenuItem("Folder")) {
            if (createNumberedFolder(dirPath)) {
                ImGui::CloseCurrentPopup();
            }
        }

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Python")) {
                if (createNumberedFile(dirPath, "py")) {
                    ImGui::CloseCurrentPopup();
                }
            }

            if (ImGui::MenuItem("Material")) {
                if (createNumberedFile(dirPath, "mat")) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }
}