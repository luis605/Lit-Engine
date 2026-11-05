module;

#include <charconv>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module Engine.Json;

namespace {

using Ordered = nlohmann::ordered_json;

struct Segment {
    std::string key;
    bool isIndex = false;
    size_t index = 0;
    bool isMatch = false;
    std::string matchKey;
    std::string matchValue;
};

constexpr size_t kExpectedSegments = 8;
constexpr double kMaxExactInteger = 9.0e15;

bool splitPath(std::string_view path, std::vector<Segment>& out) {
    const size_t base = out.size();
    out.reserve(base + kExpectedSegments);

    size_t index = 0;
    const size_t pathSize = path.size();

    while (index < pathSize) {
        if (path[index] == '.') {
            ++index;
            continue;
        }

        Segment& segment = out.emplace_back();

        if (path[index] == '[') {
            const size_t end = path.find(']', index + 1);
            if (end == std::string_view::npos) {
                out.resize(base);
                return false;
            }

            const std::string_view inner = path.substr(index + 1, end - index - 1);
            const size_t equalsPosition = inner.find('=');
            if (equalsPosition != std::string_view::npos) {
                segment.isMatch = true;
                segment.matchKey.assign(inner.data(), equalsPosition);
                segment.matchValue.assign(inner.data() + equalsPosition + 1, inner.size() - equalsPosition - 1);
            } else {
                segment.isIndex = true;
                const char* first = inner.data();
                const char* last = first + inner.size();
                const auto parsed = std::from_chars(first, last, segment.index);
                if (parsed.ec != std::errc{} || parsed.ptr != last) {
                    out.resize(base);
                    return false;
                }
            }
            index = end + 1;
        } else {
            size_t end = path.find_first_of(".[", index);
            if (end == std::string_view::npos) end = pathSize;
            segment.key.assign(path.data() + index, end - index);
            index = end;
        }
    }
    return true;
}

bool matches(const Json& item, const Segment& segment) {
    const Json* field = item.get(segment.matchKey);
    if (!field) return false;
    if (field->isString()) return field->asString() == segment.matchValue;
    if (field->isNumber()) return std::fabs(field->asNumber()) < kMaxExactInteger && std::to_string(static_cast<long long>(field->asNumber())) == segment.matchValue;
    if (field->isBool()) return (field->asBool() ? "true" : "false") == segment.matchValue;
    return false;
}

std::string formatNumber(double value) { return Ordered(value).dump(); }

std::string missing(std::string_view path) { return "missing path '" + std::string(path) + "'"; }

}

bool Json::operator==(const Json& other) const {
    if (m_type != other.m_type) return false;
    switch (m_type) {
        case Type::Null: return true;
        case Type::Bool: return m_bool == other.m_bool;
        case Type::Number: return m_number == other.m_number;
        case Type::String: return m_string == other.m_string;
        case Type::Array: return m_items == other.m_items;
        case Type::Object: return m_members == other.m_members;
    }
    return false;
}

const Json* jsonFind(const Json& root, std::string_view path) {
    std::vector<Segment> segments;
    if (!splitPath(path, segments)) return nullptr;
    const Json* current = &root;
    for (const Segment& segment : segments) {
        if (segment.isIndex) {
            current = current->isArray() ? current->at(segment.index) : nullptr;
        } else if (segment.isMatch) {
            const Json* found = nullptr;
            if (current->isArray()) {
                for (const Json& item : current->items()) {
                    if (item.isObject() && matches(item, segment)) {
                        found = &item;
                        break;
                    }
                }
            }
            current = found;
        } else {
            current = current->isObject() ? current->get(segment.key) : nullptr;
        }
        if (!current) return nullptr;
    }
    return current;
}

std::string jsonDiff(const Json& left, const Json& right, double epsilon) {
    struct Walk {
        double tolerance;
        std::string diff(const Json& lhs, const Json& rhs, const std::string& path) const {
            if (lhs.type() != rhs.type()) return path + ": type differs";
            switch (lhs.type()) {
                case Json::Type::Null: return {};
                case Json::Type::Bool: return lhs.asBool() == rhs.asBool() ? std::string() : path + ": " + (lhs.asBool() ? "true" : "false") + " vs " + (rhs.asBool() ? "true" : "false");
                case Json::Type::Number: return std::abs(lhs.asNumber() - rhs.asNumber()) <= tolerance ? std::string() : path + ": " + formatNumber(lhs.asNumber()) + " vs " + formatNumber(rhs.asNumber());
                case Json::Type::String: return lhs.asString() == rhs.asString() ? std::string() : path + ": '" + lhs.asString() + "' vs '" + rhs.asString() + "'";
                case Json::Type::Array: {
                    if (lhs.size() != rhs.size()) return path + ": size " + std::to_string(lhs.size()) + " vs " + std::to_string(rhs.size());
                    for (size_t position = 0; position < lhs.size(); ++position) {
                        if (std::string childDiff = diff(lhs.items()[position], rhs.items()[position], path + "[" + std::to_string(position) + "]"); !childDiff.empty()) return childDiff;
                    }
                    return {};
                }
                case Json::Type::Object: {
                    for (const auto& [key, value] : lhs.members()) {
                        const Json* counterpart = rhs.get(key);
                        const std::string childPath = path.empty() ? key : path + "." + key;
                        if (!counterpart) return childPath + ": missing on right";
                        if (std::string childDiff = diff(value, *counterpart, childPath); !childDiff.empty()) return childDiff;
                    }
                    for (const auto& [key, value] : rhs.members()) {
                        if (!lhs.get(key)) return (path.empty() ? key : path + "." + key) + ": missing on left";
                    }
                    return {};
                }
            }
            return {};
        }
    };
    return Walk{epsilon}.diff(left, right, "");
}

std::string expectNumber(const Json& root, std::string_view path, double expected, double epsilon) {
    const Json* found = jsonFind(root, path);
    if (!found) return missing(path);
    if (!found->isNumber()) return std::string(path) + " is not a number";
    if (std::abs(found->asNumber() - expected) <= epsilon) return {};
    return std::string(path) + " is " + formatNumber(found->asNumber()) + ", expected " + formatNumber(expected);
}

std::string expectString(const Json& root, std::string_view path, std::string_view expected) {
    const Json* found = jsonFind(root, path);
    if (!found) return missing(path);
    if (!found->isString()) return std::string(path) + " is not a string";
    if (found->asString() == expected) return {};
    return std::string(path) + " is '" + found->asString() + "', expected '" + std::string(expected) + "'";
}

std::string expectBool(const Json& root, std::string_view path, bool expected) {
    const Json* found = jsonFind(root, path);
    if (!found) return missing(path);
    if (!found->isBool()) return std::string(path) + " is not a bool";
    if (found->asBool() == expected) return {};
    return std::string(path) + " is " + (found->asBool() ? "true" : "false") + ", expected " + (expected ? "true" : "false");
}

std::string expectSize(const Json& root, std::string_view path, size_t expected) {
    const Json* found = jsonFind(root, path);
    if (!found) return missing(path);
    if (found->size() == expected) return {};
    return std::string(path) + " has size " + std::to_string(found->size()) + ", expected " + std::to_string(expected);
}

std::string expectVec(const Json& root, std::string_view path, std::initializer_list<double> expected, double epsilon) {
    const Json* found = jsonFind(root, path);
    if (!found) return missing(path);
    if (!found->isArray() || found->size() != expected.size()) return std::string(path) + " has wrong shape";
    size_t index = 0;
    for (const double expectedValue : expected) {
        if (std::abs(found->items()[index].asNumber() - expectedValue) > epsilon) return std::string(path) + "[" + std::to_string(index) + "] is " + formatNumber(found->items()[index].asNumber()) + ", expected " + formatNumber(expectedValue);
        ++index;
    }
    return {};
}

std::optional<std::string> readTextFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    try {
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    } catch (const std::ios_base::failure&) {
        return std::nullopt;
    }
}
