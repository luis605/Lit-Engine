#ifndef CORE_MATH_HPP
#define CORE_MATH_HPP

#include <raylib.h>
#include <raymath.h>

float NormalizeDegrees(float deg);
float NormalizeDegreesHalf(float deg);
float ShortestAngularDistance(float a, float b);
Quaternion QuaternionFromEuler(const Vector3& euler);
Vector3 GetContinuousEulerFromQuaternion(const Quaternion& q, const Vector3& previousEulerHint);


#endif // CORE_MATH_HPP