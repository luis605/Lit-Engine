#include "Engine/Bent/Value.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace bent {

std::string SrcLoc::str() const {
    std::string out = file.empty() ? std::string("<input>") : file;
    out += ':' + std::to_string(line) + ':' + std::to_string(col);
    return out;
}

const char* kindName(Kind kind) {
    switch (kind) {
    case Kind::Unit: return "unit";
    case Kind::Bool: return "bool";
    case Kind::Int: return "int";
    case Kind::Float: return "float";
    case Kind::Str: return "string";
    case Kind::Vec3: return "vec3";
    case Kind::Quat: return "quat";
    case Kind::List: return "list";
    case Kind::Record: return "record";
    case Kind::Action: return "action";
    case Kind::Func: return "function";
    case Kind::Gen: return "generator";
    case Kind::Entity: return "entity";
    }
    return "?";
}

Value Value::boolean(bool value) {
    Value out;
    out.kind = Kind::Bool;
    out.b = value;
    return out;
}

Value Value::integer(int64_t value) {
    Value out;
    out.kind = Kind::Int;
    out.i = value;
    return out;
}

Value Value::number(double value) {
    Value out;
    out.kind = Kind::Float;
    out.f = value;
    return out;
}

Value Value::string(std::string value) {
    Value out;
    out.kind = Kind::Str;
    out.s = std::make_shared<const std::string>(std::move(value));
    return out;
}

Value Value::vec3(double x, double y, double z) {
    Value out;
    out.kind = Kind::Vec3;
    out.v[0] = x;
    out.v[1] = y;
    out.v[2] = z;
    return out;
}

Value Value::quat(double w, double x, double y, double z) {
    Value out;
    out.kind = Kind::Quat;
    out.v[0] = w;
    out.v[1] = x;
    out.v[2] = y;
    out.v[3] = z;
    return out;
}

Value Value::makeList(ValueList items) {
    Value out;
    out.kind = Kind::List;
    out.list = std::make_shared<const ValueList>(std::move(items));
    return out;
}

namespace {
void sortFields(std::vector<std::pair<std::string, Value>>& fields) {
    std::stable_sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    // Later duplicates win (record update semantics).
    std::vector<std::pair<std::string, Value>> unique;
    for (auto& field : fields) {
        if (!unique.empty() && unique.back().first == field.first) unique.back().second = std::move(field.second);
        else unique.push_back(std::move(field));
    }
    fields = std::move(unique);
}
}

Value Value::record(std::vector<std::pair<std::string, Value>> fields) {
    sortFields(fields);
    Value out;
    out.kind = Kind::Record;
    auto rec = std::make_shared<RecordV>();
    rec->fields = std::move(fields);
    out.rec = std::move(rec);
    return out;
}

Value Value::action(std::string name, std::vector<std::pair<std::string, Value>> args) {
    sortFields(args);
    Value out;
    out.kind = Kind::Action;
    auto act = std::make_shared<ActionV>();
    act->name = std::move(name);
    act->args = std::move(args);
    out.act = std::move(act);
    return out;
}

Value Value::entity(int64_t ref) {
    Value out;
    out.kind = Kind::Entity;
    out.i = ref;
    return out;
}

const ValueList& Value::items() const {
    static const ValueList empty;
    return list ? *list : empty;
}

const std::string& Value::text() const {
    static const std::string empty;
    return s ? *s : empty;
}

const Value* Value::field(const std::string& name) const {
    if (kind != Kind::Record || !rec) return nullptr;
    for (const auto& [key, value] : rec->fields) {
        if (key == name) return &value;
    }
    return nullptr;
}

const Value* ActionV::arg(const std::string& key) const {
    for (const auto& [name, value] : args) {
        if (name == key) return &value;
    }
    return nullptr;
}

bool equal(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        if (a.kind == Kind::Int && b.kind == Kind::Int) return a.i == b.i;
        return a.num() == b.num();
    }
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case Kind::Unit: return true;
    case Kind::Bool: return a.b == b.b;
    case Kind::Str: return a.text() == b.text();
    case Kind::Vec3: return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2];
    case Kind::Quat: return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2] && a.v[3] == b.v[3];
    case Kind::Entity: return a.i == b.i;
    case Kind::List: {
        const ValueList& x = a.items();
        const ValueList& y = b.items();
        if (x.size() != y.size()) return false;
        for (size_t index = 0; index < x.size(); ++index) {
            if (!equal(x[index], y[index])) return false;
        }
        return true;
    }
    case Kind::Record: {
        const auto& x = a.rec->fields;
        const auto& y = b.rec->fields;
        if (x.size() != y.size()) return false;
        for (size_t index = 0; index < x.size(); ++index) {
            if (x[index].first != y[index].first || !equal(x[index].second, y[index].second)) return false;
        }
        return true;
    }
    case Kind::Action: {
        if (a.act->name != b.act->name || a.act->args.size() != b.act->args.size()) return false;
        for (size_t index = 0; index < a.act->args.size(); ++index) {
            if (a.act->args[index].first != b.act->args[index].first || !equal(a.act->args[index].second, b.act->args[index].second)) return false;
        }
        return true;
    }
    case Kind::Func: return a.fn == b.fn;
    case Kind::Gen: return a.gen == b.gen;
    default: return false;
    }
}

namespace {
bool closeNumber(double x, double y, double tolerance) {
    if (x == y) return true;
    if (std::isnan(x) || std::isnan(y)) return std::isnan(x) && std::isnan(y);
    const double diff = std::fabs(x - y);
    return diff <= tolerance || diff <= tolerance * std::max(std::fabs(x), std::fabs(y));
}
}

bool approxEqual(const Value& a, const Value& b, double tolerance) {
    if (a.isNumber() && b.isNumber()) return closeNumber(a.num(), b.num(), tolerance);
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case Kind::Vec3:
        return closeNumber(a.v[0], b.v[0], tolerance) && closeNumber(a.v[1], b.v[1], tolerance) && closeNumber(a.v[2], b.v[2], tolerance);
    case Kind::Quat: {
        const double dot = a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2] + a.v[3] * b.v[3];
        const double sign = dot < 0.0 ? -1.0 : 1.0;
        for (int index = 0; index < 4; ++index) {
            if (!closeNumber(a.v[index], sign * b.v[index], tolerance)) return false;
        }
        return true;
    }
    case Kind::List: {
        const ValueList& x = a.items();
        const ValueList& y = b.items();
        if (x.size() != y.size()) return false;
        for (size_t index = 0; index < x.size(); ++index) {
            if (!approxEqual(x[index], y[index], tolerance)) return false;
        }
        return true;
    }
    case Kind::Record: {
        const auto& x = a.rec->fields;
        const auto& y = b.rec->fields;
        if (x.size() != y.size()) return false;
        for (size_t index = 0; index < x.size(); ++index) {
            if (x[index].first != y[index].first || !approxEqual(x[index].second, y[index].second, tolerance)) return false;
        }
        return true;
    }
    default: return equal(a, b);
    }
}

int compare(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        const double x = a.num();
        const double y = b.num();
        if (x < y) return -1;
        if (x > y) return 1;
        return 0;
    }
    if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind) ? -1 : 1;
    switch (a.kind) {
    case Kind::Bool: return static_cast<int>(a.b) - static_cast<int>(b.b);
    case Kind::Str: return a.text().compare(b.text()) < 0 ? -1 : (a.text() == b.text() ? 0 : 1);
    case Kind::Entity: return a.i < b.i ? -1 : (a.i > b.i ? 1 : 0);
    case Kind::Vec3:
    case Kind::Quat:
        for (int index = 0; index < 4; ++index) {
            if (a.v[index] < b.v[index]) return -1;
            if (a.v[index] > b.v[index]) return 1;
        }
        return 0;
    case Kind::List: {
        const ValueList& x = a.items();
        const ValueList& y = b.items();
        for (size_t index = 0; index < std::min(x.size(), y.size()); ++index) {
            const int c = compare(x[index], y[index]);
            if (c != 0) return c;
        }
        return x.size() < y.size() ? -1 : (x.size() > y.size() ? 1 : 0);
    }
    default: {
        const std::string x = print(a);
        const std::string y = print(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    }
}

namespace {
uint64_t mix(uint64_t h, uint64_t value) {
    h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}
uint64_t hashBytes(const void* data, size_t size) {
    uint64_t h = 0xcbf29ce484222325ULL;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t index = 0; index < size; ++index) {
        h ^= bytes[index];
        h *= 0x100000001b3ULL;
    }
    return h;
}
uint64_t hashDouble(double value) {
    if (value == 0.0) value = 0.0;
    if (std::isnan(value)) return 0x7ff8dead;
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
}

uint64_t hashValue(const Value& value) {
    uint64_t h = static_cast<uint64_t>(value.kind) * 0x51ed27;
    switch (value.kind) {
    case Kind::Bool: return mix(h, value.b);
    case Kind::Int: return mix(static_cast<uint64_t>(Kind::Float) * 0x51ed27, hashDouble(static_cast<double>(value.i)));
    case Kind::Float: return mix(h, hashDouble(value.f));
    case Kind::Entity: return mix(h, static_cast<uint64_t>(value.i));
    case Kind::Str: return mix(h, hashBytes(value.text().data(), value.text().size()));
    case Kind::Vec3:
    case Kind::Quat:
        for (double component : value.v) h = mix(h, hashDouble(component));
        return h;
    case Kind::List:
        for (const Value& item : value.items()) h = mix(h, hashValue(item));
        return h;
    case Kind::Record:
        for (const auto& [key, item] : value.rec->fields) h = mix(mix(h, hashBytes(key.data(), key.size())), hashValue(item));
        return h;
    case Kind::Action:
        h = mix(h, hashBytes(value.act->name.data(), value.act->name.size()));
        for (const auto& [key, item] : value.act->args) h = mix(mix(h, hashBytes(key.data(), key.size())), hashValue(item));
        return h;
    default: return h;
    }
}

bool containsGen(const Value& value) {
    switch (value.kind) {
    case Kind::Gen: return true;
    case Kind::List:
        for (const Value& item : value.items()) {
            if (containsGen(item)) return true;
        }
        return false;
    case Kind::Record:
        for (const auto& field : value.rec->fields) {
            if (containsGen(field.second)) return true;
        }
        return false;
    default: return false;
    }
}

std::string printFloat(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    std::string text(buffer, result.ptr);
    if (text.find_first_of(".en") == std::string::npos) text += ".0";
    return text;
}

std::string quoteString(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char hex[8];
                std::snprintf(hex, sizeof hex, "\\x%02x", c);
                out += hex;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

namespace {
bool isIdentifier(const std::string& text) {
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_')) return false;
    for (const char c : text) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

void printFields(std::string& out, const std::vector<std::pair<std::string, Value>>& fields) {
    for (size_t index = 0; index < fields.size(); ++index) {
        if (index) out += ", ";
        out += isIdentifier(fields[index].first) ? fields[index].first : quoteString(fields[index].first);
        out += ": ";
        out += print(fields[index].second);
    }
}
}

std::string print(const Value& value) {
    switch (value.kind) {
    case Kind::Unit: return "unit";
    case Kind::Bool: return value.b ? "true" : "false";
    case Kind::Int: return std::to_string(value.i);
    case Kind::Float: return printFloat(value.f);
    case Kind::Str: return quoteString(value.text());
    case Kind::Vec3: return "vec3(" + printFloat(value.v[0]) + ", " + printFloat(value.v[1]) + ", " + printFloat(value.v[2]) + ")";
    case Kind::Quat: return "quat(" + printFloat(value.v[0]) + ", " + printFloat(value.v[1]) + ", " + printFloat(value.v[2]) + ", " + printFloat(value.v[3]) + ")";
    case Kind::Entity: return "entity(" + std::to_string(value.i) + ")";
    case Kind::List: {
        std::string out = "[";
        const ValueList& items = value.items();
        for (size_t index = 0; index < items.size(); ++index) {
            if (index) out += ", ";
            out += print(items[index]);
        }
        return out + "]";
    }
    case Kind::Record: {
        std::string out = "{";
        printFields(out, value.rec->fields);
        return out + "}";
    }
    case Kind::Action: {
        std::string out = value.act->name + "(";
        printFields(out, value.act->args);
        return out + ")";
    }
    case Kind::Func: return "<fn " + (value.fn ? value.fn->name : std::string()) + ">";
    case Kind::Gen: return "<generator>";
    }
    return "?";
}

bool truthy(const Value& value, const SrcLoc& loc) {
    if (value.kind != Kind::Bool) throw BentError(loc, std::string("expected bool, got ") + kindName(value.kind) + " " + print(value));
    return value.b;
}

}
