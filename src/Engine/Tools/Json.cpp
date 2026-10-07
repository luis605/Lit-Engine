module;

#include <charconv>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module Engine.Json;

namespace {

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out += "null";
        return;
    }
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    out.append(buf, r.ptr);
}

struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string error;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at offset " + std::to_string(i);
        return false;
    }
    bool literal(std::string_view word) {
        if (s.substr(i, word.size()) != word) return false;
        i += word.size();
        return true;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\') {
                if (i >= s.size()) return fail("bad escape");
                const char e = s[i++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return fail("bad unicode escape");
                        unsigned code = 0;
                        const auto r = std::from_chars(s.data() + i, s.data() + i + 4, code, 16);
                        if (r.ec != std::errc{}) return fail("bad unicode escape");
                        i += 4;
                        if (code < 0x80) {
                            out += static_cast<char>(code);
                        } else if (code < 0x800) {
                            out += static_cast<char>(0xC0 | (code >> 6));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (code >> 12));
                            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        continue;
                    }
                    default: c = e;
                }
            }
            out += c;
        }
        if (i >= s.size()) return fail("unterminated string");
        ++i;
        return true;
    }
    bool value(Json& out, int depth) {
        if (depth > 128) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            ++i;
            out = Json::object();
            ws();
            if (i < s.size() && s[i] == '}') return ++i, true;
            for (;;) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.set(std::move(key), std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}') return ++i, true;
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i;
            out = Json::array();
            ws();
            if (i < s.size() && s[i] == ']') return ++i, true;
            for (;;) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.push(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']') return ++i, true;
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string str;
            if (!string(str)) return false;
            out = Json(std::move(str));
            return true;
        }
        if (literal("true")) return out = Json(true), true;
        if (literal("false")) return out = Json(false), true;
        if (literal("null")) return out = Json(), true;
        double number = 0.0;
        const auto r = std::from_chars(s.data() + i, s.data() + s.size(), number);
        if (r.ec != std::errc{}) return fail("unexpected token");
        i = static_cast<size_t>(r.ptr - s.data());
        out = Json(number);
        return true;
    }
};

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

std::string fmt(double v) {
    std::string s;
    writeNumber(s, v);
    return s;
}

std::string missing(std::string_view path) { return "missing path '" + std::string(path) + "'"; }

}  // namespace

void Json::write(std::string& out, int indent, int depth) const {
    auto newline = [&](int d) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };
    switch (m_type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += m_bool ? "true" : "false"; break;
        case Type::Number: writeNumber(out, m_number); break;
        case Type::String: writeString(out, m_string); break;
        case Type::Array: {
            if (m_items.empty()) {
                out += "[]";
                break;
            }
            bool flat = true;
            for (const Json& item : m_items) flat = flat && item.m_type != Type::Array && item.m_type != Type::Object;
            out += '[';
            for (size_t i = 0; i < m_items.size(); ++i) {
                if (i) out += ',';
                if (!flat) newline(depth + 1);
                m_items[i].write(out, indent, depth + 1);
            }
            if (!flat) newline(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            if (m_members.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            for (size_t i = 0; i < m_members.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                writeString(out, m_members[i].first);
                out += indent < 0 ? ":" : ": ";
                m_members[i].second.write(out, indent, depth + 1);
            }
            newline(depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    return out;
}

std::optional<Json> Json::parse(std::string_view text, std::string* error) {
    Parser p{text};
    Json out;
    if (!p.value(out, 0)) {
        if (error) *error = p.error;
        return std::nullopt;
    }
    p.ws();
    if (p.i != text.size()) {
        if (error) *error = "trailing characters at offset " + std::to_string(p.i);
        return std::nullopt;
    }
    return out;
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
