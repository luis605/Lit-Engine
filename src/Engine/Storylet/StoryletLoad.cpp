module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module Engine.Storylet;
import Engine.Json;

namespace {
constexpr int MAX_CONDITION_DEPTH = 16;
constexpr double kMaxPriority = 1.0e9;

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

std::vector<std::string> splitWords(std::string_view text) {
    std::vector<std::string> words;
    size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && text[position] == ' ') ++position;
        size_t wordEnd = position;
        while (wordEnd < text.size() && text[wordEnd] != ' ') ++wordEnd;
        if (wordEnd > position) words.emplace_back(text.substr(position, wordEnd - position));
        position = wordEnd;
    }
    return words;
}

bool parseValue(const std::string& token, double& out) {
    if (token == "true") {
        out = 1.0;
        return true;
    }
    if (token == "false") {
        out = 0.0;
        return true;
    }
    const char* tokenEnd = token.data() + token.size();
    const auto result = std::from_chars(token.data(), tokenEnd, out);
    return result.ec == std::errc() && result.ptr == tokenEnd && std::isfinite(out);
}

struct Parser {
    const StateStore& state;
    std::string why;

    bool known(const std::string& name) {
        if (state.has(name)) return true;
        why = "unknown variable " + name;
        return false;
    }

    bool childConditions(const Json& list, StoryletCondition& out, int depth) {
        for (const Json& item : list.items()) {
            StoryletCondition child;
            if (!condition(item, child, depth + 1)) return false;
            out.children.push_back(std::move(child));
        }
        return true;
    }

    bool memberCondition(const std::string& key, const Json& value, StoryletCondition& out) {
        if (!known(key)) return false;
        const StateVar* variable = state.var(key);
        if (variable->kind != VarKind::Set || (!value.isString() && !value.isArray())) {
            why = "member test needs a set variable and string: " + key;
            return false;
        }
        out.op = StoryletCondition::Op::Member;
        out.name = key;
        if (value.isString()) {
            out.member = value.asString();
            return true;
        }
        out.op = StoryletCondition::Op::Any;
        for (const Json& memberJson : value.items()) {
            if (!memberJson.isString()) {
                why = "member list needs strings";
                return false;
            }
            StoryletCondition child;
            child.op = StoryletCondition::Op::Member;
            child.name = key;
            child.member = memberJson.asString();
            out.children.push_back(std::move(child));
        }
        return true;
    }

    bool condition(const Json& json, StoryletCondition& out, int depth) {
        if (depth > MAX_CONDITION_DEPTH) {
            why = "condition nesting too deep";
            return false;
        }
        if (json.isString()) return conditionText(json.asString(), out);
        if (json.isArray()) {
            out.op = StoryletCondition::Op::All;
            return childConditions(json, out, depth);
        }
        if (!json.isObject() || json.size() != 1) {
            why = "condition object needs exactly one key";
            return false;
        }
        const auto& [key, value] = json.members().front();
        if (key == "all" || key == "any") {
            out.op = key == "all" ? StoryletCondition::Op::All : StoryletCondition::Op::Any;
            if (!value.isArray()) {
                why = key + " needs an array";
                return false;
            }
            return childConditions(value, out, depth);
        }
        if (key == "not") {
            out.op = StoryletCondition::Op::Not;
            StoryletCondition child;
            if (!condition(value, child, depth + 1)) return false;
            out.children.push_back(std::move(child));
            return true;
        }
        if (key == "played" || key == "not_played") {
            if (!value.isString()) {
                why = key + " needs a storylet id";
                return false;
            }
            StoryletCondition played;
            played.op = StoryletCondition::Op::Played;
            played.name = value.asString();
            if (key == "played") {
                out = std::move(played);
                return true;
            }
            out.op = StoryletCondition::Op::Not;
            out.children.push_back(std::move(played));
            return true;
        }
        return memberCondition(key, value, out);
    }

    bool conditionText(const std::string& text, StoryletCondition& out) {
        const auto words = splitWords(text);
        if (words.size() == 1) {
            const bool negated = words[0][0] == '!';
            const std::string name = negated ? words[0].substr(1) : words[0];
            if (!known(name)) return false;
            StoryletCondition flag;
            flag.op = StoryletCondition::Op::Flag;
            flag.name = name;
            if (!negated) {
                out = std::move(flag);
                return true;
            }
            out.op = StoryletCondition::Op::Not;
            out.children.push_back(std::move(flag));
            return true;
        }
        if (words.size() != 3) {
            why = "bad condition '" + text + "'";
            return false;
        }
        if (!known(words[0])) return false;
        static const std::pair<const char*, StoryletCondition::Cmp> comparisons[] = {
            {">=", StoryletCondition::Cmp::Ge}, {"<=", StoryletCondition::Cmp::Le}, {">", StoryletCondition::Cmp::Gt},
            {"<", StoryletCondition::Cmp::Lt},  {"==", StoryletCondition::Cmp::Eq}, {"!=", StoryletCondition::Cmp::Ne}};
        bool found = false;
        for (const auto& [symbol, comparison] : comparisons) {
            if (words[1] == symbol) {
                out.cmp = comparison;
                found = true;
            }
        }
        if (!found || !parseValue(words[2], out.number)) {
            why = "bad condition '" + text + "'";
            return false;
        }
        out.op = StoryletCondition::Op::Compare;
        out.name = words[0];
        return true;
    }

    bool effectText(const std::string& text, StoryletEffect& out) {
        const auto words = splitWords(text);
        if (words.size() != 3 || !known(words[0])) {
            why = "bad effect '" + text + "'";
            return false;
        }
        if (state.var(words[0])->kind == VarKind::Set) {
            why = "use add_member for set " + words[0];
            return false;
        }
        double amount = 0.0;
        if (!parseValue(words[2], amount)) {
            why = "bad effect '" + text + "'";
            return false;
        }
        out.name = words[0];
        if (words[1] == "=") {
            out.op = StoryletEffect::Op::Set;
        } else if (words[1] == "+=") {
            out.op = StoryletEffect::Op::Add;
        } else if (words[1] == "-=") {
            out.op = StoryletEffect::Op::Add;
            amount = -amount;
        } else {
            why = "bad effect '" + text + "'";
            return false;
        }
        out.number = amount;
        return true;
    }

    bool effect(const Json& json, StoryletEffect& out) {
        if (json.isString()) return effectText(json.asString(), out);
        if (!json.isObject() || json.size() != 1) {
            why = "effect object needs exactly one key";
            return false;
        }
        const auto& [key, value] = json.members().front();
        if (key == "remember") {
            if (!value.isString()) {
                why = "remember needs a string";
                return false;
            }
            out.op = StoryletEffect::Op::Remember;
            out.member = value.asString();
            return true;
        }
        if (key == "add_member" || key == "remove_member") {
            const Json* set = value.get("set");
            const Json* member = value.get("member");
            if (!set || !member || !set->isString() || !member->isString() || !known(set->asString()) || state.var(set->asString())->kind != VarKind::Set) {
                if (why.empty()) why = key + " needs set and member";
                return false;
            }
            out.op = key == "add_member" ? StoryletEffect::Op::AddMember : StoryletEffect::Op::RemoveMember;
            out.name = set->asString();
            out.member = member->asString();
            return true;
        }
        why = "unknown effect " + key;
        return false;
    }
};

bool parseLine(const Json& lineJson, StoryletLine& line, std::string& badField) {
    for (const auto& [key, value] : lineJson.members()) {
        if (key == "speaker" && value.isString()) line.speaker = value.asString();
        else if (key == "text_id" && value.isString()) line.textId = value.asString();
        else if (key == "voice_id" && value.isString()) line.voiceId = value.asString();
        else if (key == "seconds" && value.isNumber() && value.asNumber() >= 0.0) line.seconds = static_cast<float>(value.asNumber());
        else {
            badField = key;
            return false;
        }
    }
    return true;
}

bool parseLines(const Json& value, Storylet& storylet, const std::string& where, std::string* error) {
    if (!value.isArray()) return fail(error, where + "lines must be an array");
    for (const Json& lineJson : value.items()) {
        if (!lineJson.isObject()) return fail(error, where + "line must be an object");
        StoryletLine line;
        std::string badField;
        if (!parseLine(lineJson, line, badField)) return fail(error, where + "bad line field " + badField);
        if (line.textId.empty()) return fail(error, where + "line needs text_id");
        storylet.lines.push_back(std::move(line));
    }
    return true;
}

bool parseEffects(const Json& value, Parser& parser, Storylet& storylet, const std::string& where, std::string* error) {
    if (!value.isArray()) return fail(error, where + "effects must be an array");
    for (const Json& effectJson : value.items()) {
        StoryletEffect effect;
        if (!parser.effect(effectJson, effect)) return fail(error, where + parser.why);
        storylet.effects.push_back(std::move(effect));
    }
    return true;
}

bool parseField(const std::string& key, const Json& value, Parser& parser, Storylet& storylet, const std::string& where, std::string* error) {
    if (key == "when") {
        if (!parser.condition(value, storylet.when, 0)) return fail(error, where + parser.why);
    } else if (key == "priority") {
        if (!value.isNumber()) return fail(error, where + "priority must be a number");
        storylet.priority = static_cast<int>(std::clamp(value.asNumber(), -kMaxPriority, kMaxPriority));
    } else if (key == "cooldown_seconds") {
        if (!value.isNumber() || value.asNumber() < 0.0) return fail(error, where + "cooldown_seconds must be >= 0");
        storylet.cooldownSeconds = value.asNumber();
    } else if (key == "once") {
        if (!value.isBool()) return fail(error, where + "once must be a bool");
        storylet.once = value.asBool();
    } else if (key == "interruptible") {
        if (!value.isBool()) return fail(error, where + "interruptible must be a bool");
        storylet.interruptible = value.asBool();
    } else if (key == "delivery") {
        if (!value.isString()) return fail(error, where + "delivery must be a string");
        storylet.delivery = value.asString();
    } else if (key == "lines") {
        return parseLines(value, storylet, where, error);
    } else if (key == "effects") {
        return parseEffects(value, parser, storylet, where, error);
    } else {
        return fail(error, where + "unknown key " + key);
    }
    return true;
}
}

bool StoryletEngine::loadFile(const std::string& path, std::string* error) {
    const std::optional<std::string> text = readTextFile(path);
    if (!text) return fail(error, "cannot open " + path);
    return loadText(*text, error);
}

bool StoryletEngine::loadText(std::string_view text, std::string* error) {
    std::string parseError;
    const auto doc = Json::parse(text, &parseError);
    if (!doc) return fail(error, "storylets: " + parseError);
    const Json* list = doc->isObject() ? doc->get("storylets") : (doc->isArray() ? &*doc : nullptr);
    if (!list || !list->isArray()) return fail(error, "storylets: expected storylets array");

    std::vector<Storylet> parsed;
    std::set<std::string> ids;
    for (const Json& definition : list->items()) {
        if (!definition.isObject()) return fail(error, "storylets: entry must be an object");
        const Json* idJson = definition.get("id");
        if (!idJson || !idJson->isString() || idJson->asString().empty()) return fail(error, "storylets: entry without id");
        const std::string where = "storylet " + idJson->asString() + ": ";
        if (!ids.insert(idJson->asString()).second || find(idJson->asString())) return fail(error, where + "duplicate id");
        Storylet storylet;
        storylet.id = idJson->asString();
        Parser parser{m_state, {}};
        for (const auto& [key, value] : definition.members()) {
            if (key == "id") continue;
            if (!parseField(key, value, parser, storylet, where, error)) return false;
        }
        parsed.push_back(std::move(storylet));
    }
    m_storylets.reserve(m_storylets.size() + parsed.size());
    for (auto& storylet : parsed) m_storylets.push_back(std::move(storylet));
    std::sort(m_storylets.begin(), m_storylets.end(), [](const Storylet& lhs, const Storylet& rhs) { return lhs.id < rhs.id; });
    return true;
}
