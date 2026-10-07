module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

module Engine.Storylet;

namespace {
bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

std::vector<std::string> split(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ') ++j;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool parseValue(const std::string& token, double& out) {
    if (token == "true") { out = 1.0; return true; }
    if (token == "false") { out = 0.0; return true; }
    const auto r = std::from_chars(token.data(), token.data() + token.size(), out);
    return r.ec == std::errc() && r.ptr == token.data() + token.size() && std::isfinite(out);
}

struct Parser {
    const StateStore& state;
    std::string why;

    bool known(const std::string& name) {
        if (state.has(name)) return true;
        why = "unknown variable " + name;
        return false;
    }

    bool condition(const Json& j, StoryletCondition& out, int depth) {
        if (depth > 16) { why = "condition nesting too deep"; return false; }
        if (j.isString()) return conditionText(j.asString(), out);
        if (j.isArray()) {
            out.op = StoryletCondition::Op::All;
            for (const Json& item : j.items()) {
                StoryletCondition c;
                if (!condition(item, c, depth + 1)) return false;
                out.children.push_back(std::move(c));
            }
            return true;
        }
        if (!j.isObject() || j.size() != 1) { why = "condition object needs exactly one key"; return false; }
        const auto& [key, value] = j.members().front();
        if (key == "all" || key == "any") {
            out.op = key == "all" ? StoryletCondition::Op::All : StoryletCondition::Op::Any;
            if (!value.isArray()) { why = key + " needs an array"; return false; }
            for (const Json& item : value.items()) {
                StoryletCondition c;
                if (!condition(item, c, depth + 1)) return false;
                out.children.push_back(std::move(c));
            }
            return true;
        }
        if (key == "not") {
            out.op = StoryletCondition::Op::Not;
            StoryletCondition c;
            if (!condition(value, c, depth + 1)) return false;
            out.children.push_back(std::move(c));
            return true;
        }
        if (key == "played" || key == "not_played") {
            if (!value.isString()) { why = key + " needs a storylet id"; return false; }
            StoryletCondition played;
            played.op = StoryletCondition::Op::Played;
            played.name = value.asString();
            if (key == "played") { out = std::move(played); return true; }
            out.op = StoryletCondition::Op::Not;
            out.children.push_back(std::move(played));
            return true;
        }
        if (!known(key)) return false;
        const StateVar* v = state.var(key);
        if (v->kind != VarKind::Set || (!value.isString() && !value.isArray())) { why = "member test needs a set variable and string: " + key; return false; }
        out.op = StoryletCondition::Op::Member;
        out.name = key;
        if (value.isString()) { out.member = value.asString(); return true; }
        out.op = StoryletCondition::Op::Any;
        for (const Json& m : value.items()) {
            if (!m.isString()) { why = "member list needs strings"; return false; }
            StoryletCondition c;
            c.op = StoryletCondition::Op::Member;
            c.name = key;
            c.member = m.asString();
            out.children.push_back(std::move(c));
        }
        return true;
    }

    bool conditionText(const std::string& text, StoryletCondition& out) {
        const auto t = split(text);
        if (t.size() == 1) {
            const bool neg = t[0][0] == '!';
            const std::string name = neg ? t[0].substr(1) : t[0];
            if (!known(name)) return false;
            StoryletCondition flag;
            flag.op = StoryletCondition::Op::Flag;
            flag.name = name;
            if (!neg) { out = std::move(flag); return true; }
            out.op = StoryletCondition::Op::Not;
            out.children.push_back(std::move(flag));
            return true;
        }
        if (t.size() != 3) { why = "bad condition '" + text + "'"; return false; }
        if (!known(t[0])) return false;
        static const std::pair<const char*, StoryletCondition::Cmp> ops[] = {{">=", StoryletCondition::Cmp::Ge}, {"<=", StoryletCondition::Cmp::Le}, {">", StoryletCondition::Cmp::Gt}, {"<", StoryletCondition::Cmp::Lt}, {"==", StoryletCondition::Cmp::Eq}, {"!=", StoryletCondition::Cmp::Ne}};
        bool found = false;
        for (const auto& [name, op] : ops) {
            if (t[1] == name) { out.cmp = op; found = true; }
        }
        if (!found || !parseValue(t[2], out.number)) { why = "bad condition '" + text + "'"; return false; }
        out.op = StoryletCondition::Op::Compare;
        out.name = t[0];
        return true;
    }

    bool effect(const Json& j, StoryletEffect& out) {
        if (j.isString()) {
            const auto t = split(j.asString());
            if (t.size() != 3 || !known(t[0])) { why = "bad effect '" + j.asString() + "'"; return false; }
            const StateVar* v = state.var(t[0]);
            if (v->kind == VarKind::Set) { why = "use add_member for set " + t[0]; return false; }
            double n = 0.0;
            if (!parseValue(t[2], n)) { why = "bad effect '" + j.asString() + "'"; return false; }
            out.name = t[0];
            if (t[1] == "=") out.op = StoryletEffect::Op::Set;
            else if (t[1] == "+=") out.op = StoryletEffect::Op::Add;
            else if (t[1] == "-=") { out.op = StoryletEffect::Op::Add; n = -n; }
            else { why = "bad effect '" + j.asString() + "'"; return false; }
            out.number = n;
            return true;
        }
        if (!j.isObject() || j.size() != 1) { why = "effect object needs exactly one key"; return false; }
        const auto& [key, value] = j.members().front();
        if (key == "remember") {
            if (!value.isString()) { why = "remember needs a string"; return false; }
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

bool compare(StoryletCondition::Cmp c, double a, double b) {
    switch (c) {
        case StoryletCondition::Cmp::Ge: return a >= b;
        case StoryletCondition::Cmp::Le: return a <= b;
        case StoryletCondition::Cmp::Gt: return a > b;
        case StoryletCondition::Cmp::Lt: return a < b;
        case StoryletCondition::Cmp::Eq: return a == b;
        case StoryletCondition::Cmp::Ne: return a != b;
    }
    return false;
}
}

StoryletEngine::StoryletEngine(StateStore& state, RandomService& random, EventBus* bus, std::string memorySet)
    : m_state(state), m_random(random), m_bus(bus), m_memorySet(std::move(memorySet)) {
    if (!m_state.has(m_memorySet)) m_state.declare(m_memorySet, VarKind::Set);
}

void StoryletEngine::clear() {
    m_storylets.clear();
    resetHistory();
}

void StoryletEngine::resetHistory() {
    m_played.clear();
    m_resume.clear();
    m_blocked.clear();
    m_current.reset();
    m_time = 0.0;
}

bool StoryletEngine::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) return fail(error, "cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return loadText(ss.str(), error);
}

bool StoryletEngine::loadText(std::string_view text, std::string* error) {
    std::string parseError;
    const auto doc = Json::parse(text, &parseError);
    if (!doc) return fail(error, "storylets: " + parseError);
    const Json* list = doc->isObject() ? doc->get("storylets") : (doc->isArray() ? &*doc : nullptr);
    if (!list || !list->isArray()) return fail(error, "storylets: expected storylets array");

    std::vector<Storylet> parsed;
    std::set<std::string> ids;
    for (const Json& def : list->items()) {
        if (!def.isObject()) return fail(error, "storylets: entry must be an object");
        const Json* id = def.get("id");
        if (!id || !id->isString() || id->asString().empty()) return fail(error, "storylets: entry without id");
        const std::string where = "storylet " + id->asString() + ": ";
        if (!ids.insert(id->asString()).second || find(id->asString())) return fail(error, where + "duplicate id");
        Storylet s;
        s.id = id->asString();
        Parser parser{m_state, {}};
        for (const auto& [key, value] : def.members()) {
            if (key == "id") continue;
            if (key == "when") {
                if (!parser.condition(value, s.when, 0)) return fail(error, where + parser.why);
            } else if (key == "priority") {
                if (!value.isNumber()) return fail(error, where + "priority must be a number");
                s.priority = static_cast<int>(value.asNumber());
            } else if (key == "cooldown_seconds") {
                if (!value.isNumber() || value.asNumber() < 0.0) return fail(error, where + "cooldown_seconds must be >= 0");
                s.cooldownSeconds = value.asNumber();
            } else if (key == "once") {
                if (!value.isBool()) return fail(error, where + "once must be a bool");
                s.once = value.asBool();
            } else if (key == "interruptible") {
                if (!value.isBool()) return fail(error, where + "interruptible must be a bool");
                s.interruptible = value.asBool();
            } else if (key == "delivery") {
                if (!value.isString()) return fail(error, where + "delivery must be a string");
                s.delivery = value.asString();
            } else if (key == "lines") {
                if (!value.isArray()) return fail(error, where + "lines must be an array");
                for (const Json& l : value.items()) {
                    if (!l.isObject()) return fail(error, where + "line must be an object");
                    StoryletLine line;
                    for (const auto& [lk, lv] : l.members()) {
                        if (lk == "speaker" && lv.isString()) line.speaker = lv.asString();
                        else if (lk == "text_id" && lv.isString()) line.textId = lv.asString();
                        else if (lk == "voice_id" && lv.isString()) line.voiceId = lv.asString();
                        else if (lk == "seconds" && lv.isNumber() && lv.asNumber() >= 0.0) line.seconds = static_cast<float>(lv.asNumber());
                        else return fail(error, where + "bad line field " + lk);
                    }
                    if (line.textId.empty()) return fail(error, where + "line needs text_id");
                    s.lines.push_back(std::move(line));
                }
            } else if (key == "effects") {
                if (!value.isArray()) return fail(error, where + "effects must be an array");
                for (const Json& e : value.items()) {
                    StoryletEffect effect;
                    if (!parser.effect(e, effect)) return fail(error, where + parser.why);
                    s.effects.push_back(std::move(effect));
                }
            } else {
                return fail(error, where + "unknown key " + key);
            }
        }
        parsed.push_back(std::move(s));
    }
    for (auto& s : parsed) m_storylets.push_back(std::move(s));
    std::sort(m_storylets.begin(), m_storylets.end(), [](const Storylet& a, const Storylet& b) { return a.id < b.id; });
    return true;
}

const Storylet* StoryletEngine::find(const std::string& id) const {
    const auto it = std::lower_bound(m_storylets.begin(), m_storylets.end(), id, [](const Storylet& s, const std::string& key) { return s.id < key; });
    return it != m_storylets.end() && it->id == id ? &*it : nullptr;
}

uint32_t StoryletEngine::playCount(const std::string& id) const {
    const auto it = m_played.find(id);
    return it == m_played.end() ? 0 : it->second.count;
}

bool StoryletEngine::evaluate(const StoryletCondition& c) const {
    using Op = StoryletCondition::Op;
    switch (c.op) {
        case Op::All: return std::all_of(c.children.begin(), c.children.end(), [this](const auto& x) { return evaluate(x); });
        case Op::Any: return std::any_of(c.children.begin(), c.children.end(), [this](const auto& x) { return evaluate(x); });
        case Op::Not: return c.children.empty() || !evaluate(c.children.front());
        case Op::Compare: return compare(c.cmp, m_state.get(c.name), c.number);
        case Op::Flag: return m_state.get(c.name) != 0.0;
        case Op::Member: return m_state.hasMember(c.name, c.member);
        case Op::Played: return m_played.contains(c.name);
    }
    return false;
}

bool StoryletEngine::available(const Storylet& s) const {
    if (s.once && m_played.contains(s.id)) return false;
    if (const auto it = m_played.find(s.id); it != m_played.end() && s.cooldownSeconds > 0.0 && m_time - it->second.last < s.cooldownSeconds) return false;
    if (const auto it = m_blocked.find(s.id); it != m_blocked.end() && m_time < it->second) return false;
    return evaluate(s.when);
}

bool StoryletEngine::eligible(const std::string& id) const {
    const Storylet* s = find(id);
    return s && available(*s);
}

std::vector<std::string> StoryletEngine::eligibleIds() const {
    std::vector<std::string> out;
    for (const Storylet& s : m_storylets) {
        if (available(s)) out.push_back(s.id);
    }
    return out;
}

void StoryletEngine::beginLine(const Storylet& s) {
    m_current->elapsed = 0.0f;
    if (!m_bus) return;
    const StoryletLine& l = s.lines[m_current->line];
    m_bus->emitNow(StoryletLineStarted{s.id, m_current->line, l.speaker, l.textId, l.voiceId});
}

void StoryletEngine::start(const Storylet& s) {
    const auto resume = m_resume.find(s.id);
    const uint32_t line = resume == m_resume.end() ? 0 : resume->second;
    m_current = StoryletPlayback{s.id, line, 0.0f};
    if (m_bus) m_bus->emitNow(StoryletStarted{s.id, resume != m_resume.end()});
    if (resume != m_resume.end()) m_resume.erase(resume);
    if (s.lines.empty()) complete(s);
    else beginLine(s);
}

void StoryletEngine::applyEffects(const Storylet& s) {
    for (const StoryletEffect& e : s.effects) {
        switch (e.op) {
            case StoryletEffect::Op::Set: m_state.set(e.name, e.number); break;
            case StoryletEffect::Op::Add: m_state.add(e.name, e.number); break;
            case StoryletEffect::Op::Remember: m_state.addMember(m_memorySet, e.member); break;
            case StoryletEffect::Op::AddMember: m_state.addMember(e.name, e.member); break;
            case StoryletEffect::Op::RemoveMember: m_state.removeMember(e.name, e.member); break;
        }
    }
}

void StoryletEngine::complete(const Storylet& s) {
    m_current.reset();
    m_resume.erase(s.id);
    Record& r = m_played[s.id];
    ++r.count;
    r.last = m_time;
    applyEffects(s);
    if (m_bus) m_bus->emitNow(StoryletCompleted{s.id});
}

bool StoryletEngine::interrupt(const std::string& reason) {
    if (!m_current) return false;
    const Storylet* s = find(m_current->id);
    if (!s || !s->interruptible) return false;
    const StoryletPlayback p = *m_current;
    m_current.reset();
    m_resume[p.id] = p.line;
    m_blocked[p.id] = m_time + m_retryDelay;
    if (m_bus) {
        m_bus->emitNow(StoryletLineEnded{p.id, p.line, true});
        m_bus->emitNow(StoryletInterrupted{p.id, reason, p.line});
    }
    return true;
}

void StoryletEngine::select() {
    const Storylet* best = nullptr;
    std::vector<const Storylet*> ties;
    for (const Storylet& s : m_storylets) {
        if (!available(s)) continue;
        if (!best || s.priority > best->priority) {
            best = &s;
            ties.assign(1, &s);
        } else if (s.priority == best->priority) {
            ties.push_back(&s);
        }
    }
    if (!best) return;
    if (m_current) {
        const Storylet* cur = find(m_current->id);
        if (!cur || best->priority <= cur->priority || !cur->interruptible) return;
        interrupt("outranked");
    }
    const Storylet* pick = ties.size() == 1 ? ties.front() : ties[m_random.stream("storylet").below(static_cast<uint32_t>(ties.size()))];
    start(*pick);
}

void StoryletEngine::update(float dt) {
    m_time += dt;
    select();
    float remaining = dt;
    while (m_current && remaining >= 0.0f) {
        const Storylet* s = find(m_current->id);
        if (!s) { m_current.reset(); break; }
        const float length = s->lines[m_current->line].seconds;
        const float need = length - m_current->elapsed;
        if (remaining < need) {
            m_current->elapsed += remaining;
            break;
        }
        remaining -= need;
        if (m_bus) m_bus->emitNow(StoryletLineEnded{s->id, m_current->line, false});
        if (m_current->line + 1 >= s->lines.size()) {
            complete(*s);
            break;
        }
        ++m_current->line;
        beginLine(*s);
    }
}

Json StoryletEngine::toJson() const {
    Json out = Json::object();
    out.set("time", m_time);
    Json played = Json::object();
    for (const auto& [id, r] : m_played) {
        Json e = Json::object();
        e.set("count", r.count);
        e.set("last", r.last);
        played.set(id, std::move(e));
    }
    out.set("played", std::move(played));
    Json resume = Json::object();
    for (const auto& [id, line] : m_resume) resume.set(id, line);
    out.set("resume", std::move(resume));
    Json blocked = Json::object();
    for (const auto& [id, until] : m_blocked) blocked.set(id, until);
    out.set("blocked", std::move(blocked));
    if (m_current) {
        Json cur = Json::object();
        cur.set("id", m_current->id);
        cur.set("line", m_current->line);
        cur.set("elapsed", m_current->elapsed);
        out.set("current", std::move(cur));
    }
    return out;
}

bool StoryletEngine::fromJson(const Json& doc, std::string* error) {
    if (!doc.isObject()) return fail(error, "storylet history: not an object");
    resetHistory();
    if (const Json* t = doc.get("time")) m_time = t->asNumber();
    if (const Json* played = doc.get("played")) {
        for (const auto& [id, e] : played->members()) {
            Record r;
            if (const Json* c = e.get("count")) r.count = static_cast<uint32_t>(c->asNumber());
            if (const Json* l = e.get("last")) r.last = l->asNumber();
            m_played[id] = r;
        }
    }
    if (const Json* resume = doc.get("resume")) {
        for (const auto& [id, v] : resume->members()) m_resume[id] = static_cast<uint32_t>(v.asNumber());
    }
    if (const Json* blocked = doc.get("blocked")) {
        for (const auto& [id, v] : blocked->members()) m_blocked[id] = v.asNumber();
    }
    if (const Json* cur = doc.get("current")) {
        const Json* id = cur->get("id");
        const Storylet* s = id ? find(id->asString()) : nullptr;
        if (!s) return fail(error, "storylet history: unknown current storylet");
        StoryletPlayback p{s->id, 0, 0.0f};
        if (const Json* l = cur->get("line")) p.line = static_cast<uint32_t>(l->asNumber());
        if (const Json* e = cur->get("elapsed")) p.elapsed = static_cast<float>(e->asNumber());
        if (p.line >= s->lines.size()) return fail(error, "storylet history: bad line index");
        m_current = p;
    }
    return true;
}
