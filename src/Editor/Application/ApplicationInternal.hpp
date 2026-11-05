#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* kAutosavePath = "resources/autosave.litscene";

inline constexpr float kStatsTopOffset = 122.0f;
inline constexpr float kZenTopOffset = 12.0f;
inline constexpr int kStatsLineCount = 5;
inline constexpr float kStatsLineSpacing = 2.0f;
inline constexpr float kStatsPadding = 14.0f;

inline constexpr int kWalkParent = 2;
inline constexpr int kWalkChild = -2;
inline constexpr int kWalkNext = 1;
inline constexpr int kWalkPrevious = -1;

inline constexpr size_t kSelectionLimit = 100000;

inline size_t envSize(const char* name, size_t fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    return static_cast<size_t>(std::strtoull(value, nullptr, 10));
}

inline double elapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

inline std::optional<uint32_t> findNameIndex(const std::vector<std::string>& names, const std::string& wanted) {
    for (uint32_t index = 0; index < names.size(); ++index) {
        if (names[index] == wanted) return index;
    }
    return std::nullopt;
}
