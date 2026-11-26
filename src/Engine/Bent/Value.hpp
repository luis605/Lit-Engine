#pragma once

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
    int64_t i = 0;
    double f = 0.0;
    double v[4] = {0.0, 0.0, 0.0, 0.0};
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

struct RecordV {
    std::vector<std::pair<std::string, Value>> fields;
};

struct ActionV {
    std::string name;
    std::vector<std::pair<std::string, Value>> args;
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
    NativeFn native;
    bool liftable = false;
};

[[nodiscard]] bool equal(const Value& a, const Value& b);
[[nodiscard]] bool approxEqual(const Value& a, const Value& b, double tolerance);
[[nodiscard]] int compare(const Value& a, const Value& b);
[[nodiscard]] uint64_t hashValue(const Value& value);
[[nodiscard]] bool containsGen(const Value& value);

[[nodiscard]] std::string print(const Value& value);
[[nodiscard]] std::string printFloat(double value);
[[nodiscard]] std::string quoteString(const std::string& text);
[[nodiscard]] bool truthy(const Value& value, const SrcLoc& loc);

}
