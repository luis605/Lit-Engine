module;

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module Engine.State;
import Engine.Json;

namespace {
constexpr double kDefaultCounterMaximum = 1.0e9;

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

bool kindFromName(std::string_view name, VarKind& out) {
    if (name == "range") out = VarKind::Range;
    else if (name == "flag") out = VarKind::Flag;
    else if (name == "counter") out = VarKind::Counter;
    else if (name == "set") out = VarKind::Set;
    else return false;
    return true;
}

const char* kindName(VarKind kind) {
    switch (kind) {
        case VarKind::Range: return "range";
        case VarKind::Flag: return "flag";
        case VarKind::Counter: return "counter";
        case VarKind::Set: return "set";
    }
    return "range";
}

double normalizeValue(const StateVar& variable, double value) {
    if (variable.kind == VarKind::Counter) value = std::floor(value);
    if (variable.kind == VarKind::Flag) value = value != 0.0 ? 1.0 : 0.0;
    return std::clamp(value, variable.minimum, variable.maximum);
}

double memberCount(const StateVar& variable) { return static_cast<double>(variable.members.size()); }

bool isSchemaKeyAllowed(const std::string& key) { return key == "type" || key == "min" || key == "max" || key == "default"; }

bool parseVarDefinition(const std::string& name, const Json& definition, StateVar& out, std::string* error) {
    const std::string where = "state schema: var " + name + ": ";
    if (name.empty()) return fail(error, "state schema: empty variable name");
    if (!definition.isObject()) return fail(error, where + "must be an object");
    VarKind kind = VarKind::Range;
    const Json* type = definition.get("type");
    if (!type || !type->isString() || !kindFromName(type->asString(), kind)) return fail(error, where + "type must be range, flag, counter or set");
    for (const auto& [key, value] : definition.members()) {
        if (!isSchemaKeyAllowed(key)) return fail(error, where + "unknown key " + key);
        if (key != "type" && !value.isNumber() && !(key == "default" && value.isBool())) return fail(error, where + key + " must be a number");
    }
    const Json* minimumJson = definition.get("min");
    const Json* maximumJson = definition.get("max");
    const Json* defaultJson = definition.get("default");
    double minimum = minimumJson ? minimumJson->asNumber() : 0.0;
    double maximum = maximumJson ? maximumJson->asNumber() : (kind == VarKind::Counter ? kDefaultCounterMaximum : 1.0);
    double initial = minimum;
    if (defaultJson) initial = defaultJson->isBool() ? (defaultJson->asBool() ? 1.0 : 0.0) : defaultJson->asNumber();
    if (kind == VarKind::Flag && (minimumJson || maximumJson)) return fail(error, where + "flag takes no range");
    if (kind == VarKind::Set && (minimumJson || maximumJson || defaultJson)) return fail(error, where + "set takes no range or default");
    if (minimum > maximum) return fail(error, where + "min exceeds max");
    if (kind != VarKind::Set && kind != VarKind::Flag && (initial < minimum || initial > maximum)) return fail(error, where + "default outside range");
    StateStore probe;
    if (!probe.declare(name, kind, minimum, maximum, initial)) return fail(error, where + "invalid");
    out = *probe.var(name);
    return true;
}
}

bool StateStore::declare(const std::string& name, VarKind kind, double minimum, double maximum, double initial) {
    if (name.empty() || !(minimum <= maximum) || !std::isfinite(initial)) return false;
    StateVar variable;
    variable.kind = kind;
    if (kind == VarKind::Flag) {
        variable.minimum = 0.0;
        variable.maximum = 1.0;
        initial = initial != 0.0 ? 1.0 : 0.0;
    } else if (kind == VarKind::Set) {
        variable.minimum = 0.0;
        variable.maximum = 0.0;
        initial = 0.0;
    } else {
        variable.minimum = minimum;
        variable.maximum = maximum;
        if (kind == VarKind::Counter) {
            variable.minimum = std::floor(minimum);
            variable.maximum = std::floor(maximum);
            initial = std::floor(initial);
        }
        initial = std::clamp(initial, variable.minimum, variable.maximum);
    }
    variable.initial = initial;
    variable.value = initial;
    m_vars[name] = std::move(variable);
    return true;
}

const StateVar* StateStore::var(const std::string& name) const {
    const auto found = m_vars.find(name);
    return found == m_vars.end() ? nullptr : &found->second;
}

double StateStore::get(const std::string& name) const {
    const StateVar* variable = var(name);
    if (!variable) return 0.0;
    return variable->kind == VarKind::Set ? memberCount(*variable) : variable->value;
}

bool StateStore::hasMember(const std::string& name, const std::string& member) const {
    const StateVar* variable = var(name);
    return variable && variable->kind == VarKind::Set && variable->members.contains(member);
}

std::vector<std::string> StateStore::members(const std::string& name) const {
    const StateVar* variable = var(name);
    if (!variable || variable->kind != VarKind::Set) return {};
    return {variable->members.begin(), variable->members.end()};
}

void StateStore::notify(const std::string& name, const StateVar& variable, double oldValue, const std::string& member, bool added) {
    if (!m_bus) return;
    m_bus->emitNow(StateChanged{name, variable.kind, oldValue, variable.kind == VarKind::Set ? memberCount(variable) : variable.value, member, added});
}

bool StateStore::set(const std::string& name, double value) {
    const auto found = m_vars.find(name);
    if (found == m_vars.end() || found->second.kind == VarKind::Set || !std::isfinite(value)) return false;
    StateVar& variable = found->second;
    value = normalizeValue(variable, value);
    if (value == variable.value) return true;
    const double oldValue = variable.value;
    variable.value = value;
    notify(name, variable, oldValue, {}, false);
    return true;
}

bool StateStore::add(const std::string& name, double delta) {
    const StateVar* variable = var(name);
    if (!variable || variable->kind == VarKind::Set || variable->kind == VarKind::Flag) return false;
    return set(name, variable->value + delta);
}

bool StateStore::addMember(const std::string& name, const std::string& member) {
    const auto found = m_vars.find(name);
    if (found == m_vars.end() || found->second.kind != VarKind::Set) return false;
    StateVar& variable = found->second;
    const double oldCount = memberCount(variable);
    if (variable.members.insert(member).second) notify(name, variable, oldCount, member, true);
    return true;
}

bool StateStore::removeMember(const std::string& name, const std::string& member) {
    const auto found = m_vars.find(name);
    if (found == m_vars.end() || found->second.kind != VarKind::Set) return false;
    StateVar& variable = found->second;
    const double oldCount = memberCount(variable);
    if (variable.members.erase(member)) notify(name, variable, oldCount, member, false);
    return true;
}

void StateStore::clearSet(const std::string& name) {
    const auto found = m_vars.find(name);
    if (found == m_vars.end() || found->second.kind != VarKind::Set) return;
    const std::vector<std::string> snapshot(found->second.members.begin(), found->second.members.end());
    for (const std::string& member : snapshot) removeMember(name, member);
}

void StateStore::reset() {
    for (auto& [name, variable] : m_vars) {
        if (variable.kind == VarKind::Set) clearSet(name);
        else set(name, variable.initial);
    }
}

bool StateStore::loadSchemaFile(const std::string& path, std::string* error) {
    const std::optional<std::string> text = readTextFile(path);
    if (!text) return fail(error, "cannot open " + path);
    return loadSchema(*text, error);
}

bool StateStore::loadSchema(std::string_view text, std::string* error) {
    std::string parseError;
    const auto document = Json::parse(text, &parseError);
    if (!document) return fail(error, "state schema: " + parseError);
    if (!document->isObject()) return fail(error, "state schema: root must be an object");
    const Json* varsJson = document->get("vars");
    if (!varsJson || !varsJson->isObject()) return fail(error, "state schema: missing vars object");
    for (const auto& [key, value] : document->members()) {
        if (key != "vars" && key != "version") return fail(error, "state schema: unknown key " + key);
    }

    std::map<std::string, StateVar> parsed;
    for (const auto& [name, definition] : varsJson->members()) {
        StateVar variable;
        if (!parseVarDefinition(name, definition, variable, error)) return false;
        parsed[name] = std::move(variable);
    }
    for (auto& [name, variable] : parsed) m_vars[name] = std::move(variable);
    return true;
}

Json StateStore::toJson() const {
    Json out = Json::object();
    for (const auto& [name, variable] : m_vars) {
        Json entry = Json::object();
        entry.set("type", kindName(variable.kind));
        if (variable.kind == VarKind::Set) {
            Json list = Json::array();
            for (const std::string& member : variable.members) list.push(member);
            entry.set("members", std::move(list));
        } else {
            entry.set("value", variable.value);
        }
        out.set(name, std::move(entry));
    }
    return out;
}

bool StateStore::fromJson(const Json& doc, std::string* error) {
    if (!doc.isObject()) return fail(error, "state: not an object");
    std::map<std::string, StateVar> restored = m_vars;
    for (const auto& [name, entry] : doc.members()) {
        const auto found = restored.find(name);
        if (found == restored.end()) return fail(error, "state: unknown variable " + name);
        StateVar& variable = found->second;
        if (variable.kind == VarKind::Set) {
            const Json* list = entry.get("members");
            if (!list || !list->isArray()) return fail(error, "state: bad set " + name);
            variable.members.clear();
            for (const Json& member : list->items()) {
                if (!member.isString()) return fail(error, "state: bad set member in " + name);
                variable.members.insert(member.asString());
            }
        } else {
            const Json* value = entry.get("value");
            if (!value || !value->isNumber() || !std::isfinite(value->asNumber())) return fail(error, "state: bad value " + name);
            variable.value = normalizeValue(variable, value->asNumber());
        }
    }
    m_vars = std::move(restored);
    return true;
}
