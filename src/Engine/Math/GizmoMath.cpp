module;

#include <algorithm>
#include <cmath>
#include <optional>

module Engine.GizmoMath;

namespace gizmo {

float closestParameterOnAxis(const Ray& ray, const glm::vec3& origin, const glm::vec3& axis) {
    const glm::vec3 w0 = ray.origin - origin;
    const float a = glm::dot(ray.direction, ray.direction);
    const float b = glm::dot(ray.direction, axis);
    const float c = glm::dot(axis, axis);
    const float d = glm::dot(ray.direction, w0);
    const float e = glm::dot(axis, w0);
    const float denom = a * c - b * b;
    if (std::abs(denom) < 1.0e-6f) return 0.0f;
    return (a * e - b * d) / denom;
}

std::optional<glm::vec3> rayPlane(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& planeNormal) {
    const float denom = glm::dot(planeNormal, ray.direction);
    if (std::abs(denom) < 1.0e-6f) return std::nullopt;
    const float t = glm::dot(planeNormal, planePoint - ray.origin) / denom;
    if (t < 0.0f) return std::nullopt;
    return ray.origin + ray.direction * t;
}

float signedAngleAroundAxis(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis) {
    const glm::vec3 f = glm::normalize(from - axis * glm::dot(from, axis));
    const glm::vec3 t = glm::normalize(to - axis * glm::dot(to, axis));
    const float cosine = std::clamp(glm::dot(f, t), -1.0f, 1.0f);
    const float sine = glm::dot(glm::cross(f, t), axis);
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
    glm::vec3 s(1.0f);
    if (uniform) {
        s = glm::vec3(factor);
    } else if (axisIndex >= 0 && axisIndex < 3) {
        s[axisIndex] = factor;
    }
    return local * glm::scale(glm::mat4(1.0f), s);
}

float distanceToSegment2D(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 1.0e-6f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t));
}

}
