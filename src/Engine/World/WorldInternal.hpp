#pragma once

namespace {

inline glm::mat4 composeTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

template <typename T>
bool readNumber(std::string_view& view, T& value) {
    while (!view.empty() && view.front() == ' ') view.remove_prefix(1);
    const auto [end, errorCode] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (errorCode != std::errc()) return false;
    view.remove_prefix(static_cast<size_t>(end - view.data()));
    return true;
}

template <typename T>
void appendNumber(std::string& buffer, T value) {
    constexpr size_t kNumberBufferSize = 48;
    char digits[kNumberBufferSize];
    const auto [end, errorCode] = std::to_chars(digits, digits + sizeof(digits), value);
    buffer.append(digits, end);
}

inline constexpr int kCoarseShift = 3;
inline constexpr int kCoarseSpan = 1 << kCoarseShift;

inline int cellCoordinate(float position, float cellSize) {
    constexpr float kLimit = 1.0e9f;
    return static_cast<int>(std::clamp(std::floor(position / cellSize), -kLimit, kLimit));
}

inline int decodeCellCoordinate(uint64_t key, int shift) {
    constexpr uint64_t kCoordinateMask = 0x1FFFFF;
    constexpr uint64_t kCoordinateSignBit = 0x100000;
    const uint64_t raw = (key >> shift) & kCoordinateMask;
    return static_cast<int>(raw ^ kCoordinateSignBit) - static_cast<int>(kCoordinateSignBit);
}

inline float maxAxisScale(const glm::mat4& matrix) {
    return std::max({glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2]))});
}

inline bool parseCameraValues(std::string_view values, std::array<float, 8>& out) {
    for (float& value : out) {
        if (!readNumber(values, value)) return false;
    }
    return true;
}

}
