#ifndef MATH_H
#define MATH_H

#include "include_all.h"

template <typename T>
constexpr const T& customClamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : ((value > high) ? high : value);
}

template <typename T>
T litLerp(T start, T end, float t) {
    t = customClamp(t, 0.0f, 1.0f);
    return start + t * (end - start);
}

int lerpInt(int start, int end, float t) {
    t = customClamp(t, 0.0f, 1.0f);
    return static_cast<int>(std::round(start + t * (end - start)));
}


int lerpInt(int start, int end, float t);
Vector3 lerpVector3(Vector3 start, Vector3 end, float t);

struct LitVector3 : Vector3 {
    LitVector3(float x = 0.0f, float y = 0.0f, float z = 0.0f)
        : Vector3{x, y, z} {}


    LitVector3(const Vector3 &v)
        : Vector3{v.x, v.y, v.z} {}

    LitVector3 normalized();
    LitVector3 lengthSquared();
    LitVector3 CrossProduct(LitVector3 v2);
    LitVector3 pos();

    LitVector3 operator+(const LitVector3 &other) const {
        return LitVector3(x + other.x, y + other.y, z + other.z);
    }

    LitVector3 operator-(const LitVector3 &other) const {
        return LitVector3(x - other.x, y - other.y, z - other.z);
    }

    LitVector3& operator*=(const LitVector3& other) {
        x *= other.x;
        y *= other.y;
        z *= other.z;
        return *this;
    }

    LitVector3 operator*(const LitVector3 &other) const {
        return LitVector3(x * other.x, y * other.y, z * other.z);
    }
};

float LitVector3Distance(LitVector3 v1, LitVector3 v2);
LitVector3 LitVector3Scale(LitVector3 v, float scalar);
float LitVector3Length(const LitVector3 v);
float LitVector3LengthSqr(const LitVector3 v);

#endif // MATH_H