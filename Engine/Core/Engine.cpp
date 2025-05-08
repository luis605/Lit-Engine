/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Core/Entity.hpp>
#include <Engine/Lighting/lights.hpp>
#include "Engine.hpp"
#include <fstream>
#include <stdio.h>

std::vector<Entity> entitiesListPregame;
std::vector<Entity> entitiesList;
std::unordered_map<int, size_t> entityIdToIndexMap;
std::unordered_map<int, size_t> lightIdToIndexMap;

fs::path     selectedMaterial;
Entity*      selectedEntity      = nullptr;
LightStruct* selectedLight       = nullptr;
LitButton*   selectedButton      = nullptr;
Text*        selectedTextElement = nullptr;

const char* encryptFileString(const std::string& inputFile, const std::string& key) {
    std::ifstream inFile(inputFile, std::ios::binary);

    if (!inFile) {
        TraceLog(LOG_ERROR, (std::string("Failed to open encryption file: ") + inputFile).c_str());
        return nullptr;
    }

    size_t keyLength = key.size();
    size_t bufferSize = 4096;
    char buffer[4096];

    size_t bytesRead = 0;
    size_t keyIndex = 0;
    std::string encryptedData;

    while (inFile.good()) {
        inFile.read(buffer, bufferSize);
        bytesRead = inFile.gcount();

        for (size_t i = 0; i < bytesRead; ++i) {
            buffer[i] ^= key[keyIndex++];
            keyIndex %= keyLength;
        }

        encryptedData.append(buffer, bytesRead);
    }

    inFile.close();

    char* encryptedCString = new char[encryptedData.size() + 1];
    std::strcpy(encryptedCString, encryptedData.c_str());

    return encryptedCString;
}

const char* decryptFileString(const std::string& inputFile, const std::string& key) {
    return encryptFileString(inputFile, key);
}

std::string readFileToString(const std::string& filename) {
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

template <typename T>
int findIndexInVector(const std::vector<T>& vec, const T& value) {
    auto it = std::find(vec.begin(), vec.end(), value);
    if (it != vec.end()) {
        return std::distance(vec.begin(), it);
    } else {
        return -1; // Return -1 if the element is not found
    }
}