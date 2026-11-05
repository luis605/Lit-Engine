#pragma once

inline constexpr float kSpacing = 24.0f;
inline constexpr float kHalfWidth = 1.4f;
inline constexpr float kHalfHeight = 1.8f;
inline constexpr float kCellHalfX = 5.4f;
inline constexpr float kPortalOffset = 5.0f;

inline PortalEnd portalEndOf(const std::string& cell, float x, float yaw) {
    PortalEnd end;
    end.cell = cell;
    end.position = glm::vec3(x, kHalfHeight, 0.0f);
    end.eulerDegrees = glm::vec3(yaw, 0.0f, 0.0f);
    end.halfSize = glm::vec2(kHalfWidth, kHalfHeight);
    return end;
}

inline void buildPortalLine(PortalSystem& portals, int cellCount) {
    for (int index = 0; index < cellCount; ++index) {
        PortalCell cell;
        cell.name = "C" + std::to_string(index);
        cell.min = glm::vec3(kSpacing * static_cast<float>(index) - kCellHalfX, -0.4f, -kCellHalfX);
        cell.max = glm::vec3(kSpacing * static_cast<float>(index) + kCellHalfX, 4.4f, kCellHalfX);
        portals.addCell(cell);
    }
    for (int index = 0; index + 1 < cellCount; ++index) {
        const float x = kSpacing * static_cast<float>(index);
        portals.addPair("P" + std::to_string(index), portalEndOf("C" + std::to_string(index), x + kPortalOffset, -90.0f), portalEndOf("C" + std::to_string(index + 1), x + kSpacing - kPortalOffset, 90.0f));
    }
}
