module;

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>

module Editor.context;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double TAU = 6.28318530717958647692;
constexpr double EULER = 2.71828182845904523536;
constexpr int MAX_NESTING_DEPTH = 200;

struct ExpressionParser {
    const std::string& source;
    size_t position = 0;
    int depth = 0;
    bool ok = true;

    void skipSpaces() {
        while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position]))) ++position;
    }

    bool peekIs(char expected) {
        skipSpaces();
        return position < source.size() && source[position] == expected;
    }

    bool enter() {
        if (++depth > MAX_NESTING_DEPTH) ok = false;
        return ok;
    }

    double parseParenthesized() {
        ++position;
        const double value = sum();
        if (peekIs(')')) {
            ++position;
        } else {
            ok = false;
        }
        return value;
    }

    double parseWord() {
        const size_t start = position;
        while (position < source.size() && std::isalpha(static_cast<unsigned char>(source[position]))) ++position;
        const std::string word = source.substr(start, position - start);
        if (word == "pi") return PI;
        if (word == "tau") return TAU;
        if (word == "e") return EULER;
        if (peekIs('(')) {
            ++position;
            const double argument = sum();
            if (peekIs(')')) ++position;
            if (word == "sin") return std::sin(argument);
            if (word == "cos") return std::cos(argument);
            if (word == "tan") return std::tan(argument);
            if (word == "sqrt") return std::sqrt(argument);
            if (word == "abs") return std::abs(argument);
            if (word == "rad") return argument * PI / 180.0;
            if (word == "deg") return argument * 180.0 / PI;
        }
        ok = false;
        return 0.0;
    }

    double parseNumber() {
        const char* begin = source.c_str() + position;
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin) {
            ok = false;
            return 0.0;
        }
        position += static_cast<size_t>(end - begin);
        return value;
    }

    double primary() {
        skipSpaces();
        if (position >= source.size() || !enter()) {
            ok = false;
            return 0.0;
        }
        double value = 0.0;
        const char current = source[position];
        if (current == '(') {
            value = parseParenthesized();
        } else if (current == '-') {
            ++position;
            value = -power();
        } else if (current == '+') {
            ++position;
            value = power();
        } else if (std::isalpha(static_cast<unsigned char>(current))) {
            value = parseWord();
        } else {
            value = parseNumber();
        }
        --depth;
        return value;
    }

    double power() {
        const double base = primary();
        if (!peekIs('^')) return base;
        ++position;
        if (!enter()) return 0.0;
        const double exponent = power();
        --depth;
        return std::pow(base, exponent);
    }

    double product() {
        double value = power();
        while (ok) {
            skipSpaces();
            if (position >= source.size()) break;
            const char op = source[position];
            if (op != '*' && op != '/' && op != '%') break;
            ++position;
            const double right = power();
            if (op == '*') value *= right;
            if (op == '/') value = right == 0.0 ? 0.0 : value / right;
            if (op == '%') value = right == 0.0 ? 0.0 : std::fmod(value, right);
        }
        return value;
    }

    double sum() {
        double value = product();
        while (ok) {
            skipSpaces();
            if (position >= source.size()) break;
            const char op = source[position];
            if (op != '+' && op != '-') break;
            ++position;
            const double right = product();
            value = op == '+' ? value + right : value - right;
        }
        return value;
    }
};

bool isWordSeparator(char character) { return character == ' ' || character == '_' || character == '-' || character == '/' || character == '.'; }

char toLowerChar(char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); }

}

std::optional<double> evaluateExpression(const std::string& text) {
    if (text.empty()) return std::nullopt;
    ExpressionParser parser{text};
    const double value = parser.sum();
    parser.skipSpaces();
    if (!parser.ok || parser.position != text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

float fuzzyScore(const std::string& pattern, const std::string& text) {
    constexpr float WORD_START_BONUS = 2.0f;
    constexpr float STREAK_BONUS = 1.5f;
    constexpr float LENGTH_PENALTY = 0.02f;
    if (pattern.empty()) return 1.0f;
    float score = 0.0f;
    size_t patternIndex = 0;
    int streak = 0;
    char previous = ' ';
    for (size_t textIndex = 0; textIndex < text.size() && patternIndex < pattern.size(); ++textIndex) {
        if (toLowerChar(text[textIndex]) == toLowerChar(pattern[patternIndex])) {
            const bool camelStart = std::isupper(static_cast<unsigned char>(text[textIndex])) && std::islower(static_cast<unsigned char>(previous));
            const bool wordStart = textIndex == 0 || isWordSeparator(previous) || camelStart;
            score += 1.0f + (wordStart ? WORD_START_BONUS : 0.0f) + static_cast<float>(streak) * STREAK_BONUS;
            ++streak;
            ++patternIndex;
        } else {
            streak = 0;
        }
        previous = text[textIndex];
    }
    if (patternIndex < pattern.size()) return 0.0f;
    return score / (1.0f + LENGTH_PENALTY * static_cast<float>(text.size()));
}
