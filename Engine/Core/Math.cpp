#include <Engine/Core/Math.hpp>
#include <cmath>
#include <array>

inline float NormalizeDegrees(float deg) {
    const float remainder = fmod(deg, 360.0);
    return (remainder < 0) ? remainder + 360.0 : remainder;
}

inline float NormalizeDegreesHalf(float d) {
    d = fmodf(d + 180.0f, 360.0f);
    if (d < 0) d += 360.0f;
    d -= 180.0f;
    if (d >= 90.0f) d -= 180.0f;
    else if (d <= -90.0f) d += 180.0f;
    return (d == 90.0f || d == -90.0f) ? std::copysignf(89.5f, d) : d;
}

inline float ShortestAngularDistance(float a, float b) {
    float diff = fmod(b - a, 360.0);
    if (diff > 180.0) {
        diff -= 360.0;
    } else if (diff < -180.0) {
        diff += 360.0;
    }
    return diff;
}

Quaternion QuaternionFromEuler(const Vector3& euler) {
    float roll_rad = euler.x * DEG2RAD;
    float pitch_rad = euler.y * DEG2RAD;
    float yaw_rad = euler.z * DEG2RAD;

    float cy = cos(yaw_rad * 0.5);
    float sy = sin(yaw_rad * 0.5);
    float cp = cos(pitch_rad * 0.5);
    float sp = sin(pitch_rad * 0.5);
    float cr = cos(roll_rad * 0.5);
    float sr = sin(roll_rad * 0.5);

    Quaternion q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    return q;
}

Vector3 GetContinuousEulerFromQuaternion(const Quaternion& q, const Vector3& previousEulerHint) {
    Vector3 euler;

    float sin_pitch = 2.0 * (q.w * q.y - q.z * q.x);

    constexpr float GIMBAL_LOCK_EPSILON = 0.999999f;

    if (std::abs(sin_pitch) > GIMBAL_LOCK_EPSILON) {
        euler.y = std::copysign(89.5f, sin_pitch);

        euler.x = previousEulerHint.x;

        float combined_angle = atan2(2.0 * (q.x * q.y + q.z * q.w), 1.0 - 2.0 * (q.y * q.y + q.z * q.z)) * 180.0 / PI;

        if (sin_pitch > 0) {
            euler.z = NormalizeDegrees(combined_angle - euler.x);
        } else {
            euler.z = NormalizeDegrees(combined_angle + euler.x);
        }

    } else {
        std::array<Vector3, 2> solutions;
        Vector3& e1 = solutions[0];
        Vector3& e2 = solutions[1];

        e1.y = asin(sin_pitch) * 180.0 / PI;
        e1.x = atan2(2.0 * (q.w * q.x + q.y * q.z), 1.0 - 2.0 * (q.x * q.x + q.y * q.y)) * 180.0 / PI;
        e1.z = atan2(2.0 * (q.w * q.z + q.x * q.y), 1.0 - 2.0 * (q.y * q.y + q.z * q.z)) * 180.0 / PI;

        e2.x = NormalizeDegrees(e1.x + 180.0);
        e2.y = 180.0 - e1.y;
        e2.z = NormalizeDegrees(e1.z + 180.0);

        e1.x = NormalizeDegrees(e1.x);
        e1.y = NormalizeDegrees(e1.y);
        e1.z = NormalizeDegrees(e1.z);

        float dist1 = std::abs(ShortestAngularDistance(previousEulerHint.x, e1.x)) +
                       std::abs(ShortestAngularDistance(previousEulerHint.y, e1.y)) +
                       std::abs(ShortestAngularDistance(previousEulerHint.z, e1.z));

        float dist2 = std::abs(ShortestAngularDistance(previousEulerHint.x, e2.x)) +
                       std::abs(ShortestAngularDistance(previousEulerHint.y, e2.y)) +
                       std::abs(ShortestAngularDistance(previousEulerHint.z, e2.z));

        euler = (dist1 <= dist2) ? e1 : e2;
    }

    return euler;
}