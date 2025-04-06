#include <Engine/Scripting/math.hpp>
#include <cmath>

#define RAYMATH_IMPLEMENTATION
#include <raylib.h>
#include <raymath.h>

LitVector3 LitVector3::normalized() {
    Vector3 vector = (Vector3){x, y, z};
    Vector3 normalizedVector = Vector3Normalize(vector);
    x = normalizedVector.x;
    y = normalizedVector.y;
    z = normalizedVector.z;

    return *this;
}

float LitVector3::lengthSquared() { return x * x + y * y + z * z; }

LitVector3 LitVector3::CrossProduct(LitVector3 v2) {
    x = y * v2.z - z * v2.y;
    y = z * v2.x - x * v2.z;
    z = x * v2.y - y * v2.x;

    return LitVector3(x, y, z);
}

LitVector3 LitVector3::pos() { return LitVector3(x, y, z); }

LitVector3 operator*(const LitVector3& vec, float scalar) {
    return LitVector3(vec.x * scalar, vec.y * scalar, vec.z * scalar);
}

template <typename... Args> LitVector3 multiplyAll(const Args&... args) {
    LitVector3 result = LitVector3(1.0f, 1.0f, 1.0f);
    ((result *= args), ...);
    return result;
}

float LitVector3Distance(LitVector3 v1, LitVector3 v2) {
    float result = 0.0f;

    float dx = v2.x - v1.x;
    float dy = v2.y - v1.y;
    float dz = v2.z - v1.z;
    result = sqrtf(dx * dx + dy * dy + dz * dz);

    return result;
}

LitVector3 LitVector3Scale(LitVector3 v, float scalar) {
    return {v.x * scalar, v.y * scalar, v.z * scalar};
}

float LitVector3Length(const LitVector3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

float LitVector3LengthSqr(const LitVector3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

template <typename T>
constexpr const T& customClamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : ((value > high) ? high : value);
}

int LitLerpInt(int start, int end, float t) {
    t = customClamp(t, 0.0f, 1.0f);
    return static_cast<int>(std::round(start + t * (end - start)));
}

template <typename T> T LitLerp(T start, T end, float t) {
    t = customClamp(t, 0.0f, 1.0f);
    return start + t * (end - start);
}

LitVector3 lerpVector3(const LitVector3& start, const LitVector3& end, float t) {
    t = customClamp(t, 0.0f, 1.0f);
    return LitVector3{LitLerp(start.x, end.x, t), LitLerp(start.y, end.y, t),
                   LitLerp(start.z, end.z, t)};
}