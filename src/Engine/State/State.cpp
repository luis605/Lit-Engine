module;

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

module Engine.State;

namespace {
bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

bool kindFromName(std::string_view s, VarKind& out) {
    if (s == "range") out = VarKind::Range;
    else if (s == "flag") out = VarKind::Flag;
    else if (s == "counter") out = VarKind::Counter;
    else if (s == "set") out = VarKind::Set;
    else return false;
    return true;
}

const char* kindName(VarKind k) {
    switch (k) {
        case VarKind::Range: return "range";
        case VarKind::Flag: return "flag";
        case VarKind::Counter: return "counter";
        case VarKind::Set: return "set";
    }
    return "range";
}
}

bool StateStore::declare(const std::string& name, VarKind kind, double minimum, double maximum, double initial) {
    if (name.empty() || minimum > maximum) return false;
    StateVar v;
    v.kind = kind;
    if (kind == VarKind::Flag) {
        v.minimum = 0.0;
        v.maximum = 1.0;
        initial = initial != 0.0 ? 1.0 : 0.0;
    } else if (kind == VarKind::Set) {
        v.minimum = 0.0;
        v.maximum = 0.0;
        initial = 0.0;
    } else {
        v.minimum = minimum;
        v.maximum = maximum;
        if (kind == VarKind::Counter) {
            v.minimum = std::floor(minimum);
            v.maximum = std::floor(maximum);
            initial = std::floor(initial);
        }
        initial = std::clamp(initial, v.minimum, v.maximum);
    }
    v.initial = initial;
    v.value = initial;
    m_vars[name] = std::move(v);
    return true;
}

const StateVar* StateStore::var(const std::string& name) const {
    const auto it = m_vars.find(name);
    return it == m_vars.end() ? nullptr : &it->second;
}

double StateStore::get(const std::string& name) const {
    const StateVar* v = var(name);
    if (!v) return 0.0;
    return v->kind == VarKind::Set ? static_cast<double>(v->members.size()) : v->value;
}

bool StateStore::hasMember(const std::string& name, const std::string& member) const {
    const StateVar* v = var(name);
    return v && v->kind == VarKind::Set && v->members.contains(member);
}

std::vector<std::string> StateStore::members(const std::string& name) const {
    const StateVar* v = var(name);
    if (!v || v->kind != VarKind::Set) return {};
    return {v->members.begin(), v->members.end()};
}

void StateStore::notify(const std::string& name, const StateVar& v, double oldValue, const std::string& member, bool added) {
    if (!m_bus) return;
    m_bus->emitNow(StateChanged{name, v.kind, oldValue, v.kind == VarKind::Set ? static_cast<double>(v.members.size()) : v.value, member, added});
}

bool StateStore::set(const std::string& name, double value) {
    const auto it = m_vars.find(name);
    if (it == m_vars.end() || it->second.kind == VarKind::Set || !std::isfinite(value)) return false;
    StateVar& v = it->second;
    if (v.kind == VarKind::Counter) value = std::floor(value);
    if (v.kind == VarKind::Flag) value = value != 0.0 ? 1.0 : 0.0;
    value = std::clamp(value, v.minimum, v.maximum);
    if (value == v.value) return true;
    const double old = v.value;
    v.value = value;
    notify(name, v, old, {}, false);
    return true;
}

bool StateStore::add(const std::string& name, double delta) {
    const StateVar* v = var(name);
    if (!v || v->kind == VarKind::Set || v->kind == VarKind::Flag) return false;
    return set(name, v->value + delta);
}

bool StateStore::addMember(const std::string& name, const std::string& member) {
    const auto it = m_vars.find(name);
    if (it == m_vars.end() || it->second.kind != VarKind::Set) return false;
    StateVar& v = it->second;
    const double old = static_cast<double>(v.members.size());
    if (v.members.insert(member).second) notify(name, v, old, member, true);
    return true;
}

bool StateStore::removeMember(const std::string& name, const std::string& member) {
    const auto it = m_vars.find(name);
    if (it == m_vars.end() || it->second.kind != VarKind::Set) return false;
    StateVar& v = it->second;
    const double old = static_cast<double>(v.members.size());
    if (v.members.erase(member)) notify(name, v, old, member, false);
    return true;
}

void StateStore::clearSet(const std::string& name) {
    const auto it = m_vars.find(name);
    if (it == m_vars.end() || it->second.kind != VarKind::Set) return;
    const std::vector<std::string> all(it->second.members.begin(), it->second.members.end());
    for (const auto& m : all) removeMember(name, m);
}

void StateStore::reset() {
    for (auto& [name, v] : m_vars) {
        if (v.kind == VarKind::Set) clearSet(name);
        else set(name, v.initial);
    }
}

bool StateStore::loadSchemaFile(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) return fail(error, "cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return loadSchema(ss.str(), error);
}

bool StateStore::loadSchema(std::string_view text, std::string* error) {
    std::string parseError;
    const auto doc = Json::parse(text, &parseError);
    if (!doc) return fail(error, "state schema: " + parseError);
    if (!doc->isObject()) return fail(error, "state schema: root must be an object");
    const Json* vars = doc->get("vars");
    if (!vars || !vars->isObject()) return fail(error, "state schema: missing vars object");
    for (const auto& [key, value] : doc->members()) {
        if (key != "vars" && key != "version") return fail(error, "state schema: unknown key " + key);
    }

    std::map<std::string, StateVar> parsed;
    for (const auto& [name, def] : vars->members()) {
        const std::string where = "state schema: var " + name + ": ";
        if (name.empty()) return fail(error, "state schema: empty variable name");
        if (!def.isObject()) return fail(error, where + "must be an object");
        VarKind kind = VarKind::Range;
        const Json* type = def.get("type");
        if (!type || !type->isString() || !kindFromName(type->asString(), kind)) return fail(error, where + "type must be range, flag, counter or set");
        for (const auto& [key, value] : def.members()) {
            if (key != "type" && key != "min" && key != "max" && key != "default") return fail(error, where + "unknown key " + key);
            if (key != "type" && !value.isNumber() && !(key == "default" && value.isBool())) return fail(error, where + key + " must be a number");
        }
        double lo = 0.0;
        double hi = 1.0;
        if (kind == VarKind::Counter) hi = 1.0e9;
        if (const Json* j = def.get("min")) lo = j->asNumber();
        if (const Json* j = def.get("max")) hi = j->asNumber();
        double init = lo;
        if (const Json* j = def.get("default")) init = j->isBool() ? (j->asBool() ? 1.0 : 0.0) : j->asNumber();
        if (kind == VarKind::Flag && (def.get("min") || def.get("max"))) return fail(error, where + "flag takes no range");
        if (kind == VarKind::Set && (def.get("min") || def.get("max") || def.get("default"))) return fail(error, where + "set takes no range or default");
        if (lo > hi) return fail(error, where + "min exceeds max");
        if (kind != VarKind::Set && kind != VarKind::Flag && (init < lo || init > hi)) return fail(error, where + "default outside range");
        StateStore probe;
        if (!probe.declare(name, kind, lo, hi, init)) return fail(error, where + "invalid");
        parsed[name] = probe.m_vars[name];
    }
    for (auto& [name, v] : parsed) m_vars[name] = std::move(v);
    return true;
}

Json StateStore::toJson() const {
    Json out = Json::object();
    for (const auto& [name, v] : m_vars) {
        Json entry = Json::object();
        entry.set("type", kindName(v.kind));
        if (v.kind == VarKind::Set) {
            Json list = Json::array();
            for (const auto& m : v.members) list.push(m);
            entry.set("members", std::move(list));
        } else {
            entry.set("value", v.value);
        }
        out.set(name, std::move(entry));
    }
    return out;
}

bool StateStore::fromJson(const Json& doc, std::string* error) {
    if (!doc.isObject()) return fail(error, "state: not an object");
    for (const auto& [name, entry] : doc.members()) {
        const auto it = m_vars.find(name);
        if (it == m_vars.end()) return fail(error, "state: unknown variable " + name);
        StateVar& v = it->second;
        if (v.kind == VarKind::Set) {
            const Json* list = entry.get("members");
            if (!list || !list->isArray()) return fail(error, "state: bad set " + name);
            v.members.clear();
            for (const Json& m : list->items()) v.members.insert(m.asString());
        } else {
            const Json* value = entry.get("value");
            if (!value || !value->isNumber()) return fail(error, "state: bad value " + name);
            v.value = std::clamp(value->asNumber(), v.minimum, v.maximum);
        }
    }
    return true;
}
