/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Core/Filesystem.hpp>

const std::unordered_set<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".hdr", ".pic"};
const std::unordered_set<std::string> videoExtensions = {".mp4", ".mov", ".mkv", ".webm", ".gif", ".avi"};

bool IsImageFile(const fs::path& filePath) {
    return imageExtensions.find(filePath.extension().string()) != imageExtensions.end();
}

bool IsVideoFile(const fs::path& filePath) {
    return videoExtensions.find(filePath.extension().string()) != videoExtensions.end();
}