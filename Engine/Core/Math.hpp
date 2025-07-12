#ifndef CORE_MATH_HPP
#define CORE_MATH_HPP

#include <raymath.h>

inline float NormalizeDegrees(float deg);
inline float NormalizeDegreesHalf(float deg);
inline float ShortestAngularDistance(float a, float b);
Quaternion QuaternionFromEuler(const Vector3& euler);
Vector3 GetContinuousEulerFromQuaternion(const Quaternion& q, const Vector3& previousEulerHint);


#endif // CORE_MATH_HPP