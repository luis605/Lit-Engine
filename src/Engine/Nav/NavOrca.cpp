module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr float kEpsilon = 1.0e-5f;
constexpr float kTimeHorizon = 2.5f;
constexpr float kSharedResponsibility = 0.5f;
constexpr float kStaticSpeed = 0.15f;
constexpr float kRadiusMargin = 1.02f;
constexpr int kMaxLines = kNavMaxNeighbors;

struct Line {
    glm::vec2 point{0.0f};
    glm::vec2 direction{0.0f};
};

float determinant(const glm::vec2& first, const glm::vec2& second) { return first.x * second.y - first.y * second.x; }

glm::vec2 flat(const glm::vec3& value) { return glm::vec2(value.x, value.z); }

Line buildLine(const NavAgent& agent, const NavNeighbor& other, float inverseHorizon, float inverseStep) {
    const glm::vec2 relativePosition = flat(other.position - agent.position);
    const glm::vec2 relativeVelocity = flat(agent.velocity - other.velocity);
    const float distanceSquared = glm::dot(relativePosition, relativePosition);
    const float combinedRadius = (agent.radius + other.radius) * kRadiusMargin;
    const float combinedSquared = combinedRadius * combinedRadius;
    const bool otherStatic = glm::length(flat(other.velocity)) < kStaticSpeed;
    const float responsibility = otherStatic ? 1.0f : kSharedResponsibility;
    Line line;
    glm::vec2 correction;
    if (distanceSquared > combinedSquared) {
        const glm::vec2 cutoff = relativeVelocity - inverseHorizon * relativePosition;
        const float cutoffSquared = glm::dot(cutoff, cutoff);
        const float projection = glm::dot(cutoff, relativePosition);
        if (projection < 0.0f && projection * projection > combinedSquared * cutoffSquared) {
            const float cutoffLength = std::sqrt(cutoffSquared);
            const glm::vec2 unit = cutoff / std::max(cutoffLength, kEpsilon);
            line.direction = glm::vec2(unit.y, -unit.x);
            correction = (combinedRadius * inverseHorizon - cutoffLength) * unit;
        } else {
            const float leg = std::sqrt(distanceSquared - combinedSquared);
            if (determinant(relativePosition, cutoff) > 0.0f) {
                line.direction = glm::vec2(relativePosition.x * leg - relativePosition.y * combinedRadius, relativePosition.x * combinedRadius + relativePosition.y * leg) / distanceSquared;
            } else {
                line.direction = -glm::vec2(relativePosition.x * leg + relativePosition.y * combinedRadius, -relativePosition.x * combinedRadius + relativePosition.y * leg) / distanceSquared;
            }
            correction = glm::dot(relativeVelocity, line.direction) * line.direction - relativeVelocity;
        }
    } else {
        const glm::vec2 cutoff = relativeVelocity - inverseStep * relativePosition;
        const float cutoffLength = std::sqrt(glm::dot(cutoff, cutoff));
        const glm::vec2 unit = cutoff / std::max(cutoffLength, kEpsilon);
        line.direction = glm::vec2(unit.y, -unit.x);
        correction = (combinedRadius * inverseStep - cutoffLength) * unit;
    }
    line.point = flat(agent.velocity) + responsibility * correction;
    return line;
}

bool solveOnLine(const Line* lines, int lineIndex, float radius, const glm::vec2& optimal, bool maximizeDirection, glm::vec2& result) {
    const Line& line = lines[lineIndex];
    const float dotProduct = glm::dot(line.point, line.direction);
    const float discriminant = dotProduct * dotProduct + radius * radius - glm::dot(line.point, line.point);
    if (discriminant < 0.0f) return false;
    const float root = std::sqrt(discriminant);
    float left = -dotProduct - root;
    float right = -dotProduct + root;
    for (int other = 0; other < lineIndex; ++other) {
        const float denominator = determinant(line.direction, lines[other].direction);
        const float numerator = determinant(lines[other].direction, line.point - lines[other].point);
        if (std::fabs(denominator) <= kEpsilon) {
            if (numerator < 0.0f) return false;
            continue;
        }
        const float bound = numerator / denominator;
        if (denominator >= 0.0f) right = std::min(right, bound);
        else left = std::max(left, bound);
        if (left > right) return false;
    }
    if (maximizeDirection) {
        result = glm::dot(optimal, line.direction) > 0.0f ? line.point + right * line.direction : line.point + left * line.direction;
    } else {
        const float along = glm::dot(line.direction, optimal - line.point);
        result = line.point + std::clamp(along, left, right) * line.direction;
    }
    return true;
}

int solvePlane(const Line* lines, int count, float radius, const glm::vec2& optimal, bool maximizeDirection, glm::vec2& result) {
    if (maximizeDirection) result = optimal * radius;
    else if (glm::dot(optimal, optimal) > radius * radius) result = glm::normalize(optimal) * radius;
    else result = optimal;
    for (int index = 0; index < count; ++index) {
        if (determinant(lines[index].direction, lines[index].point - result) <= 0.0f) continue;
        const glm::vec2 previous = result;
        if (!solveOnLine(lines, index, radius, optimal, maximizeDirection, result)) {
            result = previous;
            return index;
        }
    }
    return count;
}

void relaxInfeasible(const Line* lines, int count, int firstFailed, float radius, glm::vec2& result) {
    float distance = 0.0f;
    for (int index = firstFailed; index < count; ++index) {
        if (determinant(lines[index].direction, lines[index].point - result) <= distance) continue;
        Line projected[kMaxLines];
        int projectedCount = 0;
        for (int other = 0; other < index; ++other) {
            Line line;
            const float cross = determinant(lines[index].direction, lines[other].direction);
            if (std::fabs(cross) <= kEpsilon) {
                if (glm::dot(lines[index].direction, lines[other].direction) > 0.0f) continue;
                line.point = 0.5f * (lines[index].point + lines[other].point);
            } else {
                line.point = lines[index].point + (determinant(lines[other].direction, lines[index].point - lines[other].point) / cross) * lines[index].direction;
            }
            line.direction = glm::normalize(lines[other].direction - lines[index].direction);
            projected[projectedCount++] = line;
        }
        const glm::vec2 previous = result;
        if (solvePlane(projected, projectedCount, radius, glm::vec2(-lines[index].direction.y, lines[index].direction.x), true, result) < projectedCount) result = previous;
        distance = determinant(lines[index].direction, lines[index].point - result);
    }
}
}

glm::vec3 navOrcaVelocity(const NavAgent& agent, const glm::vec3& preferred, const NavNeighborSet& set, float dt) {
    Line lines[kMaxLines];
    const float inverseHorizon = 1.0f / kTimeHorizon;
    const float inverseStep = 1.0f / std::max(dt, kEpsilon);
    for (int index = 0; index < set.count; ++index) lines[index] = buildLine(agent, set.items[index], inverseHorizon, inverseStep);
    glm::vec2 result;
    const int failed = solvePlane(lines, set.count, agent.maxSpeed, flat(preferred), false, result);
    if (failed < set.count) relaxInfeasible(lines, set.count, failed, agent.maxSpeed, result);
    return glm::vec3(result.x, 0.0f, result.y);
}
