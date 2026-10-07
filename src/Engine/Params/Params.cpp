module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <unordered_map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module Engine.Params;

import Engine.Json;

float ParamCurve::eval(float x) const {
    if (points.empty()) return 0.0f;
    if (x <= points.front().first) return points.front().second;
    if (x >= points.back().first) return points.back().second;
    size_t hi = 1;
    while (points[hi].first < x) ++hi;
    const auto& [x0, y0] = points[hi - 1];
    const auto& [x1, y1] = points[hi];
    if (mode == CurveMode::Step) return y0;
    float t = (x - x0) / (x1 - x0);
    if (mode == CurveMode::Smooth) t = t * t * (3.0f - 2.0f * t);
    return y0 + (y1 - y0) * t;
}

ParamBus::ParamBus() : m_values(kParamSlots, 0.0f), m_slotUsed(kParamSlots, 0) {}

int ParamBus::freeSlot() const {
    for (uint32_t i = 0; i < kParamSlots; ++i) {
        if (!m_slotUsed[i]) return static_cast<int>(i);
    }
    return -1;
}

uint32_t ParamBus::ensure(const std::string& name) {
    const auto it = m_index.find(name);
    if (it != m_index.end()) return it->second;
    const int slot = freeSlot();
    if (slot < 0) return std::numeric_limits<uint32_t>::max();
    ParamInfo p;
    p.name = name;
    p.slot = static_cast<uint32_t>(slot);
    m_slotUsed[p.slot] = 1;
    m_params.push_back(std::move(p));
    m_fromFile.push_back(0);
    const uint32_t index = static_cast<uint32_t>(m_params.size() - 1);
    m_index.emplace(name, index);
    ++m_version;
    return index;
}

void ParamBus::rebuildIndex() {
    m_index.clear();
    std::fill(m_slotUsed.begin(), m_slotUsed.end(), static_cast<uint8_t>(0));
    for (uint32_t i = 0; i < m_params.size(); ++i) {
        m_index.emplace(m_params[i].name, i);
        m_slotUsed[m_params[i].slot] = 1;
    }
}

uint32_t ParamBus::declare(const std::string& name, float initial, float smoothSeconds, float minimum, float maximum, int slot) {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    const auto it = m_index.find(name);
    if (it != m_index.end()) {
        index = it->second;
    } else if (slot >= 0 && static_cast<uint32_t>(slot) < kParamSlots && !m_slotUsed[static_cast<uint32_t>(slot)]) {
        ParamInfo p;
        p.name = name;
        p.slot = static_cast<uint32_t>(slot);
        m_slotUsed[p.slot] = 1;
        m_params.push_back(std::move(p));
        m_fromFile.push_back(0);
        index = static_cast<uint32_t>(m_params.size() - 1);
        m_index.emplace(name, index);
    } else {
        index = ensure(name);
    }
    if (index == std::numeric_limits<uint32_t>::max()) return index;
    ParamInfo& p = m_params[index];
    p.minimum = minimum;
    p.maximum = maximum;
    p.smooth = smoothSeconds;
    p.target = std::clamp(initial, minimum, maximum);
    p.value = p.target;
    m_values[p.slot] = p.value;
    ++m_version;
    return index;
}

void ParamBus::set(const std::string& name, float value) {
    const uint32_t index = ensure(name);
    if (index == std::numeric_limits<uint32_t>::max()) return;
    ParamInfo& p = m_params[index];
    p.target = std::clamp(value, p.minimum, p.maximum);
}

void ParamBus::snap(const std::string& name, float value) {
    const uint32_t index = ensure(name);
    if (index == std::numeric_limits<uint32_t>::max()) return;
    ParamInfo& p = m_params[index];
    p.target = std::clamp(value, p.minimum, p.maximum);
    p.value = p.target;
    m_values[p.slot] = p.value;
    ++m_version;
}

float ParamBus::get(const std::string& name) const {
    const auto it = m_index.find(name);
    return it == m_index.end() ? 0.0f : m_params[it->second].value;
}

float ParamBus::target(const std::string& name) const {
    const auto it = m_index.find(name);
    return it == m_index.end() ? 0.0f : m_params[it->second].target;
}

int ParamBus::slot(const std::string& name) const {
    const auto it = m_index.find(name);
    return it == m_index.end() ? -1 : static_cast<int>(m_params[it->second].slot);
}

const ParamCurve* ParamBus::curve(const std::string& name) const {
    const auto it = m_index.find(name);
    if (it == m_index.end()) return nullptr;
    for (const Mapping& m : m_mappings) {
        if (m.param == it->second) return &m.curve;
    }
    return nullptr;
}

void ParamBus::update(float dt) {
    auto step = [&](ParamInfo& p) {
        if (p.smooth > 0.0f && dt > 0.0f) {
            p.value += (p.target - p.value) * (1.0f - std::exp(-dt / p.smooth));
        } else if (p.smooth <= 0.0f) {
            p.value = p.target;
        }
        m_values[p.slot] = p.value;
    };
    for (ParamInfo& p : m_params) {
        if (!p.derived) step(p);
    }
    for (const Mapping& m : m_mappings) {
        ParamInfo& out = m_params[m.param];
        out.target = std::clamp(m.curve.eval(m_params[m.input].value), out.minimum, out.maximum);
        step(out);
    }
    ++m_version;
}

void ParamBus::snapAll() {
    for (ParamInfo& p : m_params) {
        if (!p.derived) {
            p.value = p.target;
            m_values[p.slot] = p.value;
        }
    }
    for (const Mapping& m : m_mappings) {
        ParamInfo& out = m_params[m.param];
        out.target = std::clamp(m.curve.eval(m_params[m.input].value), out.minimum, out.maximum);
        out.value = out.target;
        m_values[out.slot] = out.value;
    }
    ++m_version;
}

namespace {

bool numberField(const Json& obj, std::string_view key, float& out, std::string& why, std::string_view ctx) {
    const Json* v = obj.get(key);
    if (!v) return true;
    if (!v->isNumber()) {
        why = std::string(ctx) + "." + std::string(key) + " must be a number";
        return false;
    }
    out = static_cast<float>(v->asNumber());
    return true;
}

bool onlyKeys(const Json& obj, std::initializer_list<std::string_view> allowed, std::string& why, std::string_view ctx) {
    for (const auto& [k, v] : obj.members()) {
        if (std::find(allowed.begin(), allowed.end(), k) == allowed.end()) {
            why = std::string(ctx) + ": unknown key '" + k + "'";
            return false;
        }
    }
    return true;
}

}  // namespace

bool ParamBus::loadText(std::string_view text, std::string* error) {
    std::string why;
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    std::string parseError;
    const std::optional<Json> doc = Json::parse(text, &parseError);
    if (!doc) return fail("json: " + parseError);
    if (!doc->isObject()) return fail("root must be an object");
    if (!onlyKeys(*doc, {"params", "mappings"}, why, "root")) return fail(why);

    struct PendingParam {
        std::string name;
        float initial = 0.0f, smooth = 0.0f, minimum = -std::numeric_limits<float>::infinity(), maximum = std::numeric_limits<float>::infinity();
        int slot = -1;
    };
    struct PendingMapping {
        std::string name, input;
        ParamCurve curve;
        float smooth = 0.0f, minimum = -std::numeric_limits<float>::infinity(), maximum = std::numeric_limits<float>::infinity();
        int slot = -1;
    };
    std::vector<PendingParam> params;
    std::vector<PendingMapping> mappings;

    if (const Json* section = doc->get("params")) {
        if (!section->isObject()) return fail("params must be an object");
        for (const auto& [name, def] : section->members()) {
            const std::string ctx = "params." + name;
            if (!def.isObject()) return fail(ctx + " must be an object");
            if (!onlyKeys(def, {"default", "min", "max", "smooth", "slot"}, why, ctx)) return fail(why);
            PendingParam p;
            p.name = name;
            float slot = -1.0f;
            if (!numberField(def, "default", p.initial, why, ctx) || !numberField(def, "min", p.minimum, why, ctx) || !numberField(def, "max", p.maximum, why, ctx) || !numberField(def, "smooth", p.smooth, why, ctx) || !numberField(def, "slot", slot, why, ctx)) return fail(why);
            if (p.minimum > p.maximum) return fail(ctx + ": min exceeds max");
            if (p.smooth < 0.0f) return fail(ctx + ".smooth must be >= 0");
            if (slot >= static_cast<float>(kParamSlots)) return fail(ctx + ".slot out of range");
            p.slot = static_cast<int>(slot);
            params.push_back(std::move(p));
        }
    }
    if (const Json* section = doc->get("mappings")) {
        if (!section->isObject()) return fail("mappings must be an object");
        for (const auto& [name, def] : section->members()) {
            const std::string ctx = "mappings." + name;
            if (!def.isObject()) return fail(ctx + " must be an object");
            if (!onlyKeys(def, {"input", "curve", "mode", "smooth", "min", "max", "slot"}, why, ctx)) return fail(why);
            PendingMapping m;
            m.name = name;
            const Json* input = def.get("input");
            if (!input || !input->isString()) return fail(ctx + ".input must be a string");
            m.input = input->asString();
            const Json* curve = def.get("curve");
            if (!curve || !curve->isArray() || curve->size() == 0) return fail(ctx + ".curve must be a non-empty array of [x, y]");
            for (const Json& pt : curve->items()) {
                if (!pt.isArray() || pt.size() != 2 || !pt.items()[0].isNumber() || !pt.items()[1].isNumber()) return fail(ctx + ".curve points must be [x, y]");
                const float x = static_cast<float>(pt.items()[0].asNumber());
                if (!m.curve.points.empty() && x <= m.curve.points.back().first) return fail(ctx + ".curve x values must be strictly increasing");
                m.curve.points.emplace_back(x, static_cast<float>(pt.items()[1].asNumber()));
            }
            if (const Json* mode = def.get("mode")) {
                if (!mode->isString()) return fail(ctx + ".mode must be a string");
                if (mode->asString() == "linear") m.curve.mode = CurveMode::Linear;
                else if (mode->asString() == "smooth") m.curve.mode = CurveMode::Smooth;
                else if (mode->asString() == "step") m.curve.mode = CurveMode::Step;
                else return fail(ctx + ".mode must be linear, smooth or step");
            }
            float slot = -1.0f;
            if (!numberField(def, "smooth", m.smooth, why, ctx) || !numberField(def, "min", m.minimum, why, ctx) || !numberField(def, "max", m.maximum, why, ctx) || !numberField(def, "slot", slot, why, ctx)) return fail(why);
            if (m.smooth < 0.0f) return fail(ctx + ".smooth must be >= 0");
            if (slot >= static_cast<float>(kParamSlots)) return fail(ctx + ".slot out of range");
            m.slot = static_cast<int>(slot);
            bool known = std::any_of(params.begin(), params.end(), [&](const PendingParam& p) { return p.name == m.input; }) || std::any_of(mappings.begin(), mappings.end(), [&](const PendingMapping& o) { return o.name == m.input; }) || m_index.contains(m.input);
            if (!known) return fail(ctx + ".input '" + m.input + "' is not defined before this mapping");
            if (std::any_of(params.begin(), params.end(), [&](const PendingParam& p) { return p.name == name; })) return fail(ctx + " collides with a param");
            mappings.push_back(std::move(m));
        }
    }

    std::vector<ParamInfo> keepParams;
    std::vector<uint8_t> keepFlags;
    for (size_t i = 0; i < m_params.size(); ++i) {
        if (!m_fromFile[i]) {
            ParamInfo p = m_params[i];
            keepParams.push_back(std::move(p));
            keepFlags.push_back(0);
        }
    }
    struct Saved {
        float target, value;
    };
    std::unordered_map<std::string, Saved> saved;
    for (const ParamInfo& p : m_params) saved.emplace(p.name, Saved{p.target, p.value});

    m_params = std::move(keepParams);
    m_fromFile = std::move(keepFlags);
    m_mappings.clear();
    rebuildIndex();

    auto place = [&](const std::string& name, int slot) {
        if (const auto it = m_index.find(name); it != m_index.end()) return it->second;
        if (slot >= 0 && !m_slotUsed[static_cast<uint32_t>(slot)]) {
            ParamInfo p;
            p.name = name;
            p.slot = static_cast<uint32_t>(slot);
            m_slotUsed[p.slot] = 1;
            m_params.push_back(std::move(p));
            m_fromFile.push_back(1);
            m_index.emplace(name, static_cast<uint32_t>(m_params.size() - 1));
            return static_cast<uint32_t>(m_params.size() - 1);
        }
        const uint32_t index = ensure(name);
        if (index != std::numeric_limits<uint32_t>::max()) m_fromFile[index] = 1;
        return index;
    };
    for (const PendingParam& pp : params) {
        const bool existed = m_index.contains(pp.name);
        const uint32_t index = place(pp.name, pp.slot);
        if (index == std::numeric_limits<uint32_t>::max()) continue;
        ParamInfo& p = m_params[index];
        p.minimum = pp.minimum;
        p.maximum = pp.maximum;
        p.smooth = pp.smooth;
        p.derived = false;
        const auto old = saved.find(pp.name);
        if (old != saved.end()) {
            p.target = std::clamp(old->second.target, p.minimum, p.maximum);
            p.value = std::clamp(old->second.value, p.minimum, p.maximum);
        } else {
            p.target = std::clamp(pp.initial, p.minimum, p.maximum);
            p.value = p.target;
        }
        (void)existed;
        m_values[p.slot] = p.value;
    }
    for (const PendingMapping& pm : mappings) {
        const uint32_t index = place(pm.name, pm.slot);
        const auto inputIt = m_index.find(pm.input);
        if (index == std::numeric_limits<uint32_t>::max() || inputIt == m_index.end()) continue;
        ParamInfo& p = m_params[index];
        p.minimum = pm.minimum;
        p.maximum = pm.maximum;
        p.smooth = pm.smooth;
        p.derived = true;
        p.input = pm.input;
        const auto old = saved.find(pm.name);
        p.target = old != saved.end() ? old->second.target : 0.0f;
        p.value = old != saved.end() ? old->second.value : 0.0f;
        Mapping m;
        m.param = index;
        m.input = inputIt->second;
        m.curve = pm.curve;
        const bool fresh = old == saved.end();
        m_mappings.push_back(std::move(m));
        if (fresh) {
            const Mapping& added = m_mappings.back();
            p.target = std::clamp(added.curve.eval(m_params[added.input].value), p.minimum, p.maximum);
            p.value = p.target;
        }
        m_values[p.slot] = p.value;
    }
    for (const ParamInfo& p : m_params) m_values[p.slot] = p.value;
    ++m_version;
    ++m_reloads;
    return true;
}

bool ParamBus::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    m_path = path;
    std::error_code ec;
    m_stamp = std::filesystem::last_write_time(path, ec);
    m_stampValid = !ec;
    return loadText(buffer.str(), error);
}

bool ParamBus::pollReload(std::string* error) {
    if (m_path.empty()) return false;
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(m_path, ec);
    if (ec) return false;
    if (m_stampValid && stamp == m_stamp) return false;
    return loadFile(m_path, error);
}
