#pragma once

// Bent values: immutable, shared, structurally compared. See docs/Bent.md.

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bent {

struct SrcLoc {
    std::string file;
    int line = 0;
    int col = 0;
    [[nodiscard]] std::string str() const;
};

// Every language-level failure (lex, parse, type, runtime) carries a location.
struct BentError : std::runtime_error {
    SrcLoc loc;
    BentError(const SrcLoc& where, const std::string& message) : std::runtime_error(where.str() + ": " + message), loc(where) {}
};

enum class Kind : uint8_t { Unit, Bool, Int, Float, Str, Vec3, Quat, List, Record, Action, Func, Gen, Entity };

[[nodiscard]] const char* kindName(Kind kind);

struct Value;
struct RecordV;
struct ActionV;
struct FuncV;
struct GenV;

using ValueList = std::vector<Value>;

struct Value {
    Kind kind = Kind::Unit;
    bool b = false;
    int64_t i = 0;  // Int, Entity reference
    double f = 0.0;
    double v[4] = {0.0, 0.0, 0.0, 0.0};  // Vec3 (x, y, z), Quat (w, x, y, z)
    std::shared_ptr<const std::string> s;
    std::shared_ptr<const ValueList> list;
    std::shared_ptr<const RecordV> rec;
    std::shared_ptr<const ActionV> act;
    std::shared_ptr<const FuncV> fn;
    std::shared_ptr<const GenV> gen;

    static Value unit() { return {}; }
    static Value boolean(bool value);
    static Value integer(int64_t value);
    static Value number(double value);
    static Value string(std::string value);
    static Value vec3(double x, double y, double z);
    static Value quat(double w, double x, double y, double z);
    static Value makeList(ValueList items);
    static Value record(std::vector<std::pair<std::string, Value>> fields);
    static Value action(std::string name, std::vector<std::pair<std::string, Value>> args);
    static Value entity(int64_t ref);

    [[nodiscard]] bool isNumber() const { return kind == Kind::Int || kind == Kind::Float; }
    [[nodiscard]] double num() const { return kind == Kind::Int ? static_cast<double>(i) : f; }
    [[nodiscard]] const ValueList& items() const;
    [[nodiscard]] const std::string& text() const;
    [[nodiscard]] const Value* field(const std::string& name) const;
};

// Fields sorted by name, so printing and comparison are canonical.
struct RecordV {
    std::vector<std::pair<std::string, Value>> fields;
};

// An action is data: the interpreter for a scene program (the host) gives it meaning.
struct ActionV {
    std::string name;
    std::vector<std::pair<std::string, Value>> args;  // sorted by name, only the given ones
    [[nodiscard]] const Value* arg(const std::string& key) const;
};

struct Interp;
struct Expr;
struct Env;

using NativeFn = std::function<Value(Interp&, const ValueList& positional, const std::vector<std::pair<std::string, Value>>& named, const SrcLoc&)>;

struct FuncV {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<const Expr> body;
    std::shared_ptr<const Env> closure;
    NativeFn native;  // set for builtins and partially applied natives
    bool liftable = false;
};

// Structural equality. Numbers compare numerically (1 == 1.0); NaN != NaN.
[[nodiscard]] bool equal(const Value& a, const Value& b);
// Approximate equality: numbers (absolute or relative tolerance), vectors, quaternions (sign canonical), lists and records recursively.
[[nodiscard]] bool approxEqual(const Value& a, const Value& b, double tolerance);
// Total order used by sort/distinct (by kind first).
[[nodiscard]] int compare(const Value& a, const Value& b);
[[nodiscard]] uint64_t hashValue(const Value& value);
[[nodiscard]] bool containsGen(const Value& value);

// Canonical source form: parsing and evaluating the printed text gives an equal value (for data, actions and entities).
[[nodiscard]] std::string print(const Value& value);
[[nodiscard]] std::string printFloat(double value);
[[nodiscard]] std::string quoteString(const std::string& text);
[[nodiscard]] bool truthy(const Value& value, const SrcLoc& loc);

}
