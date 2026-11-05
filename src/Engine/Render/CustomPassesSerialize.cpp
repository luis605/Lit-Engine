module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Query.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

module Engine.Render.custom;

import Engine.glm;

#include "CustomPassesInternal.hpp"

using namespace custom_detail;

namespace {

constexpr size_t kHeaderNumberCount = 17;
constexpr size_t kComputeNumberCount = 21;
constexpr size_t kDrawNumberCount = 27;
constexpr size_t kMinPartCount = 3;
constexpr size_t kDrawMinPartCount = 6;

const char* kindName(PassKind kind) { return kind == PassKind::Compute ? "compute" : kind == PassKind::Draw ? "draw" : "effect"; }

std::vector<std::string_view> splitTabs(std::string_view text) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t tab = text.find('\t', start);
        parts.push_back(text.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) break;
        start = tab + 1;
    }
    return parts;
}

std::vector<float> parseNumbers(std::string_view text) {
    std::vector<float> values;
    while (!text.empty()) {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        if (text.empty()) break;
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{}) break;
        values.push_back(value);
        text.remove_prefix(static_cast<size_t>(end - text.data()));
    }
    return values;
}

uint32_t toUnsigned(float value) { return value > 0.0f ? static_cast<uint32_t>(value) : 0u; }

void saveCompute(std::ostream& out, const ComputeDesc& compute) {
    out << ' ' << compute.groups.x << ' ' << compute.groups.y << ' ' << compute.groups.z << ' ' << static_cast<int>(compute.stage) << ' ' << compute.indirectOffset << '\t' << compute.name << '\t' << compute.path << '\t' << compute.indirectBuffer;
}

void saveDraw(std::ostream& out, const DrawDesc& draw) {
    out << ' ' << static_cast<int>(draw.primitive) << ' ' << draw.vertexCount << ' ' << draw.instanceCount << ' ' << draw.meshGroups.x << ' ' << draw.meshGroups.y << ' ' << draw.meshGroups.z << ' ' << static_cast<int>(draw.blend) << ' '
        << (draw.depthTest ? 1 : 0) << ' ' << (draw.depthWrite ? 1 : 0) << ' ' << (draw.doubleSided ? 1 : 0) << ' ' << draw.indirectOffset << '\t' << draw.name << '\t' << draw.vertexPath << '\t' << draw.fragmentPath << '\t' << draw.meshPath << '\t'
        << draw.taskPath << '\t' << draw.indirectBuffer;
}

ComputeDesc parseCompute(const std::vector<std::string_view>& parts, const std::vector<float>& numbers) {
    ComputeDesc desc;
    desc.name = std::string(parts[1]);
    desc.path = std::string(parts[2]);
    desc.groups = glm::ivec3(static_cast<int>(numbers[17]), static_cast<int>(numbers[18]), static_cast<int>(numbers[19]));
    desc.stage = static_cast<ComputeStage>(std::clamp(static_cast<int>(numbers[20]), 0, 2));
    if (numbers.size() >= 22) desc.indirectOffset = toUnsigned(numbers[21]);
    if (parts.size() >= 4) desc.indirectBuffer = std::string(parts[3]);
    return desc;
}

DrawDesc parseDraw(const std::vector<std::string_view>& parts, const std::vector<float>& numbers) {
    DrawDesc desc;
    desc.name = std::string(parts[1]);
    desc.vertexPath = std::string(parts[2]);
    desc.fragmentPath = std::string(parts[3]);
    desc.meshPath = std::string(parts[4]);
    desc.taskPath = std::string(parts[5]);
    desc.primitive = static_cast<DrawPrimitive>(std::clamp(static_cast<int>(numbers[17]), 0, 3));
    desc.vertexCount = toUnsigned(numbers[18]);
    desc.instanceCount = toUnsigned(numbers[19]);
    desc.meshGroups = glm::ivec3(static_cast<int>(numbers[20]), static_cast<int>(numbers[21]), static_cast<int>(numbers[22]));
    desc.blend = static_cast<DrawBlend>(std::clamp(static_cast<int>(numbers[23]), 0, 2));
    desc.depthTest = numbers[24] > 0.5f;
    desc.depthWrite = numbers[25] > 0.5f;
    desc.doubleSided = numbers[26] > 0.5f;
    if (numbers.size() >= 28) desc.indirectOffset = toUnsigned(numbers[27]);
    if (parts.size() >= 7) desc.indirectBuffer = std::string(parts[6]);
    return desc;
}

}

void CustomPasses::save(std::ostream& out, std::string_view prefix) const {
    if (!m_data) return;
    for (const Pass& pass : m_data->passes) {
        out << prefix << kindName(pass.info.kind) << ' ' << (pass.info.enabled ? 1 : 0);
        for (const glm::vec4& param : pass.info.params) out << ' ' << param.x << ' ' << param.y << ' ' << param.z << ' ' << param.w;
        if (pass.info.kind == PassKind::Compute) saveCompute(out, pass.compute);
        else if (pass.info.kind == PassKind::Draw) saveDraw(out, pass.draw);
        else out << '\t' << pass.effect.name << '\t' << pass.effect.path;
        out << '\n';
    }
}

bool CustomPasses::load(std::string_view line) {
    if (!m_data) return false;
    const auto parts = splitTabs(line);
    const size_t space = parts[0].find(' ');
    if (space == std::string_view::npos || parts.size() < kMinPartCount) return false;
    const std::string_view kind = parts[0].substr(0, space);
    const std::vector<float> numbers = parseNumbers(parts[0].substr(space + 1));
    if (numbers.size() < kHeaderNumberCount) return false;
    uint32_t id = 0;
    PassKind passKind = PassKind::Effect;
    if (kind == "compute" && numbers.size() >= kComputeNumberCount) {
        passKind = PassKind::Compute;
        id = addCompute(parseCompute(parts, numbers));
    } else if (kind == "draw" && numbers.size() >= kDrawNumberCount && parts.size() >= kDrawMinPartCount) {
        passKind = PassKind::Draw;
        id = addDraw(parseDraw(parts, numbers));
    } else if (kind == "effect") {
        id = addEffect({std::string(parts[1]), std::string(parts[2])});
    } else {
        return false;
    }
    setEnabled(passKind, id, numbers[0] > 0.5f);
    for (int index = 0; index < kParamCount; ++index) setParam(passKind, id, index, glm::vec4(numbers[1 + index * 4], numbers[2 + index * 4], numbers[3 + index * 4], numbers[4 + index * 4]));
    return true;
}
