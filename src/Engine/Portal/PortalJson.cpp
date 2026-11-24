module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Log/Log.hpp"

module Engine.Portal;

import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.entity;
import Engine.Portal.view;
import Engine.Json;

#include "PortalInternal.hpp"

namespace {

constexpr unsigned char kFirstPrintable = 0x20;
constexpr float kMaxSettingCount = 1.0e6f;

int countOr(const Json* node, int fallback) {
    if (!node || !node->isNumber()) return fallback;
    const double value = node->asNumber();
    return value >= 0.0 && value <= kMaxSettingCount ? static_cast<int>(value) : fallback;
}

float numberOr(const Json* node, float fallback) { return node && node->isNumber() ? static_cast<float>(node->asNumber()) : fallback; }

template <int Size, typename Vector>
bool readVector(const Json* node, Vector& out) {
    if (!node || !node->isArray() || node->size() != static_cast<size_t>(Size)) return false;
    Vector parsed;
    for (size_t component = 0; component < static_cast<size_t>(Size); ++component) {
        if (!node->items()[component].isNumber()) return false;
        parsed[static_cast<int>(component)] = static_cast<float>(node->items()[component].asNumber());
    }
    out = parsed;
    return true;
}

bool readVec3(const Json* node, glm::vec3& out) { return readVector<3>(node, out); }

bool readVec2(const Json* node, glm::vec2& out) { return readVector<2>(node, out); }

std::string formatVec3(const glm::vec3& value) { return std::format("[{}, {}, {}]", value.x, value.y, value.z); }

std::string escapeJson(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        if (character == '"' || character == '\\') {
            escaped += '\\';
            escaped += character;
        } else if (static_cast<unsigned char>(character) < kFirstPrintable) {
            escaped += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(character)));
        } else {
            escaped += character;
        }
    }
    return escaped;
}

void readSettings(const Json& root, PortalSettings& settings) {
    const Json* node = root.get("settings");
    if (!node) return;
    settings.maxDepth = countOr(node->get("maxDepth"), settings.maxDepth);
    settings.maxViews = countOr(node->get("maxViews"), settings.maxViews);
    settings.blendDistance = numberOr(node->get("blendDistance"), settings.blendDistance);
    settings.minPixels = numberOr(node->get("minPixels"), settings.minPixels);
    if (const Json* occlusion = node->get("occlusion")) settings.occlusion = occlusion->isBool() ? occlusion->asBool() : occlusion->asNumber() != 0.0;
    settings.tintLighting = numberOr(node->get("tintLighting"), settings.tintLighting);
    if (const Json* transport = node->get("lightTransport")) settings.lightTransport = transport->isBool() ? transport->asBool() : transport->asNumber() != 0.0;
    if (const Json* upright = node->get("uprightCamera")) settings.uprightCamera = upright->isBool() ? upright->asBool() : upright->asNumber() != 0.0;
    settings.uprightSeconds = std::max(numberOr(node->get("uprightSeconds"), settings.uprightSeconds), 0.0f);
    settings.spill = std::max(numberOr(node->get("spill"), settings.spill), 0.0f);
}

bool readCell(const Json& node, const PortalSystem& system, PortalCell& cell) {
    const Json* name = node.get("name");
    if (!name || !name->isString() || name->asString().empty() || system.findCell(name->asString()) >= 0) {
        Lit::Log::Error("Portal cell needs a unique name");
        return false;
    }
    cell.name = name->asString();
    readVec3(node.get("min"), cell.min);
    readVec3(node.get("max"), cell.max);
    readVec3(node.get("gravity"), cell.params.gravity);
    readVec3(node.get("tint"), cell.params.tint);
    cell.params.sun = numberOr(node.get("sun"), 1.0f);
    cell.params.ambient = numberOr(node.get("ambient"), 1.0f);
    if (const Json* scalars = node.get("scalars"); scalars && scalars->isObject()) {
        for (const auto& [key, value] : scalars->members()) cell.params.scalars[key] = static_cast<float>(value.asNumber());
    }
    return true;
}

bool readEnd(const Json* node, PortalEnd& end) {
    if (!node) return false;
    const Json* cell = node->get("cell");
    if (!cell || !cell->isString()) return false;
    end.cell = cell->asString();
    readVec3(node->get("position"), end.position);
    readVec3(node->get("rotation"), end.eulerDegrees);
    readVec2(node->get("size"), end.halfSize);
    return true;
}

bool flagOr(const Json* node, bool fallback) {
    if (!node) return fallback;
    if (node->isBool()) return node->asBool();
    return node->isNumber() ? node->asNumber() != 0.0 : fallback;
}

PortalLook readLook(const Json& pair) {
    PortalLook look;
    if (const Json* shape = pair.get("shape"); shape && shape->isString()) {
        const std::string& name = shape->asString();
        look.shape = name == "ellipse" ? PortalShape::Ellipse : name == "rounded" ? PortalShape::Rounded : PortalShape::Rect;
    }
    look.cornerRadius = std::max(numberOr(pair.get("cornerRadius"), look.cornerRadius), 0.0f);
    look.spill = std::max(numberOr(pair.get("spill"), look.spill), 0.0f);
    if (const Json* frame = pair.get("frame")) {
        PortalFrameStyle& style = look.frame;
        if (frame->isBool()) {
            style.enabled = frame->asBool();
        } else if (frame->isObject()) {
            style.enabled = flagOr(frame->get("enabled"), true);
            style.width = std::max(numberOr(frame->get("width"), style.width), 0.0f);
            style.depth = std::max(numberOr(frame->get("depth"), style.depth), 0.0f);
            style.back = std::max(numberOr(frame->get("back"), style.back), 0.0f);
            style.bevel = std::max(numberOr(frame->get("bevel"), style.bevel), 0.0f);
            readVec3(frame->get("color"), style.color);
            style.metalness = std::clamp(numberOr(frame->get("metalness"), style.metalness), 0.0f, 1.0f);
            style.roughness = std::clamp(numberOr(frame->get("roughness"), style.roughness), 0.0f, 1.0f);
            readVec3(frame->get("emissive"), style.emissive);
            style.emissiveIntensity = std::max(numberOr(frame->get("emissiveIntensity"), style.emissiveIntensity), 0.0f);
        }
    }
    if (const Json* edge = pair.get("edge"); edge && edge->isObject()) {
        PortalEdgeStyle& style = look.edge;
        style.feather = std::max(numberOr(edge->get("feather"), style.feather), 0.0f);
        style.rim = std::max(numberOr(edge->get("rim"), style.rim), 0.0f);
        readVec3(edge->get("rimColor"), style.rimColor);
        style.rimWidth = std::max(numberOr(edge->get("rimWidth"), style.rimWidth), 0.0f);
        style.fresnel = std::max(numberOr(edge->get("fresnel"), style.fresnel), 0.0f);
        style.distortion = std::max(numberOr(edge->get("distortion"), style.distortion), 0.0f);
        style.chroma = std::max(numberOr(edge->get("chroma"), style.chroma), 0.0f);
        style.dissolve = std::clamp(numberOr(edge->get("dissolve"), style.dissolve), 0.0f, 1.0f);
    }
    return look;
}

std::string lookJson(const PortalLook& look) {
    const PortalFrameStyle& frame = look.frame;
    const PortalEdgeStyle& edge = look.edge;
    return std::format("\"shape\": \"{}\", \"cornerRadius\": {}, \"spill\": {}, \"frame\": {{\"enabled\": {}, \"width\": {}, \"depth\": {}, \"back\": {}, \"bevel\": {}, \"color\": {}, \"metalness\": {}, \"roughness\": {}, \"emissive\": {}, \"emissiveIntensity\": {}}}, "
                       "\"edge\": {{\"feather\": {}, \"rim\": {}, \"rimColor\": {}, \"rimWidth\": {}, \"fresnel\": {}, \"distortion\": {}, \"chroma\": {}, \"dissolve\": {}}}",
                       portalShapeName(look.shape), look.cornerRadius, look.spill, frame.enabled ? "true" : "false", frame.width, frame.depth, frame.back, frame.bevel, formatVec3(frame.color), frame.metalness, frame.roughness, formatVec3(frame.emissive), frame.emissiveIntensity,
                       edge.feather, edge.rim, formatVec3(edge.rimColor), edge.rimWidth, edge.fresnel, edge.distortion, edge.chroma, edge.dissolve);
}

bool readPairs(const Json& root, PortalSystem& system) {
    const Json* pairs = root.get("portals");
    if (!pairs || !pairs->isArray()) return true;
    for (const Json& pair : pairs->items()) {
        PortalEnd endA;
        PortalEnd endB;
        const Json* name = pair.get("name");
        const bool complete = readEnd(pair.get("a"), endA) && readEnd(pair.get("b"), endB);
        if (!complete || system.addPair(name ? name->asString() : "portal", endA, endB, numberOr(pair.get("twist"), 0.0f), readLook(pair)) < 0) {
            Lit::Log::Error("Portal pair references an unknown cell or is incomplete");
            return false;
        }
    }
    return true;
}

}

bool PortalSystem::parseJson(std::string_view text) {
    const std::optional<Json> parsed = Json::parse(text);
    if (!parsed || !parsed->isObject()) {
        Lit::Log::Error("Portal world JSON is malformed");
        return false;
    }
    const Json& root = *parsed;
    PortalSystem loaded;
    readSettings(root, loaded.m_settings);
    const Json* cells = root.get("cells");
    if (!cells || !cells->isArray() || cells->size() == 0) {
        Lit::Log::Error("Portal world JSON has no cells");
        return false;
    }
    for (const Json& cellNode : cells->items()) {
        PortalCell cell;
        if (!readCell(cellNode, loaded, cell)) return false;
        loaded.addCell(cell);
    }
    if (!readPairs(root, loaded)) return false;
    loaded.m_cameraCell = 0;
    PortalCrossHook keptHook = std::move(m_crossHook);
    *this = std::move(loaded);
    m_crossHook = std::move(keptHook);
    return true;
}

bool PortalSystem::loadJson(const std::string& path) {
    const std::optional<std::string> text = readTextFile(path);
    if (!text) {
        Lit::Log::Error("Portal world '{}' not found", path);
        return false;
    }
    return parseJson(*text);
}

std::string PortalSystem::toJson() const {
    std::string out = "{\n";
    out += std::format("  \"settings\": {{\"maxDepth\": {}, \"maxViews\": {}, \"blendDistance\": {}, \"minPixels\": {}, \"occlusion\": {}, \"tintLighting\": {}, \"lightTransport\": {}, \"uprightCamera\": {}, \"uprightSeconds\": {}, \"spill\": {}}},\n", m_settings.maxDepth, m_settings.maxViews, m_settings.blendDistance,
                       m_settings.minPixels, m_settings.occlusion ? "true" : "false", m_settings.tintLighting, m_settings.lightTransport ? "true" : "false", m_settings.uprightCamera ? "true" : "false", m_settings.uprightSeconds, m_settings.spill);
    out += "  \"cells\": [\n";
    for (size_t index = 0; index < m_cells.size(); ++index) {
        const PortalCell& cell = m_cells[index];
        out += std::format("    {{\"name\": \"{}\", \"min\": {}, \"max\": {}, \"gravity\": {}, \"tint\": {}, \"sun\": {}, \"ambient\": {}, \"scalars\": {{", escapeJson(cell.name), formatVec3(cell.min), formatVec3(cell.max), formatVec3(cell.params.gravity), formatVec3(cell.params.tint), cell.params.sun, cell.params.ambient);
        bool first = true;
        for (const auto& [key, value] : cell.params.scalars) {
            out += std::format("{}\"{}\": {}", first ? "" : ", ", escapeJson(key), value);
            first = false;
        }
        out += std::format("}}}}{}\n", index + 1 < m_cells.size() ? "," : "");
    }
    out += "  ],\n  \"portals\": [\n";
    for (size_t index = 0; index + 1 < m_portals.size(); index += 2) {
        const Portal& portalA = m_portals[index];
        const Portal& portalB = m_portals[index + 1];
        const std::string name = portalA.name.substr(0, portalA.name.size() >= 2 ? portalA.name.size() - 2 : 0);
        out += std::format("    {{\"name\": \"{}\", \"a\": {{\"cell\": \"{}\", \"position\": {}, \"rotation\": {}, \"size\": [{}, {}]}}, \"b\": {{\"cell\": \"{}\", \"position\": {}, \"rotation\": {}, \"size\": [{}, {}]}}, \"twist\": {}, {}}}{}\n", escapeJson(name), escapeJson(m_cells[static_cast<size_t>(portalA.cell)].name), formatVec3(portalA.center()), formatVec3(portalA.euler), portalA.half.x, portalA.half.y, escapeJson(m_cells[static_cast<size_t>(portalB.cell)].name), formatVec3(portalB.center()), formatVec3(portalB.euler), portalB.half.x, portalB.half.y, portalA.twist, lookJson(portalA.look), index + 2 < m_portals.size() ? "," : "");
    }
    out += "  ]\n}\n";
    return out;
}
