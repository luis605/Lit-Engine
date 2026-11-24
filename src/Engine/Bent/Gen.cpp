#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

#include "Engine/Bent/Interp.hpp"

namespace bent {

namespace {
uint64_t splitmix(uint64_t& state) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
}

Tape Tape::random(uint64_t seed, double size) {
    Tape tape;
    tape.m_random = true;
    tape.m_state = seed;
    tape.m_size = std::clamp(size, 0.0, 1.0);
    return tape;
}

Tape Tape::replay(std::vector<uint64_t> choices, double size) {
    Tape tape;
    tape.m_random = false;
    tape.m_prefix = std::move(choices);
    tape.m_size = std::clamp(size, 0.0, 1.0);
    return tape;
}

uint64_t Tape::next() { return splitmix(m_state); }

uint64_t Tape::draw(uint64_t bound) {
    if (m_drawn.size() >= maxDraws) throw Overrun{};
    uint64_t value = 0;
    if (m_random) {
        if (bound == std::numeric_limits<uint64_t>::max()) {
            value = next();
        } else {
            // Unbiased bounded draw (Lemire).
            const uint64_t range = bound + 1;
            __uint128_t product = static_cast<__uint128_t>(next()) * range;
            uint64_t low = static_cast<uint64_t>(product);
            if (low < range) {
                const uint64_t threshold = -range % range;
                while (low < threshold) {
                    product = static_cast<__uint128_t>(next()) * range;
                    low = static_cast<uint64_t>(product);
                }
            }
            value = static_cast<uint64_t>(product >> 64);
        }
    } else {
        const size_t at = m_drawn.size();
        value = at < m_prefix.size() ? std::min(m_prefix[at], bound) : 0;
    }
    m_drawn.push_back(value);
    return value;
}

uint64_t Tape::drawForced(uint64_t bound, uint64_t value) {
    if (!m_random) return draw(bound);
    if (m_drawn.size() >= maxDraws) throw Overrun{};
    value = std::min(value, bound);
    m_drawn.push_back(value);
    return value;
}

Value makeGen(std::shared_ptr<GenV> gen) {
    Value value;
    value.kind = Kind::Gen;
    value.gen = std::move(gen);
    return value;
}

Value Interp::realize(const Value& value, const SrcLoc& loc) {
    switch (value.kind) {
    case Kind::Gen: return draw(*value.gen);
    case Kind::List: {
        if (!containsGen(value)) return value;
        ValueList items;
        items.reserve(value.items().size());
        for (const Value& item : value.items()) items.push_back(realize(item, loc));
        return Value::makeList(std::move(items));
    }
    case Kind::Record: {
        if (!containsGen(value)) return value;
        NamedArgs fields;
        for (const auto& [key, item] : value.rec->fields) fields.emplace_back(key, realize(item, loc));
        return Value::record(std::move(fields));
    }
    default: return value;
    }
}

namespace {

int64_t clampTarget(int64_t lo, int64_t hi) { return std::clamp<int64_t>(0, lo, hi); }

// Simplest-first table of nasty floats.
const double kNasty[] = {0.0,
                         -0.0,
                         1.0e-45,  // denormal (float)
                         -1.0e-45,
                         static_cast<double>(FLT_MIN),
                         1.0e30,
                         -1.0e30,
                         static_cast<double>(FLT_MAX),
                         -static_cast<double>(FLT_MAX),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(),
                         1.0e-310,  // denormal (double)
                         1.0e300,
                         -1.0e300,
                         static_cast<double>(FLT_EPSILON),
                         16777217.0};

// Alphabet of the text generator, simplest first: lower case, digits, upper case, punctuation, whitespace and escapes, then bytes that
// break naive parsers (NUL, DEL, control characters, multi-byte UTF-8 and an invalid byte).
const std::vector<std::string>& alphabet() {
    static const std::vector<std::string> table = [] {
        std::vector<std::string> out;
        for (char c = 'a'; c <= 'z'; ++c) out.emplace_back(1, c);
        for (char c = '0'; c <= '9'; ++c) out.emplace_back(1, c);
        for (char c = 'A'; c <= 'Z'; ++c) out.emplace_back(1, c);
        for (const char* p : {" ", "_", "-", ".", ",", ":", "/", "{", "}", "[", "]", "#", "=", "+", "'", "\"", "\\", "\n", "\t", "\r"}) out.emplace_back(p);
        out.emplace_back(std::string(1, '\0'));
        out.emplace_back("\x7f");
        out.emplace_back("\x01");
        out.emplace_back("\xc3\xa9");          // e acute
        out.emplace_back("\xe6\xbc\xa2");      // CJK
        out.emplace_back("\xf0\x9f\x98\x80");  // emoji
        out.emplace_back("\xff");              // invalid UTF-8
        return out;
    }();
    return table;
}

}

Value Interp::draw(const GenV& g) {
    if (!tape) throw BentError(g.loc, "generators can only be drawn inside forall (...) of a law");
    Tape& t = *tape;
    const size_t span = t.beginSpan();
    Value out;
    switch (g.kind) {
    case GenKind::Int: {
        // shape 0: the target (0 or the bound nearest it); otherwise sign and magnitude, the magnitude always recorded over the full
        // span: 1, 2 force the far end (lo / hi), 3..5 force a small offset, 6..9 are uniform.
        const int64_t target = clampTarget(g.lo, g.hi);
        const uint64_t up = static_cast<uint64_t>(g.hi) - static_cast<uint64_t>(target);
        const uint64_t down = static_cast<uint64_t>(target) - static_cast<uint64_t>(g.lo);
        const uint64_t shape = t.draw(9);
        int64_t value = target;
        if (shape != 0 && (up > 0 || down > 0)) {
            bool negative = up == 0 ? true : (down == 0 ? false : (shape == 1 ? t.drawForced(1, 1) == 1 : (shape == 2 ? t.drawForced(1, 0) == 1 : t.draw(1) == 1)));
            const uint64_t span = negative ? down : up;
            uint64_t magnitude = 0;
            if (shape <= 2) magnitude = t.drawForced(span, span);
            else if (shape <= 5) magnitude = t.drawForced(span, t.draw(std::min<uint64_t>(span, 16)));
            else magnitude = t.draw(span);
            value = negative ? static_cast<int64_t>(static_cast<uint64_t>(target) - magnitude) : static_cast<int64_t>(static_cast<uint64_t>(target) + magnitude);
        }
        out = Value::integer(value);
        break;
    }
    case GenKind::Float: {
        // As int: shape 0 is the target; 1, 2 the far ends; 3 a small multiple of 0.5; 4..9 uniform. The fraction choice always spans
        // the full side, so shrinking it walks the value toward the target.
        const double target = std::clamp(0.0, g.flo, g.fhi);
        const double up = g.fhi - target;
        const double down = target - g.flo;
        const uint64_t shape = t.draw(9);
        double value = target;
        constexpr uint64_t kFull = (1ULL << 53) - 1;
        if (shape != 0 && (up > 0.0 || down > 0.0)) {
            const bool negative = up <= 0.0 ? true : (down <= 0.0 ? false : (shape == 1 ? t.drawForced(1, 1) == 1 : (shape == 2 ? t.drawForced(1, 0) == 1 : t.draw(1) == 1)));
            const double span = negative ? down : up;
            uint64_t choice = 0;
            if (shape <= 2) {
                choice = t.drawForced(kFull, kFull);
            } else if (shape == 3) {
                const double step = static_cast<double>(t.draw(32)) * 0.5;
                const double fraction = span > 0.0 ? std::min(1.0, step / span) : 0.0;
                choice = t.drawForced(kFull, static_cast<uint64_t>(std::llround(fraction * static_cast<double>(kFull))));
            } else {
                choice = t.draw(kFull);
            }
            const double fraction = static_cast<double>(choice) / static_cast<double>(kFull);
            value = negative ? target - fraction * span : target + fraction * span;
            if (shape == 3 && fraction < 1.0) value = std::round(value * 2.0) / 2.0;
            value = std::clamp(value, g.flo, g.fhi);
        }
        out = Value::number(value);
        break;
    }
    case GenKind::Bool: out = Value::boolean(t.draw(1) == 1); break;
    case GenKind::Just: out = g.values.empty() ? Value::unit() : g.values[0]; break;
    case GenKind::Elements: {
        if (g.values.empty()) throw BentError(g.loc, "elements([]) has nothing to choose from");
        out = g.values[static_cast<size_t>(t.draw(g.values.size() - 1))];
        break;
    }
    case GenKind::OneOf: {
        if (g.values.empty()) throw BentError(g.loc, "oneof() needs at least one alternative");
        out = realize(g.values[static_cast<size_t>(t.draw(g.values.size() - 1))], g.loc);
        break;
    }
    case GenKind::Frequency: {
        uint64_t total = 0;
        for (const uint64_t w : g.weights) total += w;
        if (total == 0) throw BentError(g.loc, "frequency() weights sum to zero");
        uint64_t pick = t.draw(total - 1);
        size_t index = 0;
        while (index + 1 < g.weights.size() && pick >= g.weights[index]) {
            pick -= g.weights[index];
            ++index;
        }
        out = realize(g.values[index], g.loc);
        break;
    }
    case GenKind::ListOf:
    case GenKind::Text:
    case GenKind::Ident: {
        const int64_t minLen = g.lo;
        const int64_t maxLen = g.lo + static_cast<int64_t>(std::ceil(static_cast<double>(g.hi - g.lo) * t.size()));
        const uint64_t expectedExtra = static_cast<uint64_t>(std::max<int64_t>(1, (maxLen - minLen) / 2));
        ValueList items;
        std::string text;
        int64_t count = 0;
        const auto element = [&](bool first) {
            if (g.kind == GenKind::ListOf) {
                items.push_back(realize(g.values[0], g.loc));
            } else if (g.kind == GenKind::Text) {
                text += alphabet()[static_cast<size_t>(t.draw(alphabet().size() - 1))];
            } else {
                static const std::string headChars = "abcdefghijklmnopqrstuvwxyz";
                static const std::string tailChars = "abcdefghijklmnopqrstuvwxyz0123456789_";
                const std::string& pool = first ? headChars : tailChars;
                text += pool[static_cast<size_t>(t.draw(pool.size() - 1))];
            }
        };
        for (; count < minLen; ++count) element(count == 0);
        while (count < maxLen) {
            const size_t elementStart = t.beginSpan();
            if (t.draw(expectedExtra) == 0) break;
            element(count == 0);
            t.endSpan(elementStart, true);
            ++count;
        }
        out = g.kind == GenKind::ListOf ? Value::makeList(std::move(items)) : Value::string(std::move(text));
        break;
    }
    case GenKind::UnitVec: {
        const double a = static_cast<double>(t.draw((1ULL << 32) - 1)) / static_cast<double>((1ULL << 32) - 1);
        const double b = static_cast<double>(t.draw((1ULL << 32) - 1)) / static_cast<double>(1ULL << 32);
        const double y = 1.0 - 2.0 * a;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double phi = 2.0 * 3.14159265358979323846 * b;
        out = Value::vec3(r * std::cos(phi), y, r * std::sin(phi));
        break;
    }
    case GenKind::Rotation: {
        const double a = static_cast<double>(t.draw((1ULL << 32) - 1)) / static_cast<double>((1ULL << 32) - 1);
        const double b = static_cast<double>(t.draw((1ULL << 32) - 1)) / static_cast<double>(1ULL << 32);
        const double y = 1.0 - 2.0 * a;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double phi = 2.0 * 3.14159265358979323846 * b;
        // Angle: 0 first, then multiples of 45 degrees, then anything.
        const uint64_t shape = t.draw(3);
        double degrees = 0.0;
        if (shape == 1) degrees = 45.0 * static_cast<double>(t.draw(7));
        else if (shape >= 2) degrees = 360.0 * static_cast<double>(t.draw((1ULL << 32) - 1)) / static_cast<double>(1ULL << 32);
        const double half = degrees * 3.14159265358979323846 / 360.0;
        const double s = std::sin(half);
        out = Value::quat(std::cos(half), r * std::cos(phi) * s, y * s, r * std::sin(phi) * s);
        break;
    }
    case GenKind::Nasty: {
        const uint64_t which = t.draw(9);
        if (which < 8) {
            out = realize(g.values[0], g.loc);
        } else {
            constexpr size_t count = sizeof(kNasty) / sizeof(kNasty[0]);
            out = Value::number(kNasty[t.draw(count - 1)]);
        }
        break;
    }
    case GenKind::Map: {
        const Value x = realize(g.values[0], g.loc);
        out = call(g.fn, {x}, {}, g.loc);
        break;
    }
    case GenKind::Filter: {
        bool found = false;
        for (int attempt = 0; attempt < 100 && !found; ++attempt) {
            const Value x = realize(g.values[0], g.loc);
            const Value keep = call(g.fn, {x}, {}, g.loc);
            if (truthy(keep, g.loc)) {
                out = x;
                found = true;
            }
        }
        if (!found) throw Discard{"filter at " + g.loc.str() + " rejected 100 candidates"};
        break;
    }
    case GenKind::Bind: {
        const Value x = realize(g.values[0], g.loc);
        out = realize(call(g.fn, {x}, {}, g.loc), g.loc);
        break;
    }
    case GenKind::Delay: out = realize(call(g.fn, {}, {}, g.loc), g.loc); break;
    case GenKind::Lift: {
        ValueList args;
        args.reserve(g.values.size());
        for (const Value& arg : g.values) args.push_back(realize(arg, g.loc));
        NamedArgs named;
        for (const auto& [key, arg] : g.named) named.emplace_back(key, realize(arg, g.loc));
        out = g.lift(*this, args, named, g.loc);
        break;
    }
    case GenKind::ListLit: {
        ValueList items;
        for (const Value& item : g.values) items.push_back(realize(item, g.loc));
        out = Value::makeList(std::move(items));
        break;
    }
    case GenKind::RecordLit: {
        NamedArgs fields;
        for (const auto& [key, item] : g.named) fields.emplace_back(key, realize(item, g.loc));
        out = Value::record(std::move(fields));
        break;
    }
    case GenKind::Subseq: {
        ValueList items;
        for (const Value& item : g.values) {
            if (t.draw(1) == 1) items.push_back(item);
        }
        out = Value::makeList(std::move(items));
        break;
    }
    }
    t.endSpan(span);
    return out;
}

}
