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

inline constexpr char kEscapedTextMarker = '\x1e';

inline void appendLineText(std::string& buffer, std::string_view text) {
    const bool needsEscape = !text.empty() && (text.front() == kEscapedTextMarker || text.find_first_of("\n\r") != std::string_view::npos);
    if (!needsEscape) {
        buffer.append(text);
        return;
    }
    buffer.push_back(kEscapedTextMarker);
    for (const char character : text) {
        if (character == '\\') buffer.append("\\\\");
        else if (character == '\n') buffer.append("\\n");
        else if (character == '\r') buffer.append("\\r");
        else buffer.push_back(character);
    }
}

inline std::string decodeLineText(std::string_view text) {
    if (text.empty() || text.front() != kEscapedTextMarker) return std::string(text);
    std::string decoded;
    decoded.reserve(text.size());
    for (size_t index = 1; index < text.size(); ++index) {
        if (text[index] == '\\' && index + 1 < text.size()) {
            const char next = text[++index];
            decoded.push_back(next == 'n' ? '\n' : next == 'r' ? '\r' : next);
        } else {
            decoded.push_back(text[index]);
        }
    }
    return decoded;
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

// "camera x y z yaw pitch fov near far [roll qw qx qy qz]": the optional tail (camera roll and reference frame, written only when
// they are not 0 / identity) defaults to an upright camera, so older scenes load unchanged.
inline bool parseCameraValues(std::string_view values, std::array<float, 13>& out) {
    for (size_t index = 0; index < 8; ++index) {
        if (!readNumber(values, out[index])) return false;
    }
    std::array<float, 5> tail{};
    bool complete = true;
    for (float& value : tail) complete = complete && readNumber(values, value);
    if (!complete) tail = {0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    for (size_t index = 0; index < tail.size(); ++index) out[8 + index] = tail[index];
    return true;
}

}
