module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

module Engine.IK.Solvers;

import Engine.glm;

namespace {

constexpr float kEpsilon = 1.0e-6f;
constexpr float kParallelEpsilon = 1.0e-5f;
constexpr float kUnreachableSlack = 1.0e-4f;
constexpr int kMaxIterations = 256;

bool isFinite(const glm::vec3& vector) { return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z); }

size_t slot(int index) { return static_cast<size_t>(index); }

IkSettings sanitised(IkSettings settings) {
    if (settings.hasPole && !isFinite(settings.pole)) settings.hasPole = false;
    if (!isFinite(settings.baseDirection)) settings.baseDirection = glm::vec3(0.0f);
    settings.iterations = std::clamp(settings.iterations, 1, kMaxIterations);
    return settings;
}

glm::vec3 safeNormalize(const glm::vec3& vector, const glm::vec3& fallback) {
    const float length = glm::length(vector);
    return length < kEpsilon ? fallback : vector / length;
}

glm::vec3 anyPerpendicular(const glm::vec3& direction) {
    const glm::vec3 candidate = std::abs(direction.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(direction, candidate));
}

void stretchTowards(IkChainState& chain, const glm::vec3& target) {
    const glm::vec3 direction = safeNormalize(target - chain.root(), glm::vec3(0.0f, -1.0f, 0.0f));
    float travelled = 0.0f;
    for (int bone = 0; bone + 1 < chain.count; ++bone) {
        travelled += chain.lengths[slot(bone)];
        chain.points[slot(bone + 1)] = chain.root() + direction * travelled;
    }
}

glm::vec3 boneDirection(const IkChainState& chain, int bone) {
    const glm::vec3 offset = chain.points[slot(bone + 1)] - chain.points[slot(bone)];
    return safeNormalize(offset, glm::vec3(0.0f, -1.0f, 0.0f));
}

glm::vec3 previousDirection(const IkChainState& chain, int bone, const glm::vec3& base) {
    if (bone == 0) return base;
    return boneDirection(chain, bone - 1);
}

}

glm::quat ikRotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const float fromLength = glm::length(from);
    const float toLength = glm::length(to);
    if (fromLength < kEpsilon || toLength < kEpsilon) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 fromUnit = from / fromLength;
    const glm::vec3 toUnit = to / toLength;
    const float cosine = glm::dot(fromUnit, toUnit);
    const glm::vec3 axis = glm::cross(fromUnit, toUnit);
    const float sine = glm::length(axis);
    if (sine < kParallelEpsilon) {
        if (cosine > 0.0f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return glm::angleAxis(kIkPi, anyPerpendicular(fromUnit));
    }
    return glm::angleAxis(std::atan2(sine, cosine), axis / sine);
}

glm::vec3 ikRotateAround(const glm::vec3& vector, const glm::vec3& axis, float radians) {
    return glm::angleAxis(radians, glm::normalize(axis)) * vector;
}

float ikSignedAngle(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis) {
    const glm::vec3 cross = glm::cross(from, to);
    return std::atan2(glm::dot(cross, axis), glm::dot(from, to));
}

glm::vec3 ikConstrainDirection(const glm::vec3& previous, const glm::vec3& direction, const IkJointLimit& limit, const glm::vec3& bendAxis) {
    if (glm::length(previous) < kEpsilon) return direction;
    if (limit.hinge) {
        const glm::vec3 axis = glm::normalize(bendAxis);
        const glm::vec3 planarPrevious = previous - axis * glm::dot(previous, axis);
        const glm::vec3 planarDirection = direction - axis * glm::dot(direction, axis);
        if (glm::length(planarPrevious) < kEpsilon) return direction;
        const glm::vec3 reference = glm::normalize(planarPrevious);
        if (glm::length(planarDirection) < kEpsilon) return reference;
        const float angle = std::clamp(ikSignedAngle(reference, glm::normalize(planarDirection), axis), limit.minAngle, limit.maxAngle);
        return ikRotateAround(reference, axis, angle);
    }
    if (limit.cone >= kIkPi) return direction;
    const float cosine = std::clamp(glm::dot(previous, direction), -1.0f, 1.0f);
    if (cosine >= std::cos(limit.cone)) return direction;
    glm::vec3 sideways = direction - previous * cosine;
    sideways = glm::length(sideways) < kEpsilon ? anyPerpendicular(previous) : glm::normalize(sideways);
    return glm::normalize(previous * std::cos(limit.cone) + sideways * std::sin(limit.cone));
}

void ikMeasureLengths(IkChainState& chain) {
    for (int bone = 0; bone + 1 < chain.count; ++bone) chain.lengths[slot(bone)] = glm::length(chain.points[slot(bone + 1)] - chain.points[slot(bone)]);
}

float ikChainReach(const IkChainState& chain) {
    float reach = 0.0f;
    for (int bone = 0; bone + 1 < chain.count; ++bone) reach += chain.lengths[slot(bone)];
    return reach;
}

void ikResolveBendAxis(IkChainState& chain, const glm::vec3& target, const IkSettings& settings) {
    const glm::vec3 axisLine = safeNormalize(target - chain.root(), glm::vec3(0.0f, -1.0f, 0.0f));
    glm::vec3 offset(0.0f);
    if (settings.hasPole) {
        const glm::vec3 toPole = settings.pole - chain.root();
        offset = toPole - axisLine * glm::dot(toPole, axisLine);
    }
    float largest = glm::length(offset);
    for (int index = 1; index + 1 < chain.count && largest < kEpsilon; ++index) {
        const glm::vec3 toPoint = chain.points[slot(index)] - chain.root();
        const glm::vec3 candidate = toPoint - axisLine * glm::dot(toPoint, axisLine);
        if (glm::length(candidate) > largest) {
            largest = glm::length(candidate);
            offset = candidate;
        }
    }
    if (largest < kEpsilon) offset = anyPerpendicular(axisLine);
    chain.bendAxis = glm::normalize(glm::cross(axisLine, offset));
}

void ikApplyPole(IkChainState& chain, const glm::vec3& pole) {
    if (chain.count < 3) return;
    const glm::vec3 axis = safeNormalize(chain.tip() - chain.root(), glm::vec3(0.0f, -1.0f, 0.0f));
    const glm::vec3 toPole = pole - chain.root();
    const glm::vec3 poleSide = toPole - axis * glm::dot(toPole, axis);
    if (glm::length(poleSide) < kEpsilon) return;
    for (int index = 1; index + 1 < chain.count; ++index) {
        const glm::vec3 offset = chain.points[slot(index)] - chain.root();
        const glm::vec3 side = offset - axis * glm::dot(offset, axis);
        if (glm::length(side) < kEpsilon) continue;
        const float angle = ikSignedAngle(side, poleSide, axis);
        chain.points[slot(index)] = chain.root() + ikRotateAround(offset, axis, angle);
    }
}

void ikRebuildFromDirections(IkChainState& chain, const glm::vec3* directions) {
    for (int bone = 0; bone + 1 < chain.count; ++bone) chain.points[slot(bone + 1)] = chain.points[slot(bone)] + directions[bone] * chain.lengths[slot(bone)];
}

IkResult solveFabrik(IkChainState& chain, const glm::vec3& target, const IkSettings& rawSettings) {
    IkResult result;
    if (chain.count < 2 || !isFinite(target)) return result;
    const IkSettings settings = sanitised(rawSettings);
    const int last = chain.count - 1;
    const glm::vec3 root = chain.root();
    ikResolveBendAxis(chain, target, settings);
    if (settings.hasPole) ikApplyPole(chain, settings.pole);
    const float reach = ikChainReach(chain);
    const bool unreachable = glm::length(target - root) >= reach - kUnreachableSlack;
    const int passes = unreachable ? 1 : std::max(1, settings.iterations);
    for (int pass = 0; pass < passes; ++pass) {
        result.iterations = pass + 1;
        if (unreachable) {
            stretchTowards(chain, target);
        } else {
            chain.points[slot(last)] = target;
            for (int index = last - 1; index >= 0; --index) {
                const glm::vec3 direction = safeNormalize(chain.points[slot(index)] - chain.points[slot(index + 1)], glm::vec3(0.0f, 1.0f, 0.0f));
                chain.points[slot(index)] = chain.points[slot(index + 1)] + direction * chain.lengths[slot(index)];
            }
        }
        chain.points[0] = root;
        for (int bone = 0; bone < last; ++bone) {
            glm::vec3 direction = safeNormalize(chain.points[slot(bone + 1)] - chain.points[slot(bone)], glm::vec3(0.0f, -1.0f, 0.0f));
            direction = ikConstrainDirection(previousDirection(chain, bone, settings.baseDirection), direction, chain.limits[slot(bone)], chain.bendAxis);
            chain.points[slot(bone + 1)] = chain.points[slot(bone)] + direction * chain.lengths[slot(bone)];
        }
        result.error = glm::length(chain.tip() - target);
        if (result.error < settings.tolerance) break;
    }
    result.reached = result.error < settings.tolerance;
    return result;
}

IkResult solveIk(IkChainState& chain, const glm::vec3& target, const IkSettings& settings) {
    bool anyHinge = false;
    for (int bone = 0; bone + 1 < chain.count; ++bone) anyHinge = anyHinge || chain.limits[slot(bone)].hinge;
    if (chain.count == 3 && !anyHinge) return solveTwoBoneAnalytic(chain, target, settings);
    return solveFabrik(chain, target, settings);
}

namespace {

constexpr float kReachMargin = 1.0e-4f;
constexpr float kStallDelta = 1.0e-7f;

void rotateTail(IkChainState& chain, int firstPoint, const glm::vec3& pivot, const glm::quat& rotation) {
    for (int index = firstPoint; index < chain.count; ++index) chain.points[slot(index)] = pivot + rotation * (chain.points[slot(index)] - pivot);
}

void constrainSweep(IkChainState& chain, const IkSettings& settings) {
    for (int bone = 0; bone + 1 < chain.count; ++bone) {
        const glm::vec3 direction = boneDirection(chain, bone);
        const glm::vec3 allowed = ikConstrainDirection(previousDirection(chain, bone, settings.baseDirection), direction, chain.limits[slot(bone)], chain.bendAxis);
        rotateTail(chain, bone + 1, chain.points[slot(bone)], ikRotationBetween(direction, allowed));
    }
}

}

IkResult solveCcd(IkChainState& chain, const glm::vec3& target, const IkSettings& rawSettings) {
    IkResult result;
    if (chain.count < 2 || !isFinite(target)) return result;
    const IkSettings settings = sanitised(rawSettings);
    ikResolveBendAxis(chain, target, settings);
    if (settings.hasPole) ikApplyPole(chain, settings.pole);
    float previousError = 1.0e30f;
    for (int pass = 0; pass < std::max(1, settings.iterations); ++pass) {
        result.iterations = pass + 1;
        for (int joint = chain.count - 2; joint >= 0; --joint) {
            const glm::vec3 pivot = chain.points[slot(joint)];
            rotateTail(chain, joint + 1, pivot, ikRotationBetween(chain.tip() - pivot, target - pivot));
            const glm::vec3 direction = boneDirection(chain, joint);
            const glm::vec3 allowed = ikConstrainDirection(previousDirection(chain, joint, settings.baseDirection), direction, chain.limits[slot(joint)], chain.bendAxis);
            rotateTail(chain, joint + 1, pivot, ikRotationBetween(direction, allowed));
        }
        constrainSweep(chain, settings);
        result.error = glm::length(chain.tip() - target);
        if (result.error < settings.tolerance || std::abs(previousError - result.error) < kStallDelta) break;
        previousError = result.error;
    }
    result.reached = result.error < settings.tolerance;
    return result;
}

IkResult solveTwoBoneAnalytic(IkChainState& chain, const glm::vec3& target, const IkSettings& rawSettings) {
    IkResult result;
    if (chain.count != 3) return solveFabrik(chain, target, rawSettings);
    if (!isFinite(target)) return result;
    const IkSettings settings = sanitised(rawSettings);
    const float upper = chain.lengths[0];
    const float lower = chain.lengths[1];
    const glm::vec3 root = chain.root();
    const glm::vec3 toTarget = target - root;
    const float rawDistance = glm::length(toTarget);
    if (rawDistance < kEpsilon || upper < kEpsilon || lower < kEpsilon) return result;
    const float minimum = std::abs(upper - lower) + kReachMargin;
    const float maximum = std::max(minimum, upper + lower - kReachMargin);
    const float distance = std::clamp(rawDistance, minimum, maximum);
    const glm::vec3 direction = toTarget / rawDistance;
    glm::vec3 hint = (settings.hasPole ? settings.pole : chain.points[1]) - root;
    hint -= direction * glm::dot(hint, direction);
    hint = glm::length(hint) < kParallelEpsilon ? anyPerpendicular(direction) : glm::normalize(hint);
    const float cosine = std::clamp((upper * upper + distance * distance - lower * lower) / (2.0f * upper * distance), -1.0f, 1.0f);
    const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    chain.points[1] = root + direction * (upper * cosine) + hint * (upper * sine);
    chain.points[2] = root + direction * distance;
    chain.bendAxis = glm::normalize(glm::cross(direction, hint));
    result.iterations = 1;
    result.error = glm::length(chain.tip() - target);
    result.reached = result.error < settings.tolerance;
    return result;
}
