module;

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
    Json(bool v) : m_type(Type::Bool), m_bool(v) {}
    Json(int v) : m_type(Type::Number), m_number(v) {}
    Json(unsigned v) : m_type(Type::Number), m_number(v) {}
    Json(int64_t v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
    Json(uint64_t v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
    Json(float v) : m_type(Type::Number), m_number(v) {}
    Json(double v) : m_type(Type::Number), m_number(v) {}
    Json(const char* v) : m_type(Type::String), m_string(v) {}
    Json(std::string v) : m_type(Type::String), m_string(std::move(v)) {}

    static Json array() {
        Json j;
        j.m_type = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.m_type = Type::Object;
        return j;
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
        for (auto& [k, v] : m_members) {
            if (k == key) {
                v = std::move(value);
                return v;
            }
        }
        m_members.emplace_back(std::move(key), std::move(value));
        return m_members.back().second;
    }
    [[nodiscard]] const Json* get(std::string_view key) const {
        for (const auto& [k, v] : m_members) {
            if (k == key) return &v;
        }
        return nullptr;
    }
    [[nodiscard]] const Json* at(size_t index) const { return index < m_items.size() ? &m_items[index] : nullptr; }

    [[nodiscard]] std::string dump(int indent = -1) const;
    static std::optional<Json> parse(std::string_view text, std::string* error = nullptr);
    bool operator==(const Json& other) const;

  private:
    void write(std::string& out, int indent, int depth) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<Json> m_items;
    std::vector<Member> m_members;
};

export const Json* jsonFind(const Json& root, std::string_view path);
export std::string jsonDiff(const Json& a, const Json& b, double epsilon = 0.0);
export std::string expectNumber(const Json& root, std::string_view path, double expected, double epsilon = 1.0e-5);
export std::string expectString(const Json& root, std::string_view path, std::string_view expected);
export std::string expectBool(const Json& root, std::string_view path, bool expected);
export std::string expectSize(const Json& root, std::string_view path, size_t expected);
export std::string expectVec(const Json& root, std::string_view path, std::initializer_list<double> expected, double epsilon = 1.0e-5);
