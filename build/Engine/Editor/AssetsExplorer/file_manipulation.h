#ifndef FILE_MANIPULATION_H
#define FILE_MANIPULATION_H

#include "../../../include_all.h"
std::string generateNumberedFileName(const fs::path& directoryPath, const std::string& extension) {
    int fileNumber = 1;
    fs::path filePath;

    do {
        // Construct the file name with the number and extension
        std::string fileName = "file" + std::to_string(fileNumber) + "." + extension;
        filePath = directoryPath / fileName;
        fileNumber++;
    } while (fs::exists(filePath));

    return filePath.string();
}

bool createNumberedFile(const fs::path& directoryPath, const std::string& extension) {
    // Generate the numbered file name
    std::string filePathString = generateNumberedFileName(directoryPath, extension);

    // Create the file
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


bool showAddFilePopup = false;
bool showEditFilePopup = false;
int rename_file_index = -1;
const char* rename_file_name;
int file_index = -1;

void EditFileManipulation()
{
    if (ImGui::IsWindowHovered() && showEditFilePopup)
        ImGui::OpenPopup("edit_popup");


    if (ImGui::BeginPopup("edit_popup"))
    {
        if (!ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showEditFilePopup = false;

        ImGui::Text("Edit File");

        ImGui::Separator();

        if (ImGui::Button("Rename"))
        {
            if (file_index != -1)
            {
                rename_file_index = file_index;
                rename_file_name = files_texture_struct[file_index].full_path.string().c_str();
                showEditFilePopup = false;
            }
        }
        if (ImGui::Button("Run"))
        {
            string file_extension = getFileExtension(files_texture_struct[file_index].path.filename().string());
            if (file_extension == ".py")
            {
                Entity run_script_entity = Entity();
                run_script_entity.script = files_texture_struct[file_index].full_path.string();

                LitCamera* scene_camera_reference = &scene_camera;
                run_script_entity.setupScript(scene_camera_reference);
                run_script_entity.runScript(scene_camera_reference);


                showEditFilePopup = false;
            }
        }

        ImGui::EndPopup();
    }
}
void AddFileManipulation()
{
    if (ImGui::IsWindowHovered() && showAddFilePopup)
        ImGui::OpenPopup("popup");


    if (ImGui::BeginPopup("popup"))
    {
        if (!ImGui::IsItemHovered() && !ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            showAddFilePopup = false;

        ImGui::Text("Add File");

        ImGui::Separator();

        if (ImGui::Button("Python"))
        {
            createNumberedFile(dir_path, "py");
            showAddFilePopup = false;
        }

        if (ImGui::Button("Material"))
        {
            createNumberedFile(dir_path, "mat");
            showAddFilePopup = false;
        }

        ImGui::EndPopup();
    }
}

#endif // FILE_MANIPULATION_H