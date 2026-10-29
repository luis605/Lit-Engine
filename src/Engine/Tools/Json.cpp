module;

#include <charconv>
#include <nlohmann/json.hpp>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module Engine.Json;

namespace {

using Ordered = nlohmann::ordered_json;

Ordered toOrdered(const Json& j) {
    switch (j.type()) {
        case Json::Type::Null: return nullptr;
        case Json::Type::Bool: return j.asBool();
        case Json::Type::Number: {
            const double v = j.asNumber();
            if (!std::isfinite(v)) return nullptr;
            return v;
        }
        case Json::Type::String: return j.asString();
        case Json::Type::Array: {
            Ordered out = Ordered::array();
            for (const Json& item : j.items()) out.push_back(toOrdered(item));
            return out;
        }
        case Json::Type::Object: {
            Ordered out = Ordered::object();
            for (const auto& [k, v] : j.members()) out[k] = toOrdered(v);
            return out;
        }
    }
    return nullptr;
}

Json fromOrdered(const Ordered& j) {
    if (j.is_boolean()) return Json(j.get<bool>());
    if (j.is_number()) return Json(j.get<double>());
    if (j.is_string()) return Json(j.get<std::string>());
    if (j.is_array()) {
        Json out = Json::array();
        for (const Ordered& item : j) out.push(fromOrdered(item));
        return out;
    }
    if (j.is_object()) {
        Json out = Json::object();
        for (const auto& [k, v] : j.items()) out.set(k, fromOrdered(v));
        return out;
    }
    return Json();
}

struct Segment {
    std::string key;
    bool isIndex = false;
    size_t index = 0;
    bool isMatch = false;
    std::string matchKey, matchValue;
};

bool splitPath(std::string_view path, std::vector<Segment>& out) {
    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '.') {
            ++i;
            continue;
        }
        Segment seg;
        if (path[i] == '[') {
            const size_t end = path.find(']', i);
            if (end == std::string_view::npos) return false;
            const std::string_view inner = path.substr(i + 1, end - i - 1);
            const size_t eq = inner.find('=');
            if (eq != std::string_view::npos) {
                seg.isMatch = true;
                seg.matchKey = std::string(inner.substr(0, eq));
                seg.matchValue = std::string(inner.substr(eq + 1));
            } else {
                seg.isIndex = true;
                const auto r = std::from_chars(inner.data(), inner.data() + inner.size(), seg.index);
                if (r.ec != std::errc{}) return false;
            }
            i = end + 1;
        } else {
            size_t end = i;
            while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
            seg.key = std::string(path.substr(i, end - i));
            i = end;
        }
        out.push_back(std::move(seg));
    }
    return true;
}

bool matches(const Json& item, const Segment& seg) {
    const Json* field = item.get(seg.matchKey);
    if (!field) return false;
    if (field->isString()) return field->asString() == seg.matchValue;
    if (field->isNumber()) return std::to_string(static_cast<long long>(field->asNumber())) == seg.matchValue;
    if (field->isBool()) return (field->asBool() ? "true" : "false") == seg.matchValue;
    return false;
}

std::string fmt(double v) { return Ordered(v).dump(); }

std::string missing(std::string_view path) { return "missing path '" + std::string(path) + "'"; }

}  // namespace

std::string Json::dump(int indent) const {
    return toOrdered(*this).dump(indent, ' ', false, Ordered::error_handler_t::replace);
}

std::optional<Json> Json::parse(std::string_view text, std::string* error) {
    try {
        return fromOrdered(Ordered::parse(text.begin(), text.end(), nullptr, true, false));
    } catch (const Ordered::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

bool Json::operator==(const Json& o) const {
    if (m_type != o.m_type) return false;
    switch (m_type) {
        case Type::Null: return true;
        case Type::Bool: return m_bool == o.m_bool;
        case Type::Number: return m_number == o.m_number;
        case Type::String: return m_string == o.m_string;
        case Type::Array: return m_items == o.m_items;
        case Type::Object: return m_members == o.m_members;
    }
    return false;
}

const Json* jsonFind(const Json& root, std::string_view path) {
    std::vector<Segment> segs;
    if (!splitPath(path, segs)) return nullptr;
    const Json* cur = &root;
    for (const Segment& seg : segs) {
        if (seg.isIndex) {
            cur = cur->isArray() ? cur->at(seg.index) : nullptr;
        } else if (seg.isMatch) {
            const Json* found = nullptr;
            if (cur->isArray()) {
                for (const Json& item : cur->items()) {
                    if (item.isObject() && matches(item, seg)) {
                        found = &item;
                        break;
                    }
                }
            }
            cur = found;
        } else {
            cur = cur->isObject() ? cur->get(seg.key) : nullptr;
        }
        if (!cur) return nullptr;
    }
    return cur;
}

std::string jsonDiff(const Json& a, const Json& b, double epsilon) {
    struct Walk {
        double eps;
        std::string diff(const Json& x, const Json& y, const std::string& path) const {
            if (x.type() != y.type()) return path + ": type differs";
            switch (x.type()) {
                case Json::Type::Null: return {};
                case Json::Type::Bool: return x.asBool() == y.asBool() ? std::string() : path + ": " + (x.asBool() ? "true" : "false") + " vs " + (y.asBool() ? "true" : "false");
                case Json::Type::Number: return std::abs(x.asNumber() - y.asNumber()) <= eps ? std::string() : path + ": " + fmt(x.asNumber()) + " vs " + fmt(y.asNumber());
                case Json::Type::String: return x.asString() == y.asString() ? std::string() : path + ": '" + x.asString() + "' vs '" + y.asString() + "'";
                case Json::Type::Array: {
                    if (x.size() != y.size()) return path + ": size " + std::to_string(x.size()) + " vs " + std::to_string(y.size());
                    for (size_t i = 0; i < x.size(); ++i) {
                        if (std::string d = diff(x.items()[i], y.items()[i], path + "[" + std::to_string(i) + "]"); !d.empty()) return d;
                    }
                    return {};
                }
                case Json::Type::Object: {
                    for (const auto& [k, v] : x.members()) {
                        const Json* other = y.get(k);
                        const std::string sub = path.empty() ? k : path + "." + k;
                        if (!other) return sub + ": missing on right";
                        if (std::string d = diff(v, *other, sub); !d.empty()) return d;
                    }
                    for (const auto& [k, v] : y.members()) {
                        if (!x.get(k)) return (path.empty() ? k : path + "." + k) + ": missing on left";
                    }
                    return {};
                }
            }
            return {};
        }
    };
    return Walk{epsilon}.diff(a, b, "");
}

std::string expectNumber(const Json& root, std::string_view path, double expected, double epsilon) {
    const Json* v = jsonFind(root, path);
    if (!v) return missing(path);
    if (!v->isNumber()) return std::string(path) + " is not a number";
    if (std::abs(v->asNumber() - expected) <= epsilon) return {};
    return std::string(path) + " is " + fmt(v->asNumber()) + ", expected " + fmt(expected);
}

std::string expectString(const Json& root, std::string_view path, std::string_view expected) {
    const Json* v = jsonFind(root, path);
    if (!v) return missing(path);
    if (!v->isString()) return std::string(path) + " is not a string";
    if (v->asString() == expected) return {};
    return std::string(path) + " is '" + v->asString() + "', expected '" + std::string(expected) + "'";
}

std::string expectBool(const Json& root, std::string_view path, bool expected) {
    const Json* v = jsonFind(root, path);
    if (!v) return missing(path);
    if (!v->isBool()) return std::string(path) + " is not a bool";
    if (v->asBool() == expected) return {};
    return std::string(path) + " is " + (v->asBool() ? "true" : "false") + ", expected " + (expected ? "true" : "false");
}

std::string expectSize(const Json& root, std::string_view path, size_t expected) {
    const Json* v = jsonFind(root, path);
    if (!v) return missing(path);
    if (v->size() == expected) return {};
    return std::string(path) + " has size " + std::to_string(v->size()) + ", expected " + std::to_string(expected);
}

std::string expectVec(const Json& root, std::string_view path, std::initializer_list<double> expected, double epsilon) {
    const Json* v = jsonFind(root, path);
    if (!v) return missing(path);
    if (!v->isArray() || v->size() != expected.size()) return std::string(path) + " has wrong shape";
    size_t i = 0;
    for (const double e : expected) {
        if (std::abs(v->items()[i].asNumber() - e) > epsilon) return std::string(path) + "[" + std::to_string(i) + "] is " + fmt(v->items()[i].asNumber()) + ", expected " + fmt(e);
        ++i;
    }
    return {};
}
