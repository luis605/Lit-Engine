module;

#include <array>
#include <cmath>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

module Engine.Json;

namespace {

constexpr size_t kNumberBufferSize = 64;
constexpr size_t kInitialReserve = 256;
constexpr unsigned char kFirstNonAscii = 0x80;
constexpr unsigned char kFirstPrintable = 0x20;
constexpr std::string_view kHexDigits = "0123456789abcdef";

bool isPlainAscii(const std::string& text) {
    for (const char character : text) {
        if (static_cast<unsigned char>(character) >= kFirstNonAscii) return false;
    }
    return true;
}

class Writer {
  public:
    Writer(std::string& out, int indent) : m_out(out), m_indent(indent) {}

    void write(const Json& json, int depth) {
        switch (json.type()) {
            case Json::Type::Null: m_out.append("null"); return;
            case Json::Type::Bool: m_out.append(json.asBool() ? "true" : "false"); return;
            case Json::Type::Number: writeNumber(json.asNumber()); return;
            case Json::Type::String: writeString(json.asString()); return;
            case Json::Type::Array: writeArray(json, depth); return;
            case Json::Type::Object: writeObject(json, depth); return;
        }
    }

  private:
    void writeNumber(double number) {
        if (!std::isfinite(number)) {
            m_out.append("null");
            return;
        }
        std::array<char, kNumberBufferSize> buffer;
        char* end = ::nlohmann::detail::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
        m_out.append(buffer.data(), static_cast<size_t>(end - buffer.data()));
    }

    void writeString(const std::string& text) {
        if (!isPlainAscii(text)) {
            m_out.append(nlohmann::ordered_json(text).dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace));
            return;
        }
        m_out.push_back('"');
        size_t runStart = 0;
        for (size_t index = 0; index < text.size(); ++index) {
            const unsigned char byte = static_cast<unsigned char>(text[index]);
            if (byte >= kFirstPrintable && byte != '"' && byte != '\\') continue;
            m_out.append(text, runStart, index - runStart);
            runStart = index + 1;
            writeEscape(byte);
        }
        m_out.append(text, runStart, text.size() - runStart);
        m_out.push_back('"');
    }

    void writeEscape(unsigned char byte) {
        m_out.push_back('\\');
        switch (byte) {
            case '"': m_out.push_back('"'); return;
            case '\\': m_out.push_back('\\'); return;
            case '\b': m_out.push_back('b'); return;
            case '\f': m_out.push_back('f'); return;
            case '\n': m_out.push_back('n'); return;
            case '\r': m_out.push_back('r'); return;
            case '\t': m_out.push_back('t'); return;
            default:
                m_out.append("u00");
                m_out.push_back(kHexDigits[byte >> 4]);
                m_out.push_back(kHexDigits[byte & 0xF]);
        }
    }

    void newline(int depth) {
        if (m_indent < 0) return;
        m_out.push_back('\n');
        m_out.append(static_cast<size_t>(depth) * static_cast<size_t>(m_indent), ' ');
    }

    void writeArray(const Json& json, int depth) {
        const auto& items = json.items();
        if (items.empty()) {
            m_out.append("[]");
            return;
        }
        m_out.push_back('[');
        bool first = true;
        for (const Json& item : items) {
            if (!first) m_out.push_back(',');
            first = false;
            newline(depth + 1);
            write(item, depth + 1);
        }
        newline(depth);
        m_out.push_back(']');
    }

    void writeObject(const Json& json, int depth) {
        const auto& members = json.members();
        if (members.empty()) {
            m_out.append("{}");
            return;
        }
        m_out.push_back('{');
        bool first = true;
        for (const auto& [key, value] : members) {
            if (!first) m_out.push_back(',');
            first = false;
            newline(depth + 1);
            writeString(key);
            m_out.push_back(':');
            if (m_indent >= 0) m_out.push_back(' ');
            write(value, depth + 1);
        }
        newline(depth);
        m_out.push_back('}');
    }

    std::string& m_out;
    int m_indent;
};

}

std::string Json::dump(int indent) const {
    std::string out;
    out.reserve(kInitialReserve);
    Writer(out, indent).write(*this, 0);
    return out;
}
