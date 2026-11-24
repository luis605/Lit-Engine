#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>
#include <vector>

#include "Engine/Bent/Ast.hpp"

namespace bent {

namespace {

void appendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}

std::vector<Token> lex(const std::string& source, const std::string& file) {
    std::vector<Token> tokens;
    size_t pos = 0;
    int line = 1;
    int col = 1;
    const auto here = [&]() { return SrcLoc{file, line, col}; };
    const auto advance = [&](size_t count = 1) {
        for (size_t step = 0; step < count && pos < source.size(); ++step) {
            if (source[pos] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
            ++pos;
        }
    };
    const auto peek = [&](size_t offset = 0) -> char { return pos + offset < source.size() ? source[pos + offset] : '\0'; };

    while (pos < source.size()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        if (c == '#' || (c == '/' && peek(1) == '/')) {
            while (pos < source.size() && peek() != '\n') advance();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            const SrcLoc start = here();
            advance(2);
            while (pos < source.size() && !(peek() == '*' && peek(1) == '/')) advance();
            if (pos >= source.size()) throw BentError(start, "unterminated block comment");
            advance(2);
            continue;
        }
        Token token;
        token.loc = here();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const size_t start = pos;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
            token.kind = TokKind::Ident;
            token.text = source.substr(start, pos - start);
            tokens.push_back(std::move(token));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            const size_t start = pos;
            bool isFloat = false;
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                advance(2);
                const size_t digits = pos;
                while (hexDigit(peek()) >= 0) advance();
                if (pos == digits) throw BentError(token.loc, "malformed hex literal");
                uint64_t value = 0;
                const auto result = std::from_chars(source.data() + digits, source.data() + pos, value, 16);
                if (result.ec != std::errc()) throw BentError(token.loc, "hex literal out of range");
                token.kind = TokKind::Int;
                token.ival = static_cast<int64_t>(value);
                token.text = source.substr(start, pos - start);
                tokens.push_back(std::move(token));
                continue;
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
                isFloat = true;
                advance();
                while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            if (peek() == 'e' || peek() == 'E') {
                const char sign = peek(1);
                const bool signed_ = sign == '+' || sign == '-';
                if (std::isdigit(static_cast<unsigned char>(signed_ ? peek(2) : sign))) {
                    isFloat = true;
                    advance(signed_ ? 2 : 1);
                    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
                }
            }
            if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') throw BentError(here(), "unexpected character after number");
            token.text = source.substr(start, pos - start);
            if (isFloat) {
                token.kind = TokKind::Float;
                token.fval = std::strtod(token.text.c_str(), nullptr);
            } else {
                token.kind = TokKind::Int;
                const auto result = std::from_chars(token.text.data(), token.text.data() + token.text.size(), token.ival);
                if (result.ec != std::errc()) throw BentError(token.loc, "integer literal out of range: " + token.text);
            }
            tokens.push_back(std::move(token));
            continue;
        }
        if (c == '"') {
            advance();
            std::string text;
            while (true) {
                if (pos >= source.size() || peek() == '\n') throw BentError(token.loc, "unterminated string");
                const char ch = peek();
                if (ch == '"') {
                    advance();
                    break;
                }
                if (ch == '\\') {
                    const SrcLoc escapeLoc = here();
                    advance();
                    const char e = peek();
                    advance();
                    switch (e) {
                    case 'n': text += '\n'; break;
                    case 't': text += '\t'; break;
                    case 'r': text += '\r'; break;
                    case '0': text += '\0'; break;
                    case '"': text += '"'; break;
                    case '\\': text += '\\'; break;
                    case 'x': {
                        const int hi = hexDigit(peek());
                        const int lo = hexDigit(peek(1));
                        if (hi < 0 || lo < 0) throw BentError(escapeLoc, "malformed \\x escape");
                        advance(2);
                        text += static_cast<char>(hi * 16 + lo);
                        break;
                    }
                    case 'u': {
                        if (peek() != '{') throw BentError(escapeLoc, "expected \\u{hex}");
                        advance();
                        uint32_t code = 0;
                        int count = 0;
                        while (hexDigit(peek()) >= 0 && count < 6) {
                            code = code * 16 + static_cast<uint32_t>(hexDigit(peek()));
                            advance();
                            ++count;
                        }
                        if (peek() != '}' || count == 0 || code > 0x10FFFF) throw BentError(escapeLoc, "malformed \\u{...} escape");
                        advance();
                        appendUtf8(text, code);
                        break;
                    }
                    default: throw BentError(escapeLoc, std::string("unknown escape \\") + e);
                    }
                    continue;
                }
                text += ch;
                advance();
            }
            token.kind = TokKind::String;
            token.text = std::move(text);
            tokens.push_back(std::move(token));
            continue;
        }
        static const char* const kOperators[] = {"==>", "==", "=>", "!=", "<=", ">=", "~=", "++", "(", ")", "[", "]", "{", "}", ",", ":", ".", "=", "<", ">", "+", "-", "*", "/", "%"};
        bool matched = false;
        for (const char* op : kOperators) {
            const size_t length = std::char_traits<char>::length(op);
            if (source.compare(pos, length, op) == 0) {
                token.kind = TokKind::Op;
                token.text = op;
                advance(length);
                tokens.push_back(std::move(token));
                matched = true;
                break;
            }
        }
        if (!matched) throw BentError(token.loc, std::string("unexpected character '") + c + "'");
    }
    Token end;
    end.kind = TokKind::End;
    end.loc = here();
    tokens.push_back(std::move(end));
    return tokens;
}

}
