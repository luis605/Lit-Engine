#include "include_all.h"

// Function to read and serialize a Python script file
std::string serializePythonScript(const std::string &scriptFilePath) {
    std::ifstream inputFile(scriptFilePath);
    if (!inputFile.is_open()) {
        std::cerr << "Failed to open file: " << scriptFilePath << std::endl;
        return "";
    }

    std::string scriptContent((std::istreambuf_iterator<char>(inputFile)),
                              std::istreambuf_iterator<char>());
    inputFile.close();

    return scriptContent;
}

int main() {
    // Initialize the Python interpreter
    int file_id = 0;

    Entity entity1;
    entity1.script = "project/game/test.py";
    Entity entity2;
    entity2.script = "project/game/file2.py";
    Entity entity3;
    entity3.script = "project/game/hi.py";

    entities_list_pregame.push_back(entity1);
    entities_list_pregame.push_back(entity2);
    entities_list_pregame.push_back(entity3);    

    // Map to store individual script contents
    std::map<std::string, std::string> scriptContents;

    for (Entity& entity : entities_list_pregame) {
        if (entity.script.empty()) continue;

        std::string scriptContent = serializePythonScript(entity.script);
        if (scriptContent.empty()) continue;

        // Extract the script name from the file path (e.g., "test.py")
        std::string scriptName = entity.script.substr(entity.script.find_last_of('/') + 1);
        // Remove the file extension to use as a variable name (e.g., "test") and add the index
        scriptName = scriptName.substr(0, scriptName.find_last_of('.'))  + std::to_string(file_id);

        // Store the script content in the map with the script name as the key
        scriptContents[scriptName] = scriptContent;
        entity.script_index = scriptName;

        file_id++;

    }

    // Generate the header file content with individual script variables
    std::string headerContent = "";
    for (const auto &entry : scriptContents) {
        headerContent += "const char* " + entry.first + " = R\"(\n";
        headerContent += entry.second + "\n)\";\n\n";
    }

    // Write the header content to a header file
    std::ofstream headerFile("ScriptData.h");
    if (headerFile.is_open()) {
        headerFile << headerContent;
        headerFile.close();
        std::cout << "Header file 'ScriptData.h' generated successfully." << std::endl;
    } else {
        std::cerr << "Failed to create header file 'ScriptData.h'." << std::endl;
    }

    for (Entity& entity : entities_list_pregame) {
        std::cout << "Script Index: " << entity.script_index << std::endl;
    }

    return 0;
}
