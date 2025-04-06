#include <Engine/Core/Filesystem.hpp>

const std::unordered_set<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".hdr", ".pic"};
const std::unordered_set<std::string> videoExtensions = {".mp4", ".mov", ".mkv", ".webm", ".gif", ".avi"};

bool IsImageFile(const fs::path& filePath) {
    return imageExtensions.find(filePath.extension().string()) != imageExtensions.end();
}

bool IsVideoFile(const fs::path& filePath) {
    return videoExtensions.find(filePath.extension().string()) != videoExtensions.end();
}