/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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