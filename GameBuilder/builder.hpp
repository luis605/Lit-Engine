#ifndef BUILDER_H
#define BUILDER_H

#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>

void encryptFile(const std::string& inputFile, const std::string& outputFile, const std::string& key);
inline void decryptFile(const std::string& inputFile, const std::string& outputFile, const std::string& key);
void createFolder(std::string directoryName);
int BuildProject();

#endif // BUILDER_H