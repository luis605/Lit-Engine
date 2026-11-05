module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <optional>

module Engine.GizmoMath;

namespace gizmo {

namespace {
constexpr float kParallelEpsilon = 1.0e-6f;
constexpr float kMinFrameHalfAngle = 0.01f;
constexpr float kFrameMargin = 1.1f;
constexpr float kDegenerateLengthSquared = 1.0e-12f;
}

float closestParameterOnAxis(const Ray& ray, const glm::vec3& origin, const glm::vec3& axis) {
    const glm::vec3 originOffset = ray.origin - origin;
    const float rayLengthSquared = glm::dot(ray.direction, ray.direction);
    const float rayAxisDot = glm::dot(ray.direction, axis);
    const float axisLengthSquared = glm::dot(axis, axis);
    const float rayOffsetDot = glm::dot(ray.direction, originOffset);
    const float axisOffsetDot = glm::dot(axis, originOffset);
    const float denominator = rayLengthSquared * axisLengthSquared - rayAxisDot * rayAxisDot;
    if (std::abs(denominator) < kParallelEpsilon) return 0.0f;
    return (rayLengthSquared * axisOffsetDot - rayAxisDot * rayOffsetDot) / denominator;
}

std::optional<glm::vec3> rayPlane(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& planeNormal) {
    const float denominator = glm::dot(planeNormal, ray.direction);
    if (std::abs(denominator) < kParallelEpsilon) return std::nullopt;
    const float distance = glm::dot(planeNormal, planePoint - ray.origin) / denominator;
    if (distance < 0.0f) return std::nullopt;
    return ray.origin + ray.direction * distance;
}

float signedAngleAroundAxis(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis) {
    const glm::vec3 projectedFrom = from - axis * glm::dot(from, axis);
    const glm::vec3 projectedTo = to - axis * glm::dot(to, axis);
    if (glm::dot(projectedFrom, projectedFrom) < kDegenerateLengthSquared || glm::dot(projectedTo, projectedTo) < kDegenerateLengthSquared) return 0.0f;
    const glm::vec3 fromDirection = glm::normalize(projectedFrom);
    const glm::vec3 toDirection = glm::normalize(projectedTo);
    const float cosine = std::clamp(glm::dot(fromDirection, toDirection), -1.0f, 1.0f);
    const float sine = glm::dot(glm::cross(fromDirection, toDirection), axis);
    return std::atan2(sine, cosine);
}

float snap(float value, float step) {
    if (step <= 0.0f) return value;
    return std::round(value / step) * step;
}

glm::mat4 rotateAboutWorldAxis(const glm::mat4& world, const glm::vec3& pivot, const glm::vec3& axis, float radians) {
    const glm::mat4 rotation = glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), radians, glm::normalize(axis)) * glm::translate(glm::mat4(1.0f), -pivot);
    return rotation * world;
}

glm::mat4 scaleAlongLocalAxis(const glm::mat4& local, int axisIndex, float factor, bool uniform) {
    glm::vec3 scale(1.0f);
    if (uniform) {
        scale = glm::vec3(factor);
    } else if (axisIndex >= 0 && axisIndex < 3) {
        scale[axisIndex] = factor;
    }
    return local * glm::scale(glm::mat4(1.0f), scale);
}

glm::vec4 enclosingSphere(const std::vector<glm::vec4>& spheres) {
    if (spheres.empty()) return glm::vec4(0.0f);
    glm::vec3 lower(std::numeric_limits<float>::max());
    glm::vec3 upper(std::numeric_limits<float>::lowest());
    for (const glm::vec4& sphere : spheres) {
        lower = glm::min(lower, glm::vec3(sphere) - glm::vec3(sphere.w));
        upper = glm::max(upper, glm::vec3(sphere) + glm::vec3(sphere.w));
    }
    const glm::vec3 center = (lower + upper) * 0.5f;
    float radius = 0.0f;
    for (const glm::vec4& sphere : spheres) radius = std::max(radius, glm::length(glm::vec3(sphere) - center) + sphere.w);
    return glm::vec4(center, radius);
}

float frameDistance(float radius, float fovYDegrees, float aspect) {
    const float halfVertical = glm::radians(fovYDegrees) * 0.5f;
    const float halfHorizontal = std::atan(std::tan(halfVertical) * aspect);
    const float halfAngle = std::max(std::min(halfHorizontal, halfVertical), kMinFrameHalfAngle);
    return radius / std::sin(halfAngle) * kFrameMargin;
}

float distanceToSegment2D(const glm::vec2& point, const glm::vec2& segmentStart, const glm::vec2& segmentEnd) {
    const glm::vec2 segment = segmentEnd - segmentStart;
    const float lengthSquared = segment.x * segment.x + segment.y * segment.y;
    const float along = lengthSquared > kParallelEpsilon ? std::clamp(((point.x - segmentStart.x) * segment.x + (point.y - segmentStart.y) * segment.y) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    return glm::length(point - (segmentStart + segment * along));
}

}
