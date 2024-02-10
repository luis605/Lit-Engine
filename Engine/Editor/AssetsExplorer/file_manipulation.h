#ifndef FILE_MANIPULATION_H
#define FILE_MANIPULATION_H

#include "../../../include_all.h"
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
        std::cout << "File created: " << filePathString << std::endl;
        return true;
    } else {
        std::cerr << "Failed to create the file: " << filePathString << std::endl;
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
                std::cout << "Directory created successfully.\n";
                return true; // Indicate success
            } else {
                std::cout << "Directory already exists.\n";
                // Increment folderNumber for the next iteration
                ++folderNumber;
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating directory: " << e.what() << '\n';
            return false; // Indicate failure to create the directory
        }
    }
}

bool showAddFilePopup       = false;
bool showEditFilePopup      = false;
bool showEditFolderPopup    = false;
int rename_file_index       = -1;
int rename_folder_index       = -1;
std::filesystem::path rename_file_name;
std::filesystem::path rename_folder_name;
int file_index            = -1;
int folder_index          = -1;

void EditFolderManipulation()
{
    if (ImGui::IsWindowHovered() && showEditFolderPopup)
        ImGui::OpenPopup("edit_folder_popup");


    if (ImGui::BeginPopup("edit_folder_popup"))
    {
        if (!ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showEditFolderPopup = false;

        ImGui::Text("Edit Folder");

        ImGui::Separator();

        if (ImGui::Button("Rename"))
        {
            if (folder_index != -1)
            {
                std::filesystem::path currentPath = std::filesystem::current_path();

                rename_folder_index = folder_index;
                rename_folder_name = (currentPath / folders_texture_struct[folder_index].full_path);
                strcpy(rename_file_buffer, folders_texture_struct[folder_index].name.c_str());

                showEditFolderPopup = false;
            }
        }

        if (ImGui::Button("Delete"))
        {
            if (folder_index != -1)
            {
                std::filesystem::path currentPath = std::filesystem::current_path();

                std::filesystem::path folderPath = (currentPath / folders_texture_struct[folder_index].full_path);
                fs::remove_all(folderPath);

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


    if (ImGui::BeginPopup("edit_file_popup"))
    {
        if (!ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showEditFilePopup = false;

        ImGui::Text("Edit File");

        ImGui::Separator();

        const float buttonWidth = ImGui::GetContentRegionAvail().x;

        if (ImGui::Button("Rename", ImVec2(buttonWidth, 0)))
        {
            if (file_index != -1)
            {
                std::filesystem::path currentPath = std::filesystem::current_path();

                rename_file_index = file_index;
                rename_file_name = (currentPath / files_texture_struct[file_index].full_path);
                strcpy(rename_file_buffer, files_texture_struct[file_index].name.c_str());

                showEditFilePopup = false;
            }
        }

        if (ImGui::Button("Delete", ImVec2(buttonWidth, 0)))
        {
            if (file_index != -1)
            {
                std::filesystem::path currentPath = std::filesystem::current_path();

                std::filesystem::path filePath = (currentPath / files_texture_struct[file_index].full_path);
                fs::remove_all(filePath);

                showEditFilePopup = false;
            }
        }

        string file_extension = getFileExtension(files_texture_struct[file_index].path.filename().string());
        if (file_extension == ".py")
        {
            if (ImGui::Button("Run", ImVec2(buttonWidth, 0)))
            {
                    entities_list.assign(entities_list_pregame.begin(), entities_list_pregame.end());

                    Entity run_script_entity = Entity();
                    run_script_entity.script = files_texture_struct[file_index].full_path.string();

                    LitCamera* scene_camera_reference = &scene_camera;
                    run_script_entity.setupScript(scene_camera_reference);
                    run_script_entity.runScript(scene_camera_reference);

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

    if (ImGui::BeginPopup("popup", ImGuiWindowFlags_NoTitleBar))
    {
        if (!ImGui::IsItemHovered() && !ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showAddFilePopup = false;

        ImGui::Text("Add");
        ImGui::Separator();

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Python"))
            {
                createNumberedFile(dir_path, "py");
                showAddFilePopup = false;
            }

            if (ImGui::MenuItem("Material"))
            {
                createNumberedFile(dir_path, "mat");
                showAddFilePopup = false;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Folder"))
        {
            if (ImGui::MenuItem("Folder"))
            {
                createNumberedFolder(dir_path);
                showAddFilePopup = false;
            }

            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(); // Restore window padding
}


#endif // FILE_MANIPULATION_H