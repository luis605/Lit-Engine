#ifndef FILESYSTEM_HPP
#define FILESYSTEM_HPP

#include <unordered_set>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

extern const std::unordered_set<std::string> imageExtensions;
extern const std::unordered_set<std::string> videoExtensions;

bool IsImageFile(const fs::path& filePath);
bool IsVideoFile(const fs::path& filePath);

#endif // FILESYSTEM_HPP