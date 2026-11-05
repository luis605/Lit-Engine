module;

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

module Engine.Params;

import Engine.Json;

namespace {
constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
constexpr size_t kCurvePointSize = 2;

bool numberField(const Json& object, std::string_view key, float& out, std::string& why, std::string_view context) {
    const Json* value = object.get(key);
    if (!value) return true;
    if (!value->isNumber()) {
        why = std::string(context) + "." + std::string(key) + " must be a number";
        return false;
    }
    out = static_cast<float>(value->asNumber());
    return true;
}

bool onlyKeys(const Json& object, std::initializer_list<std::string_view> allowed, std::string& why, std::string_view context) {
    for (const auto& [key, value] : object.members()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            why = std::string(context) + ": unknown key '" + key + "'";
            return false;
        }
    }
    return true;
}

bool parseSlot(const Json& definition, std::string& why, const std::string& context, int& slotOut) {
    float slot = -1.0f;
    if (!numberField(definition, "slot", slot, why, context)) return false;
    if (slot >= static_cast<float>(kParamSlots)) {
        why = context + ".slot out of range";
        return false;
    }
    slotOut = static_cast<int>(slot);
    return true;
}

bool parseParams(const Json& section, ParamDefinitions& out, std::string& why) {
    if (!section.isObject()) {
        why = "params must be an object";
        return false;
    }
    for (const auto& [name, definition] : section.members()) {
        const std::string context = "params." + name;
        if (!definition.isObject()) {
            why = context + " must be an object";
            return false;
        }
        if (!onlyKeys(definition, {"default", "min", "max", "smooth", "slot"}, why, context)) return false;
        ParamDefinitions::Param param;
        param.name = name;
        if (!numberField(definition, "default", param.initial, why, context) || !numberField(definition, "min", param.minimum, why, context) || !numberField(definition, "max", param.maximum, why, context) || !numberField(definition, "smooth", param.smooth, why, context)) return false;
        if (param.minimum > param.maximum) {
            why = context + ": min exceeds max";
            return false;
        }
        if (param.smooth < 0.0f) {
            why = context + ".smooth must be >= 0";
            return false;
        }
        if (!parseSlot(definition, why, context, param.slot)) return false;
        out.params.push_back(std::move(param));
    }
    return true;
}

bool parseCurve(const Json& curve, ParamCurve& out, std::string& why, const std::string& context) {
    if (!curve.isArray() || curve.size() == 0) {
        why = context + ".curve must be a non-empty array of [x, y]";
        return false;
    }
    for (const Json& point : curve.items()) {
        if (!point.isArray() || point.size() != kCurvePointSize || !point.items()[0].isNumber() || !point.items()[1].isNumber()) {
            why = context + ".curve points must be [x, y]";
            return false;
        }
        const float pointX = static_cast<float>(point.items()[0].asNumber());
        if (!out.points.empty() && pointX <= out.points.back().first) {
            why = context + ".curve x values must be strictly increasing";
            return false;
        }
        out.points.emplace_back(pointX, static_cast<float>(point.items()[1].asNumber()));
    }
    return true;
}

bool parseMode(const Json& mode, CurveMode& out, std::string& why, const std::string& context) {
    if (!mode.isString()) {
        why = context + ".mode must be a string";
        return false;
    }
    if (mode.asString() == "linear") out = CurveMode::Linear;
    else if (mode.asString() == "smooth") out = CurveMode::Smooth;
    else if (mode.asString() == "step") out = CurveMode::Step;
    else {
        why = context + ".mode must be linear, smooth or step";
        return false;
    }
    return true;
}

bool parseMapping(const std::string& name, const Json& definition, ParamDefinitions& out, const std::unordered_map<std::string, uint32_t>& existing, std::string& why) {
    const std::string context = "mappings." + name;
    if (!definition.isObject()) {
        why = context + " must be an object";
        return false;
    }
    if (!onlyKeys(definition, {"input", "curve", "mode", "smooth", "min", "max", "slot"}, why, context)) return false;
    ParamDefinitions::Mapping mapping;
    mapping.name = name;
    const Json* input = definition.get("input");
    if (!input || !input->isString()) {
        why = context + ".input must be a string";
        return false;
    }
    mapping.input = input->asString();
    const Json* curve = definition.get("curve");
    if (!curve) {
        why = context + ".curve must be a non-empty array of [x, y]";
        return false;
    }
    if (!parseCurve(*curve, mapping.curve, why, context)) return false;
    if (const Json* mode = definition.get("mode")) {
        if (!parseMode(*mode, mapping.curve.mode, why, context)) return false;
    }
    if (!numberField(definition, "smooth", mapping.smooth, why, context) || !numberField(definition, "min", mapping.minimum, why, context) || !numberField(definition, "max", mapping.maximum, why, context)) return false;
    if (!parseSlot(definition, why, context, mapping.slot)) return false;
    if (mapping.smooth < 0.0f) {
        why = context + ".smooth must be >= 0";
        return false;
    }
    if (mapping.minimum > mapping.maximum) {
        why = context + ": min exceeds max";
        return false;
    }
    const auto sameName = [&](const auto& entry) { return entry.name == mapping.input; };
    const bool known = std::any_of(out.params.begin(), out.params.end(), sameName) || std::any_of(out.mappings.begin(), out.mappings.end(), sameName) || existing.contains(mapping.input);
    if (!known) {
        why = context + ".input '" + mapping.input + "' is not defined before this mapping";
        return false;
    }
    if (std::any_of(out.params.begin(), out.params.end(), [&](const ParamDefinitions::Param& param) { return param.name == name; })) {
        why = context + " collides with a param";
        return false;
    }
    out.mappings.push_back(std::move(mapping));
    return true;
}

bool parseMappings(const Json& section, ParamDefinitions& out, const std::unordered_map<std::string, uint32_t>& existing, std::string& why) {
    if (!section.isObject()) {
        why = "mappings must be an object";
        return false;
    }
    for (const auto& [name, definition] : section.members()) {
        if (!parseMapping(name, definition, out, existing, why)) return false;
    }
    return true;
}
}

uint32_t ParamBus::place(const std::string& name, int slot) {
    if (const auto found = m_index.find(name); found != m_index.end()) return found->second;
    if (slot >= 0 && !m_slotUsed[static_cast<uint32_t>(slot)]) return addParam(name, static_cast<uint32_t>(slot), 1);
    const uint32_t index = ensure(name);
    if (index != kInvalidIndex) m_fromFile[index] = 1;
    return index;
}

void ParamBus::applyParamDefinition(const ParamDefinitions::Param& definition, const std::unordered_map<std::string, std::pair<float, float>>& saved) {
    const uint32_t index = place(definition.name, definition.slot);
    if (index == kInvalidIndex) return;
    ParamInfo& info = m_params[index];
    info.minimum = definition.minimum;
    info.maximum = definition.maximum;
    info.smooth = definition.smooth;
    info.derived = false;
    const auto previous = saved.find(definition.name);
    if (previous != saved.end()) {
        info.target = std::clamp(previous->second.first, info.minimum, info.maximum);
        info.value = std::clamp(previous->second.second, info.minimum, info.maximum);
    } else {
        info.target = std::clamp(definition.initial, info.minimum, info.maximum);
        info.value = info.target;
    }
    m_values[info.slot] = info.value;
}

void ParamBus::applyMappingDefinition(const ParamDefinitions::Mapping& definition, const std::unordered_map<std::string, std::pair<float, float>>& saved) {
    const uint32_t index = place(definition.name, definition.slot);
    const auto inputIt = m_index.find(definition.input);
    if (index == kInvalidIndex || inputIt == m_index.end()) return;
    ParamInfo& info = m_params[index];
    info.minimum = definition.minimum;
    info.maximum = definition.maximum;
    info.smooth = definition.smooth;
    info.derived = true;
    info.input = definition.input;
    const auto previous = saved.find(definition.name);
    const bool fresh = previous == saved.end();
    info.target = fresh ? 0.0f : previous->second.first;
    info.value = fresh ? 0.0f : previous->second.second;
    Mapping mapping;
    mapping.param = index;
    mapping.input = inputIt->second;
    mapping.curve = definition.curve;
    m_mappings.push_back(std::move(mapping));
    if (fresh) {
        const Mapping& added = m_mappings.back();
        info.target = std::clamp(added.curve.eval(m_params[added.input].value), info.minimum, info.maximum);
        info.value = info.target;
    }
    m_values[info.slot] = info.value;
}

void ParamBus::applyDefinitions(const ParamDefinitions& definitions) {
    std::unordered_map<std::string, std::pair<float, float>> saved;
    saved.reserve(m_params.size());
    for (const ParamInfo& info : m_params) saved.emplace(info.name, std::make_pair(info.target, info.value));

    std::vector<ParamInfo> keptParams;
    std::vector<uint8_t> keptFlags;
    for (size_t index = 0; index < m_params.size(); ++index) {
        if (m_fromFile[index]) continue;
        keptParams.push_back(std::move(m_params[index]));
        keptFlags.push_back(0);
    }
    m_params = std::move(keptParams);
    m_fromFile = std::move(keptFlags);
    m_mappings.clear();
    rebuildIndex();

    for (const ParamDefinitions::Param& definition : definitions.params) applyParamDefinition(definition, saved);
    for (const ParamDefinitions::Mapping& definition : definitions.mappings) applyMappingDefinition(definition, saved);
    for (const ParamInfo& info : m_params) m_values[info.slot] = info.value;
    ++m_version;
    ++m_reloads;
}

bool ParamBus::loadText(std::string_view text, std::string* error) {
    std::string why;
    const auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    std::string parseError;
    const std::optional<Json> document = Json::parse(text, &parseError);
    if (!document) return fail("json: " + parseError);
    if (!document->isObject()) return fail("root must be an object");
    if (!onlyKeys(*document, {"params", "mappings"}, why, "root")) return fail(why);

    ParamDefinitions definitions;
    if (const Json* section = document->get("params")) {
        if (!parseParams(*section, definitions, why)) return fail(why);
    }
    if (const Json* section = document->get("mappings")) {
        if (!parseMappings(*section, definitions, m_index, why)) return fail(why);
    }
    applyDefinitions(definitions);
    return true;
}

bool ParamBus::loadFile(const std::string& path, std::string* error) {
    const std::optional<std::string> text = readTextFile(path);
    if (!text) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    m_path = path;
    std::error_code errorCode;
    m_stamp = std::filesystem::last_write_time(path, errorCode);
    m_stampValid = !errorCode;
    return loadText(*text, error);
}

bool ParamBus::pollReload(std::string* error) {
    if (m_path.empty()) return false;
    std::error_code errorCode;
    const auto stamp = std::filesystem::last_write_time(m_path, errorCode);
    if (errorCode) return false;
    if (m_stampValid && stamp == m_stamp) return false;
    return loadFile(m_path, error);
}
