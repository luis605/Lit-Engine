#pragma once

inline constexpr float kPortalPi = 3.14159265358979f;
inline constexpr float kCrossingParallelEpsilon = 1.0e-6f;

inline glm::vec3 transformPoint(const glm::mat4& matrix, const glm::vec3& point) { return glm::vec3(matrix * glm::vec4(point, 1.0f)); }

inline glm::vec3 transformDirection(const glm::mat4& matrix, const glm::vec3& direction) { return glm::vec3(matrix * glm::vec4(direction, 0.0f)); }

inline glm::vec3 cameraForward(const Camera& camera) {
    const float yaw = glm::radians(camera.getYaw());
    const float pitch = glm::radians(camera.getPitch());
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

inline std::array<glm::vec3, 4> cornersOf(const Portal& portal) {
    const glm::vec3 center = portal.center();
    const glm::vec3 horizontal = portal.right() * portal.half.x;
    const glm::vec3 vertical = portal.up() * portal.half.y;
    return {center - horizontal - vertical, center + horizontal - vertical, center + horizontal + vertical, center - horizontal + vertical};
}

inline std::optional<float> crossing(const Portal& portal, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    const glm::vec3 normal = portal.normal();
    const float denominator = glm::dot(normal, direction);
    if (denominator >= -kCrossingParallelEpsilon) return std::nullopt;
    const float distance = glm::dot(normal, portal.center() - origin) / denominator;
    if (distance < 0.0f || distance > maxDistance) return std::nullopt;
    const glm::vec3 offset = origin + direction * distance - portal.center();
    if (std::abs(glm::dot(offset, portal.right())) > portal.half.x || std::abs(glm::dot(offset, portal.up())) > portal.half.y) return std::nullopt;
    return distance;
}

inline float rectDistance(const Portal& portal, const glm::vec3& point, float& signedPlaneDistance) {
    const glm::vec3 offset = point - portal.center();
    signedPlaneDistance = glm::dot(offset, portal.normal());
    const float outsideRight = std::max(std::abs(glm::dot(offset, portal.right())) - portal.half.x, 0.0f);
    const float outsideUp = std::max(std::abs(glm::dot(offset, portal.up())) - portal.half.y, 0.0f);
    return std::sqrt(signedPlaneDistance * signedPlaneDistance + outsideRight * outsideRight + outsideUp * outsideUp);
}

inline bool cellContains(const PortalCell& cell, const glm::vec3& point) {
    return point.x >= cell.min.x && point.y >= cell.min.y && point.z >= cell.min.z && point.x <= cell.max.x && point.y <= cell.max.y && point.z <= cell.max.z;
}

inline uint64_t entityKey(EntityHandle entity) { return (static_cast<uint64_t>(entity.generation) << 32) | entity.index; }
