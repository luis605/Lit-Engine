#pragma once

// Helpers for the physics / character / nav / AI / animation / IK / audio builtins (Ext_Physics.cpp, Ext_Sim.cpp).
// Include after the engine module imports (needs glm).

#include <cmath>
#include <string>
#include <vector>

#include "Engine/Bent/Interp.hpp"

namespace bent::ext {

inline glm::vec3 vecOf(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind == Kind::Vec3) return glm::vec3(static_cast<float>(value.v[0]), static_cast<float>(value.v[1]), static_cast<float>(value.v[2]));
    if (value.isNumber()) return glm::vec3(static_cast<float>(value.num()));
    throw BentError(loc, std::string(what) + ": expected a vec3, got " + kindName(value.kind));
}
inline glm::quat quatOf(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind == Kind::Quat) return glm::quat(static_cast<float>(value.v[0]), static_cast<float>(value.v[1]), static_cast<float>(value.v[2]), static_cast<float>(value.v[3]));
    throw BentError(loc, std::string(what) + ": expected a quat, got " + kindName(value.kind));
}
inline Value vecV(const glm::vec3& v) { return Value::vec3(v.x, v.y, v.z); }
inline Value quatV(const glm::quat& q) { return Value::quat(q.w, q.x, q.y, q.z); }
inline Value numV(double d) { return Value::number(d); }
inline Value intV(int64_t i) { return Value::integer(i); }
inline Value boolV(bool b) { return Value::boolean(b); }
inline Value listV(ValueList items) { return Value::makeList(std::move(items)); }
inline Value recV(std::vector<std::pair<std::string, Value>> fields) { return Value::record(std::move(fields)); }

// Reads fields of a record value with defaults.
struct Rec {
    const Value& v;
    const SrcLoc& loc;
    [[nodiscard]] const Value* get(const char* key) const { return v.kind == Kind::Record ? v.field(key) : nullptr; }
    [[nodiscard]] bool has(const char* key) const { return get(key) != nullptr; }
    [[nodiscard]] double num(const char* key, double fallback) const {
        const Value* f = get(key);
        return f ? argNumber(*f, loc, key) : fallback;
    }
    [[nodiscard]] float f(const char* key, float fallback) const { return static_cast<float>(num(key, fallback)); }
    [[nodiscard]] int64_t integer(const char* key, int64_t fallback) const {
        const Value* f = get(key);
        return f ? argInt(*f, loc, key) : fallback;
    }
    [[nodiscard]] bool flag(const char* key, bool fallback) const {
        const Value* f = get(key);
        return f && f->kind == Kind::Bool ? f->b : fallback;
    }
    [[nodiscard]] std::string str(const char* key, const std::string& fallback) const {
        const Value* f = get(key);
        return f && f->kind == Kind::Str ? f->text() : fallback;
    }
    [[nodiscard]] glm::vec3 vec(const char* key, const glm::vec3& fallback) const {
        const Value* f = get(key);
        return f ? vecOf(*f, loc, key) : fallback;
    }
    [[nodiscard]] glm::quat quat(const char* key, const glm::quat& fallback) const {
        const Value* f = get(key);
        return f ? quatOf(*f, loc, key) : fallback;
    }
    [[nodiscard]] const ValueList& list(const char* key) const {
        static const ValueList empty;
        const Value* f = get(key);
        return f && f->kind == Kind::List ? f->items() : empty;
    }
};

inline Rec rec(const Value& v, const SrcLoc& loc) { return Rec{v, loc}; }
inline bool finite3(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}
