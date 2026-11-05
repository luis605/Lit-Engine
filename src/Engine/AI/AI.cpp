module;

#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

module Engine.AI;

#include "AIInternal.hpp"

namespace {
Json optionToJson(const TraceOption& option) {
    Json considerations = Json::array();
    for (const TraceConsideration& consideration : option.considerations) {
        Json entry = Json::object();
        entry.set("input", consideration.input);
        entry.set("value", consideration.value);
        entry.set("score", consideration.score);
        considerations.push(std::move(entry));
    }
    Json out = Json::object();
    out.set("name", option.name);
    out.set("score", option.score);
    out.set("available", option.available);
    out.set("considerations", std::move(considerations));
    return out;
}

Json eventToJson(const TraceEvent& event) {
    Json options = Json::array();
    for (const TraceOption& option : event.options) options.push(optionToJson(option));
    Json out = Json::object();
    out.set("tick", event.tick);
    out.set("agent", event.agent);
    out.set("node", event.node);
    out.set("kind", event.kind);
    out.set("winner", event.winner);
    out.set("reason", event.reason);
    out.set("options", std::move(options));
    return out;
}
}

const Json* Blackboard::get(const std::string& key) const {
    const auto found = m_values.find(key);
    return found == m_values.end() ? nullptr : &found->second;
}

double Blackboard::number(const std::string& key, double fallback) const {
    const Json* value = get(key);
    return value && (value->isNumber() || value->isBool()) ? AiDetail::toNumber(*value) : fallback;
}

bool Blackboard::boolean(const std::string& key, bool fallback) const {
    const Json* value = get(key);
    if (!value) return fallback;
    if (value->isBool()) return value->asBool();
    if (value->isNumber()) return value->asNumber() != 0.0;
    return fallback;
}

std::string Blackboard::string(const std::string& key, const std::string& fallback) const {
    const Json* value = get(key);
    return value && value->isString() ? value->asString() : fallback;
}

void Blackboard::importParams(const ParamBus& bus, const std::string& prefix) {
    for (const ParamInfo& param : bus.params()) m_values[prefix + param.name] = Json(static_cast<double>(param.value));
}

Json Blackboard::toJson() const {
    Json out = Json::object();
    for (const auto& [key, value] : m_values) out.set(key, value);
    return out;
}

void DecisionTrace::record(TraceEvent event) {
    if (!m_enabled) return;
    if (m_capacity > 0 && m_events.size() >= m_capacity) m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(m_events.size() - m_capacity + 1));
    m_events.push_back(std::move(event));
}

std::vector<const TraceEvent*> DecisionTrace::forTick(uint64_t tick) const {
    std::vector<const TraceEvent*> out;
    for (const TraceEvent& event : m_events) {
        if (event.tick == tick) out.push_back(&event);
    }
    return out;
}

std::string DecisionTrace::lastWinner(const std::string& node) const {
    for (auto event = m_events.rbegin(); event != m_events.rend(); ++event) {
        if (event->node == node) return event->winner;
    }
    return {};
}

Json DecisionTrace::toJson() const {
    Json events = Json::array();
    for (const TraceEvent& event : m_events) events.push(eventToJson(event));
    Json root = Json::object();
    root.set("events", std::move(events));
    return root;
}

std::string DecisionTrace::toText() const {
    std::string out;
    for (const TraceEvent& event : m_events) {
        out += "tick " + std::to_string(event.tick) + " agent " + std::to_string(event.agent) + " " + event.node + " [" + event.kind + "] -> " + event.winner + " (" + event.reason + ")\n";
        for (const TraceOption& option : event.options) {
            out += std::string("  ") + (option.name == event.winner ? "* " : "  ") + option.name + " " + (option.available ? AiDetail::formatNumber(option.score) : std::string("unavailable"));
            for (const TraceConsideration& consideration : option.considerations) {
                out += " " + consideration.input + "=" + AiDetail::formatNumber(consideration.value) + "->" + AiDetail::formatNumber(consideration.score);
            }
            out += "\n";
        }
    }
    return out;
}

bool DecisionTrace::writeFile(const std::string& path, bool asText) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << (asText ? toText() : toJson().dump(2));
    return static_cast<bool>(out);
}
