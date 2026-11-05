module;

#include <array>
#include <cstddef>
#include <cstdint>

export module Engine.IK.Solvers;

import Engine.glm;

export inline constexpr int kMaxIkJoints = 12;
export inline constexpr float kIkPi = 3.14159265358979f;

export struct IkJointLimit {
    bool hinge = false;
    float minAngle = -kIkPi;
    float maxAngle = kIkPi;
    float cone = kIkPi;
};

export struct IkSettings {
    int iterations = 12;
    float tolerance = 1.0e-3f;
    bool hasPole = false;
    glm::vec3 pole{0.0f};
    glm::vec3 baseDirection{0.0f};
};

export struct IkResult {
    float error = 0.0f;
    int iterations = 0;
    bool reached = false;
};

export struct IkChainState {
    int count = 0;
    std::array<glm::vec3, kMaxIkJoints> points{};
    std::array<float, kMaxIkJoints> lengths{};
    std::array<IkJointLimit, kMaxIkJoints> limits{};
    glm::vec3 bendAxis{0.0f, 0.0f, 1.0f};

    inline void setPoints(const glm::vec3* source, int pointCount) {
        count = pointCount > kMaxIkJoints ? kMaxIkJoints : pointCount;
        for (int index = 0; index < count; ++index) points[static_cast<size_t>(index)] = source[index];
    }
    [[nodiscard]] inline const glm::vec3& root() const { return points[0]; }
    [[nodiscard]] inline const glm::vec3& tip() const { return points[static_cast<size_t>(count - 1)]; }
};

export glm::quat ikRotationBetween(const glm::vec3& from, const glm::vec3& to);
export glm::vec3 ikRotateAround(const glm::vec3& vector, const glm::vec3& axis, float radians);
export float ikSignedAngle(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis);
export glm::vec3 ikConstrainDirection(const glm::vec3& previous, const glm::vec3& direction, const IkJointLimit& limit, const glm::vec3& bendAxis);
export void ikMeasureLengths(IkChainState& chain);
export float ikChainReach(const IkChainState& chain);
export void ikResolveBendAxis(IkChainState& chain, const glm::vec3& target, const IkSettings& settings);
export void ikApplyPole(IkChainState& chain, const glm::vec3& pole);
export void ikRebuildFromDirections(IkChainState& chain, const glm::vec3* directions);

export IkResult solveFabrik(IkChainState& chain, const glm::vec3& target, const IkSettings& settings);
export IkResult solveCcd(IkChainState& chain, const glm::vec3& target, const IkSettings& settings);
export IkResult solveTwoBoneAnalytic(IkChainState& chain, const glm::vec3& target, const IkSettings& settings);
export IkResult solveIk(IkChainState& chain, const glm::vec3& target, const IkSettings& settings);
