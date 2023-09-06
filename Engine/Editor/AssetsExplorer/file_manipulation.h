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


#endif // FILE_MANIPULATION_H