#ifndef FILE_MANIPULATION_H
#define FILE_MANIPULATION_H

std::string generateNumberedFileName(const fs::path& directoryPath, const std::string& extension) {
    int fileNumber = 1;
    fs::path filePath;

    do {
        std::string fileName = "file" + std::to_string(fileNumber) + "." + extension;
        filePath = directoryPath / fileName;
        fileNumber++;
    } while (fs::exists(filePath));

    return filePath.string();
}

bool createNumberedFile(const fs::path& directoryPath, const std::string& extension) {
    std::string filePathString = generateNumberedFileName(directoryPath, extension);

    std::ofstream outputFile(filePathString);
    
    if (outputFile.is_open()) {
        outputFile.close();
        return true;
    } else {
        TraceLog(LOG_ERROR, (std::string("Failed to create the file: ") + std::string(filePathString)).c_str());
        return false;
    }
}

bool createNumberedFolder(const fs::path& directoryPath) {
    int folderNumber = 1;

    while (true) {
        fs::path folderPath = directoryPath / std::to_string(folderNumber);

        try {
            if (!fs::exists(folderPath)) {
                fs::create_directory(folderPath);
                return true; // Indicate success
            } else {
                ++folderNumber;
            }
        } catch (const fs::filesystem_error& e) {
            TraceLog(LOG_ERROR, (std::string("Failed to create directory: ") + std::string(e.what())).c_str());
            return false; // Indicate failure to create the directory
        }
    }
}

bool showAddFilePopup       = false;
bool showEditFilePopup      = false;
bool showEditFolderPopup    = false;
int renameFileIndex       = -1;
int renameFolderIndex       = -1;
char renameFileBuffer[256];
char renameFolderBuffer[256];
fs::path renameFolderName;
int fileIndex            = -1;
int folderIndex          = -1;

void EditFolderManipulation() {
    if (ImGui::IsWindowHovered() && showEditFolderPopup)
        ImGui::OpenPopup("edit_folder_popup");

    if (ImGui::BeginPopup("edit_folder_popup")) {
        if (!ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showEditFolderPopup = false;

        ImGui::Text("Edit Folder");

        ImGui::Separator();

        const float buttonWidth = ImGui::GetContentRegionAvail().x;

        if (ImGui::Button("Rename", ImVec2(buttonWidth, 0))) {
            if (folderIndex != -1) {
                fs::path currentPath = fs::current_path();

                renameFolderIndex = folderIndex;

                size_t bufferSize = sizeof(renameFolderBuffer);
                const char* source = folderStruct[folderIndex].name.c_str();

                strncpy(renameFolderBuffer, source, bufferSize - 1);
                renameFolderBuffer[bufferSize - 1] = '\0';

                folderIndex = -1;
                showEditFolderPopup = false;
            }
        }

        if (ImGui::Button("Delete", ImVec2(buttonWidth, 0))) {
            if (folderIndex != -1) {
                fs::path folderPath = (fs::current_path() / folderStruct[folderIndex].full_path);
                fs::remove_all(folderPath);

                folderIndex = -1;
                showEditFolderPopup = false;
            }
        }
        
        ImGui::EndPopup();
    }
}

void EditFileManipulation()
{
    if (ImGui::IsWindowHovered() && showEditFilePopup)
        ImGui::OpenPopup("edit_file_popup");


    if (ImGui::BeginPopup("edit_file_popup")) {
        if (!ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showEditFilePopup = false;

        ImGui::Text("Edit File");

        ImGui::Separator();

        const float buttonWidth = ImGui::GetContentRegionAvail().x;

        if (ImGui::Button("Rename", ImVec2(buttonWidth, 0))) {
            if (fileIndex != -1) {
                fs::path currentPath = fs::current_path();

                renameFileIndex = fileIndex;

                size_t bufferSize = sizeof(renameFileBuffer);
                const char* source = fileStruct[fileIndex].name.c_str();

                strncpy(renameFileBuffer, source, bufferSize - 1);
                renameFileBuffer[bufferSize - 1] = '\0';

                fileIndex = -1;
                showEditFilePopup = false;
            }
        }

        if (ImGui::Button("Delete", ImVec2(buttonWidth, 0))) {
            if (fileIndex != -1) {
                fs::path currentPath = fs::current_path();

                fs::path filePath = currentPath / dirPath / fileStruct[fileIndex].name;
                fs::remove_all(filePath);

                fileIndex = -1;
                showEditFilePopup = false;
            }
        }

        if (!showEditFilePopup) {
            ImGui::EndPopup();
            return;
        }

        std::string file_extension = getFileExtension(fileStruct[fileIndex].path.filename().string());
        if (file_extension == ".py") {
            if (ImGui::Button("Run", ImVec2(buttonWidth, 0))) {
                entitiesList.assign(entitiesListPregame.begin(), entitiesListPregame.end());

                Entity run_script_entity = Entity();
                run_script_entity.script = fileStruct[fileIndex].full_path.string();

                LitCamera* sceneCamera_reference = &sceneCamera;
                run_script_entity.setupScript(sceneCamera_reference);
                run_script_entity.runScript(sceneCamera_reference);

                fileIndex = -1;
                showEditFilePopup = false;
                showAddFilePopup = false;
            }
        }

        ImGui::EndPopup();
    }
}

void AddFileManipulation()
{
    if (ImGui::IsWindowHovered() && showAddFilePopup)
        ImGui::OpenPopup("popup");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

    if (ImGui::BeginPopup("popup", ImGuiWindowFlags_NoTitleBar)) {
        if (!ImGui::IsItemHovered() && !ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showAddFilePopup = false;

        ImGui::Text("Add");
        ImGui::Separator();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Python")) {
                createNumberedFile(dirPath, "py");
                showAddFilePopup = false;
            }

            if (ImGui::MenuItem("Material")) {
                createNumberedFile(dirPath, "mat");
                showAddFilePopup = false;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Folder")) {
            if (ImGui::MenuItem("Folder")) {
                createNumberedFolder(dirPath);
                showAddFilePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
}


#endif // FILE_MANIPULATION_H