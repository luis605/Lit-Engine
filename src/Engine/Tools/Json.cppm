module;

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Engine.Json;

export class Json {
  public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Member = std::pair<std::string, Json>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : m_type(Type::Bool), m_bool(value) {}
    Json(int value) : m_type(Type::Number), m_number(value) {}
    Json(unsigned value) : m_type(Type::Number), m_number(value) {}
    Json(int64_t value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Json(uint64_t value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Json(float value) : m_type(Type::Number), m_number(value) {}
    Json(double value) : m_type(Type::Number), m_number(value) {}
    Json(const char* value) : m_type(Type::String), m_string(value) {}
    Json(std::string value) : m_type(Type::String), m_string(std::move(value)) {}

    static Json array() {
        Json result;
        result.m_type = Type::Array;
        return result;
    }
    static Json object() {
        Json result;
        result.m_type = Type::Object;
        return result;
    }
    static Json array(std::vector<Json> items) {
        Json result;
        result.m_type = Type::Array;
        result.m_items = std::move(items);
        return result;
    }
    static Json object(std::vector<Member> members) {
        Json result;
        result.m_type = Type::Object;
        result.m_members = std::move(members);
        return result;
    }
    Json& reserve(size_t capacity) {
        if (m_type == Type::Array) m_items.reserve(capacity);
        else if (m_type == Type::Object) m_members.reserve(capacity);
        return *this;
    }

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] bool isNull() const { return m_type == Type::Null; }
    [[nodiscard]] bool isBool() const { return m_type == Type::Bool; }
    [[nodiscard]] bool isNumber() const { return m_type == Type::Number; }
    [[nodiscard]] bool isString() const { return m_type == Type::String; }
    [[nodiscard]] bool isArray() const { return m_type == Type::Array; }
    [[nodiscard]] bool isObject() const { return m_type == Type::Object; }

    [[nodiscard]] bool asBool(bool fallback = false) const { return m_type == Type::Bool ? m_bool : fallback; }
    [[nodiscard]] double asNumber(double fallback = 0.0) const { return m_type == Type::Number ? m_number : fallback; }
    [[nodiscard]] const std::string& asString() const { return m_string; }
    [[nodiscard]] const std::vector<Json>& items() const { return m_items; }
    [[nodiscard]] const std::vector<Member>& members() const { return m_members; }
    [[nodiscard]] size_t size() const { return m_type == Type::Array ? m_items.size() : (m_type == Type::Object ? m_members.size() : 0); }

    Json& push(Json value) {
        m_type = Type::Array;
        m_items.push_back(std::move(value));
        return m_items.back();
    }
    Json& set(std::string key, Json value) {
        m_type = Type::Object;
        for (auto& [memberKey, memberValue] : m_members) {
            if (memberKey == key) {
                memberValue = std::move(value);
                return memberValue;
            }
        }
        m_members.emplace_back(std::move(key), std::move(value));
        return m_members.back().second;
    }
    [[nodiscard]] const Json* get(std::string_view key) const {
        for (const auto& [memberKey, memberValue] : m_members) {
            if (memberKey == key) return &memberValue;
        }
        return nullptr;
    }
    [[nodiscard]] const Json* at(size_t index) const { return index < m_items.size() ? &m_items[index] : nullptr; }

    [[nodiscard]] std::string dump(int indent = -1) const;
    static std::optional<Json> parse(std::string_view text, std::string* error = nullptr);
    bool operator==(const Json& other) const;

  private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<Json> m_items;
    std::vector<Member> m_members;
};

export const Json* jsonFind(const Json& root, std::string_view path);
export std::string jsonDiff(const Json& left, const Json& right, double epsilon = 0.0);
export std::string expectNumber(const Json& root, std::string_view path, double expected, double epsilon = 1.0e-5);
export std::string expectString(const Json& root, std::string_view path, std::string_view expected);
export std::string expectBool(const Json& root, std::string_view path, bool expected);
export std::string expectSize(const Json& root, std::string_view path, size_t expected);
export std::optional<std::string> readTextFile(const std::string& path);
export std::string expectVec(const Json& root, std::string_view path, std::initializer_list<double> expected, double epsilon = 1.0e-5);
