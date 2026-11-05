module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

module Engine.Params;

namespace {
constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
}

float ParamCurve::eval(float input) const {
    if (points.empty()) return 0.0f;
    if (!(input > points.front().first)) return points.front().second;
    if (input >= points.back().first) return points.back().second;
    size_t upper = 1;
    while (points[upper].first < input) ++upper;
    const auto& [lowerX, lowerY] = points[upper - 1];
    const auto& [upperX, upperY] = points[upper];
    if (mode == CurveMode::Step) return lowerY;
    float fraction = (input - lowerX) / (upperX - lowerX);
    if (mode == CurveMode::Smooth) fraction = fraction * fraction * (3.0f - 2.0f * fraction);
    return lowerY + (upperY - lowerY) * fraction;
}

ParamBus::ParamBus() : m_values(kParamSlots, 0.0f), m_slotUsed(kParamSlots, 0) {}

int ParamBus::freeSlot() const {
    for (uint32_t slot = 0; slot < kParamSlots; ++slot) {
        if (!m_slotUsed[slot]) return static_cast<int>(slot);
    }
    return -1;
}

uint32_t ParamBus::addParam(const std::string& name, uint32_t slot, uint8_t fromFile) {
    ParamInfo info;
    info.name = name;
    info.slot = slot;
    m_slotUsed[slot] = 1;
    m_params.push_back(std::move(info));
    m_fromFile.push_back(fromFile);
    const uint32_t index = static_cast<uint32_t>(m_params.size() - 1);
    m_index.emplace(name, index);
    return index;
}

uint32_t ParamBus::ensure(const std::string& name) {
    const auto found = m_index.find(name);
    if (found != m_index.end()) return found->second;
    const int slot = freeSlot();
    if (slot < 0) return kInvalidIndex;
    const uint32_t index = addParam(name, static_cast<uint32_t>(slot), 0);
    ++m_version;
    return index;
}

void ParamBus::rebuildIndex() {
    m_index.clear();
    std::fill(m_slotUsed.begin(), m_slotUsed.end(), static_cast<uint8_t>(0));
    for (uint32_t index = 0; index < m_params.size(); ++index) {
        m_index.emplace(m_params[index].name, index);
        m_slotUsed[m_params[index].slot] = 1;
    }
}

uint32_t ParamBus::declare(const std::string& name, float initial, float smoothSeconds, float minimum, float maximum, int slot) {
    if (minimum > maximum) std::swap(minimum, maximum);
    if (!std::isfinite(initial)) initial = 0.0f;
    if (!std::isfinite(smoothSeconds) || smoothSeconds < 0.0f) smoothSeconds = 0.0f;
    uint32_t index = kInvalidIndex;
    const auto found = m_index.find(name);
    if (found != m_index.end()) {
        index = found->second;
    } else if (slot >= 0 && static_cast<uint32_t>(slot) < kParamSlots && !m_slotUsed[static_cast<uint32_t>(slot)]) {
        index = addParam(name, static_cast<uint32_t>(slot), 0);
    } else {
        index = ensure(name);
    }
    if (index == kInvalidIndex) return index;
    ParamInfo& info = m_params[index];
    info.minimum = minimum;
    info.maximum = maximum;
    info.smooth = smoothSeconds;
    info.target = std::clamp(initial, minimum, maximum);
    info.value = info.target;
    m_values[info.slot] = info.value;
    ++m_version;
    return index;
}

void ParamBus::set(const std::string& name, float value) {
    const uint32_t index = ensure(name);
    if (index == kInvalidIndex || !std::isfinite(value)) return;
    ParamInfo& info = m_params[index];
    info.target = std::clamp(value, info.minimum, info.maximum);
}

void ParamBus::snap(const std::string& name, float value) {
    const uint32_t index = ensure(name);
    if (index == kInvalidIndex || !std::isfinite(value)) return;
    ParamInfo& info = m_params[index];
    info.target = std::clamp(value, info.minimum, info.maximum);
    info.value = info.target;
    m_values[info.slot] = info.value;
    ++m_version;
}

float ParamBus::get(const std::string& name) const {
    const auto found = m_index.find(name);
    return found == m_index.end() ? 0.0f : m_params[found->second].value;
}

float ParamBus::target(const std::string& name) const {
    const auto found = m_index.find(name);
    return found == m_index.end() ? 0.0f : m_params[found->second].target;
}

int ParamBus::slot(const std::string& name) const {
    const auto found = m_index.find(name);
    return found == m_index.end() ? -1 : static_cast<int>(m_params[found->second].slot);
}

const ParamCurve* ParamBus::curve(const std::string& name) const {
    const auto found = m_index.find(name);
    if (found == m_index.end()) return nullptr;
    for (const Mapping& mapping : m_mappings) {
        if (mapping.param == found->second) return &mapping.curve;
    }
    return nullptr;
}

void ParamBus::stepSmoothing(ParamInfo& info, float deltaSeconds) {
    if (info.smooth > 0.0f && deltaSeconds > 0.0f) {
        info.value += (info.target - info.value) * (1.0f - std::exp(-deltaSeconds / info.smooth));
    } else if (info.smooth <= 0.0f) {
        info.value = info.target;
    }
    m_values[info.slot] = info.value;
}

void ParamBus::update(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) deltaSeconds = 0.0f;
    for (ParamInfo& info : m_params) {
        if (!info.derived) stepSmoothing(info, deltaSeconds);
    }
    for (const Mapping& mapping : m_mappings) {
        ParamInfo& output = m_params[mapping.param];
        output.target = std::clamp(mapping.curve.eval(m_params[mapping.input].value), output.minimum, output.maximum);
        stepSmoothing(output, deltaSeconds);
    }
    ++m_version;
}

void ParamBus::snapAll() {
    for (ParamInfo& info : m_params) {
        if (info.derived) continue;
        info.value = info.target;
        m_values[info.slot] = info.value;
    }
    for (const Mapping& mapping : m_mappings) {
        ParamInfo& output = m_params[mapping.param];
        output.target = std::clamp(mapping.curve.eval(m_params[mapping.input].value), output.minimum, output.maximum);
        output.value = output.target;
        m_values[output.slot] = output.value;
    }
    ++m_version;
}
