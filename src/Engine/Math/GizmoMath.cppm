module;

#include <optional>

export module Engine.GizmoMath;

import Engine.World;
import Engine.glm;

export namespace gizmo {

float closestParameterOnAxis(const Ray& ray, const glm::vec3& origin, const glm::vec3& axis);
std::optional<glm::vec3> rayPlane(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& planeNormal);
float signedAngleAroundAxis(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis);
float snap(float value, float step);
glm::mat4 rotateAboutWorldAxis(const glm::mat4& world, const glm::vec3& pivot, const glm::vec3& axis, float radians);
glm::mat4 scaleAlongLocalAxis(const glm::mat4& local, int axisIndex, float factor, bool uniform);
float distanceToSegment2D(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b);

}
