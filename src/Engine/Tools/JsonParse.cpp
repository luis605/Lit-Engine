module;

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

module Engine.Json;

namespace {

constexpr size_t kLinearDuplicateLimit = 32;
constexpr size_t kMaxNestingDepth = 512;

struct Frame {
    bool isObject;
    size_t start;
};

bool hasDuplicateKeys(const std::vector<Json::Member>& members) {
    if (members.size() <= kLinearDuplicateLimit) {
        for (size_t outer = 1; outer < members.size(); ++outer) {
            for (size_t inner = 0; inner < outer; ++inner) {
                if (members[inner].first == members[outer].first) return true;
            }
        }
        return false;
    }
    std::unordered_set<std::string_view> seen;
    seen.reserve(members.size());
    for (const auto& member : members) {
        if (!seen.insert(member.first).second) return true;
    }
    return false;
}

void mergeDuplicateKeys(std::vector<Json::Member>& members) {
    if (!hasDuplicateKeys(members)) return;
    std::vector<Json::Member> unique;
    unique.reserve(members.size());
    for (auto& member : members) {
        Json::Member* existing = nullptr;
        for (auto& candidate : unique) {
            if (candidate.first == member.first) {
                existing = &candidate;
                break;
            }
        }
        if (existing) existing->second = std::move(member.second);
        else unique.push_back(std::move(member));
    }
    members = std::move(unique);
}

class Builder {
  public:
    using number_integer_t = nlohmann::ordered_json::number_integer_t;
    using number_unsigned_t = nlohmann::ordered_json::number_unsigned_t;
    using number_float_t = nlohmann::ordered_json::number_float_t;
    using string_t = nlohmann::ordered_json::string_t;
    using binary_t = nlohmann::ordered_json::binary_t;

    bool null() { return add(Json()); }
    bool boolean(bool value) { return add(Json(value)); }
    bool number_integer(number_integer_t value) { return add(Json(static_cast<double>(value))); }
    bool number_unsigned(number_unsigned_t value) { return add(Json(static_cast<double>(value))); }
    bool number_float(number_float_t value, const string_t&) { return add(Json(value)); }
    bool string(string_t& value) { return add(Json(std::move(value))); }
    bool binary(binary_t&) { return false; }

    bool start_object(std::size_t) { return open(true); }
    bool key(string_t& value) {
        m_keys.push_back(std::move(value));
        return true;
    }
    bool end_object() {
        const Frame frame = m_frames.back();
        m_frames.pop_back();
        const size_t count = m_values.size() - frame.start;
        std::vector<Json::Member> members;
        members.reserve(count);
        const size_t keyStart = m_keys.size() - count;
        for (size_t index = 0; index < count; ++index) members.emplace_back(std::move(m_keys[keyStart + index]), std::move(m_values[frame.start + index]));
        m_keys.resize(keyStart);
        m_values.resize(frame.start);
        mergeDuplicateKeys(members);
        return add(Json::object(std::move(members)));
    }

    bool start_array(std::size_t) { return open(false); }
    bool end_array() {
        const Frame frame = m_frames.back();
        m_frames.pop_back();
        std::vector<Json> items;
        items.reserve(m_values.size() - frame.start);
        for (size_t index = frame.start; index < m_values.size(); ++index) items.push_back(std::move(m_values[index]));
        m_values.resize(frame.start);
        return add(Json::array(std::move(items)));
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::ordered_json::exception& exception) {
        m_error = exception.what();
        return false;
    }

    Json takeRoot() { return std::move(m_root); }
    const std::string& error() const { return m_error; }

  private:
    bool open(bool isObject) {
        if (m_frames.size() >= kMaxNestingDepth) {
            m_error = "nesting too deep";
            return false;
        }
        m_frames.push_back({isObject, m_values.size()});
        return true;
    }
    bool add(Json value) {
        if (m_frames.empty()) m_root = std::move(value);
        else m_values.push_back(std::move(value));
        return true;
    }

    Json m_root;
    std::vector<Frame> m_frames;
    std::vector<Json> m_values;
    std::vector<std::string> m_keys;
    std::string m_error;
};

}

std::optional<Json> Json::parse(std::string_view text, std::string* error) {
    Builder builder;
    if (!text.empty() && std::memchr(text.data(), 0, text.size())) {
        if (error) *error = "embedded NUL byte";
        return std::nullopt;
    }
    const bool ok = nlohmann::ordered_json::sax_parse(text.data(), text.data() + text.size(), &builder, nlohmann::detail::input_format_t::json, true, false);
    if (!ok) {
        if (error) *error = builder.error();
        return std::nullopt;
    }
    return builder.takeRoot();
}
