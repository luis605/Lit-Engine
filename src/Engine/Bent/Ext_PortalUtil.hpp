#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Bent/Interp.hpp"

namespace bent::xp {

using Fields = std::vector<std::pair<std::string, Value>>;

inline glm::vec3 vec3Of(const Value& value, const SrcLoc& loc, const char* what) {
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
inline Value strV(std::string s) { return Value::string(std::move(s)); }
inline Value listV(ValueList items) { return Value::makeList(std::move(items)); }
inline Value recV(Fields fields) { return Value::record(std::move(fields)); }

inline Value matV(const glm::mat4& m) {
    ValueList out;
    out.reserve(16);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) out.push_back(Value::number(m[c][r]));
    }
    return Value::makeList(std::move(out));
}
inline glm::mat4 matOf(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind != Kind::List || value.items().size() != 16) throw BentError(loc, std::string(what) + ": expected a list of 16 numbers (column-major mat4)");
    glm::mat4 m(1.0f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) m[c][r] = static_cast<float>(argNumber(value.items()[static_cast<size_t>(c * 4 + r)], loc, what));
    }
    return m;
}

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
        return f ? vec3Of(*f, loc, key) : fallback;
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

inline Rec rec(const Value& value, const SrcLoc& loc) { return Rec{value, loc}; }

inline void checkArity(const ValueList& args, size_t n, const SrcLoc& loc, const char* name) {
    if (args.size() != n) throw BentError(loc, std::string(name) + "() takes " + std::to_string(n) + " arguments, got " + std::to_string(args.size()));
}

using Fn = std::function<Value(const ValueList&, const SrcLoc&)>;

inline void reg(Program& program, const std::string& name, Fn fn) {
    program.native(name, [fn = std::move(fn), name](Interp&, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) {
        if (!named.empty()) throw BentError(loc, name + "() takes no named arguments");
        return fn(args, loc);
    });
}

}
