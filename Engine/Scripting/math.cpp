// Custom implementation of clamp
template <typename T>
constexpr const T& custom_clamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : ((value > high) ? high : value);
}

// Lerp for floating-point types (float and double)
template <typename T>
T lerp(T start, T end, float t) {
    t = custom_clamp(t, 0.0f, 1.0f);
    return start + t * (end - start);
}

// Lerp for integer types (int)
int lerp_int(int start, int end, float t) {
    t = custom_clamp(t, 0.0f, 1.0f);
    return static_cast<int>(std::round(start + t * (end - start)));
}

// Lerp for Vector3
Vector3 lerp_Vector3(Vector3 start, Vector3 end, float t) {
    t = custom_clamp(t, 0.0f, 1.0f);
    return Vector3{
        lerp(start.x, end.x, t),
        lerp(start.y, end.y, t),
        lerp(start.z, end.z, t)
    };
}


struct LitVector3 : Vector3 {

    // Constructor with default values
    LitVector3(float x = 0.0f, float y = 0.0f, float z = 0.0f)
        : Vector3{x, y, z} {}


    LitVector3(const Vector3 &v)
        : Vector3{v.x, v.y, v.z} {}
        
    // Normalize function
    LitVector3 normalized() {
        Vector3 vector = (Vector3){ x, y, z };
        Vector3 normalizedVector = Vector3Normalize(vector);
        x = normalizedVector.x;
        y = normalizedVector.y;
        z = normalizedVector.z;

        return *this;
    }

    LitVector3 lengthSquared() {
        return x * x + y * y + z * z;
    }

    LitVector3 CrossProduct(LitVector3 v2) {
        x = y * v2.z - z * v2.y;
        y = z * v2.x - x * v2.z;
        z = x * v2.y - y * v2.x;
        return LitVector3(x, y, z);
    }

    LitVector3 pos() {
        return LitVector3(x, y, z);
    }

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

LitVector3 operator*(const LitVector3& vec, float scalar) {
    return LitVector3(vec.x * scalar, vec.y * scalar, vec.z * scalar);
}

template <typename... Args>
LitVector3 multiplyAll(const Args&... args) {
    LitVector3 result = LitVector3(1.0f, 1.0f, 1.0f);
    ((result *= args), ...);
    return result;
}

float LitVector3Distance(LitVector3 v1, LitVector3 v2) {
    float result = 0.0f;

    float dx = v2.x - v1.x;
    float dy = v2.y - v1.y;
    float dz = v2.z - v1.z;
    result = sqrtf(dx*dx + dy*dy + dz*dz);

    return result;
}


LitVector3 LitVector3Scale(LitVector3 v, float scalar) {
    return { v.x*scalar, v.y*scalar, v.z*scalar };
}

float LitVector3Length(const LitVector3 v) {
    float result = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);

    return result;
}

float LitVector3LengthSqr(const LitVector3 v) {
    float result = v.x*v.x + v.y*v.y + v.z*v.z;

    return result;
}
