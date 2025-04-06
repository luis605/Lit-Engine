#ifndef MATH_H
#define MATH_H

#include <cmath>
#include <raylib.h>

struct LitVector3 : Vector3 {
    LitVector3(float x = 0.0f, float y = 0.0f, float z = 0.0f)
        : Vector3{x, y, z} {}

    LitVector3(const Vector3& v) : Vector3{v.x, v.y, v.z} {}

    LitVector3 normalized();
    float lengthSquared();
    LitVector3 CrossProduct(LitVector3 v2);
    LitVector3 pos();

    LitVector3 operator+(const LitVector3& other) const {
        return LitVector3(x + other.x, y + other.y, z + other.z);
    }

    LitVector3 operator-(const LitVector3& other) const {
        return LitVector3(x - other.x, y - other.y, z - other.z);
    }

    LitVector3& operator*=(const LitVector3& other) {
        x *= other.x;
        y *= other.y;
        z *= other.z;
        return *this;
    }

    LitVector3 operator*(const LitVector3& other) const {
        return LitVector3(x * other.x, y * other.y, z * other.z);
    }
};

float LitVector3Distance(LitVector3 v1, LitVector3 v2);
LitVector3 LitVector3Scale(LitVector3 v, float scalar);
float LitVector3Length(const LitVector3 v);
float LitVector3LengthSqr(const LitVector3 v);
template <typename T> constexpr const T& customClamp(const T& value, const T& low, const T& high);
template <typename T> T LitLerp(T start, T end, float t);
int LitLerpInt(int start, int end, float t);
LitVector3 lerpVector3(const LitVector3& start, const LitVector3& end, float t);

#endif // MATH_H