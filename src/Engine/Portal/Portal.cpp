module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Engine/Log/Log.hpp"

module Engine.Portal;

import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Portal.view;

namespace {

constexpr float kPi = 3.14159265358979f;

glm::vec3 transformPoint(const glm::mat4& m, const glm::vec3& p) { return glm::vec3(m * glm::vec4(p, 1.0f)); }
glm::vec3 transformDir(const glm::mat4& m, const glm::vec3& d) { return glm::vec3(m * glm::vec4(d, 0.0f)); }

glm::vec3 cameraForward(const Camera& camera) {
    const float yaw = glm::radians(camera.getYaw());
    const float pitch = glm::radians(camera.getPitch());
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

std::array<glm::vec3, 4> cornersOf(const Portal& p) {
    const glm::vec3 c = p.center();
    const glm::vec3 u = p.right() * p.half.x;
    const glm::vec3 v = p.up() * p.half.y;
    return {c - u - v, c + u - v, c + u + v, c - u + v};
}

std::optional<float> crossing(const Portal& p, const glm::vec3& origin, const glm::vec3& direction, float maxT) {
    const glm::vec3 n = p.normal();
    const float denom = glm::dot(n, direction);
    if (denom >= -1.0e-6f) return std::nullopt;
    const float t = glm::dot(n, p.center() - origin) / denom;
    if (t < 0.0f || t > maxT) return std::nullopt;
    const glm::vec3 q = origin + direction * t - p.center();
    if (std::abs(glm::dot(q, p.right())) > p.half.x || std::abs(glm::dot(q, p.up())) > p.half.y) return std::nullopt;
    return t;
}

float rectDistance(const Portal& p, const glm::vec3& point, float& signedPlane) {
    const glm::vec3 q = point - p.center();
    signedPlane = glm::dot(q, p.normal());
    const float lu = std::max(std::abs(glm::dot(q, p.right())) - p.half.x, 0.0f);
    const float lv = std::max(std::abs(glm::dot(q, p.up())) - p.half.y, 0.0f);
    return std::sqrt(signedPlane * signedPlane + lu * lu + lv * lv);
}

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    double number = 0.0;
    bool boolean = false;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    [[nodiscard]] const Json* get(std::string_view key) const {
        for (const auto& [name, value] : members) {
            if (name == key) return &value;
        }
        return nullptr;
    }
};

struct JsonReader {
    const char* p;
    const char* end;

    void skip() {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    }
    bool literal(std::string_view word) {
        if (static_cast<size_t>(end - p) < word.size() || std::string_view(p, word.size()) != word) return false;
        p += word.size();
        return true;
    }
    bool string(std::string& out) {
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    default: out += *p; break;
                }
            } else {
                out += *p;
            }
            ++p;
        }
        if (p >= end) return false;
        ++p;
        return true;
    }
    bool value(Json& out, int depth) {
        if (depth > 32) return false;
        skip();
        if (p >= end) return false;
        if (*p == '{') {
            ++p;
            out.type = Json::Type::Object;
            skip();
            if (p < end && *p == '}') return ++p, true;
            while (true) {
                skip();
                std::string key;
                if (!string(key)) return false;
                skip();
                if (p >= end || *p != ':') return false;
                ++p;
                Json child;
                if (!value(child, depth + 1)) return false;
                out.members.emplace_back(std::move(key), std::move(child));
                skip();
                if (p < end && *p == ',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == '}') return ++p, true;
                return false;
            }
        }
        if (*p == '[') {
            ++p;
            out.type = Json::Type::Array;
            skip();
            if (p < end && *p == ']') return ++p, true;
            while (true) {
                Json child;
                if (!value(child, depth + 1)) return false;
                out.items.push_back(std::move(child));
                skip();
                if (p < end && *p == ',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == ']') return ++p, true;
                return false;
            }
        }
        if (*p == '"') {
            out.type = Json::Type::String;
            return string(out.text);
        }
        if (literal("true")) return out.type = Json::Type::Bool, out.boolean = true, true;
        if (literal("false")) return out.type = Json::Type::Bool, out.boolean = false, true;
        if (literal("null")) return out.type = Json::Type::Null, true;
        char* stop = nullptr;
        out.number = std::strtod(p, &stop);
        if (stop == p) return false;
        out.type = Json::Type::Number;
        p = stop;
        return true;
    }
};

float numberOr(const Json* j, float fallback) { return j && j->type == Json::Type::Number ? static_cast<float>(j->number) : fallback; }

bool vec3Of(const Json* j, glm::vec3& out) {
    if (!j || j->type != Json::Type::Array || j->items.size() != 3) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (j->items[i].type != Json::Type::Number) return false;
        out[static_cast<int>(i)] = static_cast<float>(j->items[i].number);
    }
    return true;
}

bool vec2Of(const Json* j, glm::vec2& out) {
    if (!j || j->type != Json::Type::Array || j->items.size() != 2) return false;
    for (size_t i = 0; i < 2; ++i) {
        if (j->items[i].type != Json::Type::Number) return false;
        out[static_cast<int>(i)] = static_cast<float>(j->items[i].number);
    }
    return true;
}

std::string fmt3(const glm::vec3& v) { return std::format("[{}, {}, {}]", v.x, v.y, v.z); }

}  // namespace

glm::mat4 portalPose(const glm::vec3& position, const glm::vec3& eulerDegrees) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = glm::rotate(m, glm::radians(eulerDegrees.x), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, glm::radians(eulerDegrees.y), glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return m;
}

glm::mat4 obliqueProjection(const glm::mat4& projection, const glm::mat4& view, const glm::vec4& worldPlane) {
    const glm::mat4 inverseView = glm::inverse(view);
    const glm::vec4 c(glm::dot(inverseView[0], worldPlane), glm::dot(inverseView[1], worldPlane), glm::dot(inverseView[2], worldPlane), glm::dot(inverseView[3], worldPlane));
    const glm::vec4 corner = glm::inverse(projection) * glm::vec4(c.x < 0.0f ? -1.0f : 1.0f, c.y < 0.0f ? -1.0f : 1.0f, 1.0f, 1.0f);
    const float d = glm::dot(c, corner);
    if (std::abs(d) < 1.0e-8f) return projection;
    const float k = 1.0f / d;
    glm::mat4 result = projection;
    result[0][2] = c.x * k;
    result[1][2] = c.y * k;
    result[2][2] = c.z * k;
    result[3][2] = c.w * k;
    return result;
}

std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& m) {
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    std::array<glm::vec4, 6> planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    for (glm::vec4& p : planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 1.0e-8f) p = p / len;
    }
    return planes;
}

void applyTransformToCamera(Camera& camera, const glm::mat4& transform) {
    const glm::vec3 front = glm::normalize(transformDir(transform, cameraForward(camera)));
    camera.setPos(transformPoint(transform, camera.getPosition()));
    camera.setOrientation(glm::degrees(std::atan2(front.z, front.x)), glm::degrees(std::asin(glm::clamp(front.y, -1.0f, 1.0f))));
}

void PortalSystem::clear() {
    m_cells.clear();
    m_portals.clear();
    m_cellPortals.clear();
    m_cameraCell = -1;
    m_hasNear = false;
    m_tracked.clear();
}

int PortalSystem::addCell(const PortalCell& cell) {
    m_cells.push_back(cell);
    m_cellPortals.emplace_back();
    if (m_cameraCell < 0) m_cameraCell = 0;
    return static_cast<int>(m_cells.size()) - 1;
}

int PortalSystem::findCell(const std::string& name) const {
    for (size_t i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

int PortalSystem::cellAt(const glm::vec3& p) const {
    for (size_t i = 0; i < m_cells.size(); ++i) {
        const PortalCell& c = m_cells[i];
        if (p.x >= c.min.x && p.y >= c.min.y && p.z >= c.min.z && p.x <= c.max.x && p.y <= c.max.y && p.z <= c.max.z) return static_cast<int>(i);
    }
    return -1;
}

int PortalSystem::resolveCell(int cell, const glm::vec3& p) const {
    if (cell >= 0 && static_cast<size_t>(cell) < m_cells.size()) {
        const PortalCell& c = m_cells[static_cast<size_t>(cell)];
        if (p.x >= c.min.x && p.y >= c.min.y && p.z >= c.min.z && p.x <= c.max.x && p.y <= c.max.y && p.z <= c.max.z) return cell;
    }
    const int found = cellAt(p);
    return found >= 0 ? found : cell;
}

void PortalSystem::setCameraCell(int cell) {
    m_cameraCell = cell;
    m_hasNear = false;
}

const std::vector<int>& PortalSystem::portalsOf(int cell) const {
    static const std::vector<int> none;
    return cell >= 0 && static_cast<size_t>(cell) < m_cellPortals.size() ? m_cellPortals[static_cast<size_t>(cell)] : none;
}

int PortalSystem::addPair(const std::string& name, const PortalEnd& a, const PortalEnd& b, float twistDegrees) {
    const int cellA = findCell(a.cell);
    const int cellB = findCell(b.cell);
    if (cellA < 0 || cellB < 0) return -1;
    Portal pa;
    Portal pb;
    pa.name = name + ".a";
    pb.name = name + ".b";
    pa.cell = cellA;
    pb.cell = cellB;
    pa.pose = portalPose(a.position, a.eulerDegrees);
    pb.pose = portalPose(b.position, b.eulerDegrees);
    pa.half = a.halfSize;
    pa.twist = twistDegrees;
    pb.twist = -twistDegrees;
    pb.half = b.halfSize;
    const glm::mat4 flip = glm::rotate(glm::mat4(1.0f), kPi, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 twist = glm::rotate(glm::mat4(1.0f), glm::radians(twistDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    pa.transform = pb.pose * twist * flip * glm::inverse(pa.pose);
    pb.transform = glm::inverse(pa.transform);
    const int ia = static_cast<int>(m_portals.size());
    pa.partner = ia + 1;
    pb.partner = ia;
    m_portals.push_back(pa);
    m_portals.push_back(pb);
    m_cellPortals[static_cast<size_t>(cellA)].push_back(ia);
    m_cellPortals[static_cast<size_t>(cellB)].push_back(ia + 1);
    return ia;
}

bool PortalSystem::parseJson(std::string_view text) {
    Json root;
    JsonReader reader{text.data(), text.data() + text.size()};
    if (!reader.value(root, 0) || root.type != Json::Type::Object) {
        Lit::Log::Error("Portal world JSON is malformed");
        return false;
    }
    PortalSystem loaded;
    if (const Json* s = root.get("settings")) {
        loaded.m_settings.maxDepth = static_cast<int>(numberOr(s->get("maxDepth"), static_cast<float>(loaded.m_settings.maxDepth)));
        loaded.m_settings.maxViews = static_cast<int>(numberOr(s->get("maxViews"), static_cast<float>(loaded.m_settings.maxViews)));
        loaded.m_settings.blendDistance = numberOr(s->get("blendDistance"), loaded.m_settings.blendDistance);
        loaded.m_settings.minPixels = numberOr(s->get("minPixels"), loaded.m_settings.minPixels);
        if (const Json* o = s->get("occlusion")) loaded.m_settings.occlusion = o->type == Json::Type::Bool ? o->boolean : o->number != 0.0;
    }
    const Json* cells = root.get("cells");
    if (!cells || cells->type != Json::Type::Array || cells->items.empty()) {
        Lit::Log::Error("Portal world JSON has no cells");
        return false;
    }
    for (const Json& c : cells->items) {
        PortalCell cell;
        const Json* name = c.get("name");
        if (!name || name->type != Json::Type::String || name->text.empty() || loaded.findCell(name->text) >= 0) {
            Lit::Log::Error("Portal cell needs a unique name");
            return false;
        }
        cell.name = name->text;
        vec3Of(c.get("min"), cell.min);
        vec3Of(c.get("max"), cell.max);
        vec3Of(c.get("gravity"), cell.params.gravity);
        vec3Of(c.get("tint"), cell.params.tint);
        cell.params.sun = numberOr(c.get("sun"), 1.0f);
        cell.params.ambient = numberOr(c.get("ambient"), 1.0f);
        if (const Json* sc = c.get("scalars"); sc && sc->type == Json::Type::Object) {
            for (const auto& [key, v] : sc->members) cell.params.scalars[key] = static_cast<float>(v.number);
        }
        loaded.addCell(cell);
    }
    if (const Json* portals = root.get("portals"); portals && portals->type == Json::Type::Array) {
        auto readEnd = [&](const Json* j, PortalEnd& end) {
            if (!j) return false;
            const Json* cell = j->get("cell");
            if (!cell || cell->type != Json::Type::String) return false;
            end.cell = cell->text;
            vec3Of(j->get("position"), end.position);
            vec3Of(j->get("rotation"), end.eulerDegrees);
            vec2Of(j->get("size"), end.halfSize);
            return true;
        };
        for (const Json& p : portals->items) {
            PortalEnd a;
            PortalEnd b;
            const Json* name = p.get("name");
            if (!readEnd(p.get("a"), a) || !readEnd(p.get("b"), b) || loaded.addPair(name ? name->text : "portal", a, b, numberOr(p.get("twist"), 0.0f)) < 0) {
                Lit::Log::Error("Portal pair references an unknown cell or is incomplete");
                return false;
            }
        }
    }
    loaded.m_cameraCell = 0;
    const PortalSettings keep = loaded.m_settings;
    *this = std::move(loaded);
    m_settings = keep;
    return true;
}

bool PortalSystem::loadJson(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Lit::Log::Error("Portal world '{}' not found", path);
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parseJson(buffer.str());
}

std::string PortalSystem::toJson() const {
    std::string out = "{\n";
    out += std::format("  \"settings\": {{\"maxDepth\": {}, \"maxViews\": {}, \"blendDistance\": {}, \"minPixels\": {}, \"occlusion\": {}}},\n", m_settings.maxDepth, m_settings.maxViews, m_settings.blendDistance, m_settings.minPixels, m_settings.occlusion ? "true" : "false");
    out += "  \"cells\": [\n";
    for (size_t i = 0; i < m_cells.size(); ++i) {
        const PortalCell& c = m_cells[i];
        out += std::format("    {{\"name\": \"{}\", \"min\": {}, \"max\": {}, \"gravity\": {}, \"tint\": {}, \"sun\": {}, \"ambient\": {}, \"scalars\": {{", c.name, fmt3(c.min), fmt3(c.max), fmt3(c.params.gravity), fmt3(c.params.tint), c.params.sun, c.params.ambient);
        bool first = true;
        for (const auto& [key, value] : c.params.scalars) {
            out += std::format("{}\"{}\": {}", first ? "" : ", ", key, value);
            first = false;
        }
        out += std::format("}}}}{}\n", i + 1 < m_cells.size() ? "," : "");
    }
    out += "  ],\n  \"portals\": [\n";
    for (size_t i = 0; i + 1 < m_portals.size(); i += 2) {
        const Portal& a = m_portals[i];
        const Portal& b = m_portals[i + 1];
        auto euler = [](const Portal& p) {
            const glm::mat4& m = p.pose;
            const glm::vec3 f = glm::vec3(m[2]);
            const float yaw = std::atan2(f.x, f.z);
            const float pitch = -std::asin(glm::clamp(f.y, -1.0f, 1.0f));
            const float roll = std::atan2(glm::vec3(m[0]).y, glm::vec3(m[1]).y);
            return glm::vec3(glm::degrees(yaw), glm::degrees(pitch), glm::degrees(roll));
        };
        std::string name = a.name.substr(0, a.name.size() >= 2 ? a.name.size() - 2 : 0);
        out += std::format("    {{\"name\": \"{}\", \"a\": {{\"cell\": \"{}\", \"position\": {}, \"rotation\": {}, \"size\": [{}, {}]}}, \"b\": {{\"cell\": \"{}\", \"position\": {}, \"rotation\": {}, \"size\": [{}, {}]}}, \"twist\": {}}}{}\n", name, m_cells[static_cast<size_t>(a.cell)].name, fmt3(a.center()), fmt3(euler(a)), a.half.x, a.half.y, m_cells[static_cast<size_t>(b.cell)].name, fmt3(b.center()), fmt3(euler(b)), b.half.x, b.half.y, a.twist, i + 2 < m_portals.size() ? "," : "");
    }
    out += "  ]\n}\n";
    return out;
}

PortalFrame PortalSystem::buildFrame(const Camera& camera, float viewportHeight, const PortalBlockedFn& blocked) const {
    PortalFrame frame;
    if (m_portals.empty() || m_cameraCell < 0) return frame;
    const size_t maxViews = static_cast<size_t>(std::max(m_settings.maxViews, 2));
    frame.nodes.reserve(maxViews);

    const glm::mat4 projection = camera.getProjectionMatrix();
    const glm::mat4 view0 = camera.getViewMatrix();
    const float tanHalf = 1.0f / std::max(projection[1][1], 1.0e-4f);
    const float aspect = projection[1][1] / std::max(projection[0][0], 1.0e-4f);
    const float reach = camera.getNearPlane() * std::sqrt(1.0f + tanHalf * tanHalf * (1.0f + aspect * aspect));
    const float farPlane = camera.getFarPlane();

    auto fill = [&](PortalViewNode& node, int cell) {
        node.cell = cell;
        node.sunScale = m_cells[static_cast<size_t>(cell)].params.sun;
        node.ambientScale = m_cells[static_cast<size_t>(cell)].params.ambient;
        node.label = m_cells[static_cast<size_t>(cell)].name;
    };

    PortalViewNode root;
    root.view = view0;
    root.projection = projection;
    root.position = camera.getPosition();
    root.planes = extractFrustumPlanes(projection * view0);
    root.key = 1469598103934665603ull ^ static_cast<uint64_t>(m_cameraCell + 1);
    fill(root, m_cameraCell);
    frame.nodes.push_back(std::move(root));

    struct Pending {
        int node;
        glm::mat4 camWorld;
        int entry;
        float pixels;
    };
    std::vector<Pending> queue;
    queue.push_back({0, glm::inverse(view0), -1, 1.0e30f});
    const size_t hardCap = 24;

    for (size_t qi = 0; qi < queue.size(); ++qi) {
        const size_t best = static_cast<size_t>(std::ranges::max_element(queue.begin() + static_cast<std::ptrdiff_t>(qi), queue.end(), {}, &Pending::pixels) - queue.begin());
        std::swap(queue[qi], queue[best]);
        const Pending cur = queue[qi];
        const int cell = frame.nodes[static_cast<size_t>(cur.node)].cell;
        const int depth = frame.nodes[static_cast<size_t>(cur.node)].depth;
        const std::array<glm::vec4, 6> planes = frame.nodes[static_cast<size_t>(cur.node)].planes;
        const uint64_t parentKey = frame.nodes[static_cast<size_t>(cur.node)].key;
        const std::string parentLabel = frame.nodes[static_cast<size_t>(cur.node)].label;
        const glm::vec3 eye(cur.camWorld[3]);

        struct Candidate {
            int id;
            float pixels;
        };
        std::vector<Candidate> candidates;
        for (int id : m_cellPortals[static_cast<size_t>(cell)]) {
            if (id == cur.entry) continue;
            const Portal& p = m_portals[static_cast<size_t>(id)];
            const glm::vec3 offset = eye - p.center();
            const float distE = glm::dot(p.normal(), offset);
            if (distE < -reach) continue;
            const auto corners = cornersOf(p);
            bool inside = true;
            for (const glm::vec4& plane : planes) {
                bool anyIn = false;
                for (const glm::vec3& c : corners) anyIn = anyIn || glm::dot(glm::vec3(plane), c) + plane.w >= 0.0f;
                if (!anyIn) inside = false;
            }
            if (!inside) continue;
            const float dist = std::max(glm::length(offset), 1.0e-3f);
            const float radius = glm::length(glm::vec3(p.half.x, p.half.y, 0.0f));
            const float pixels = std::min(viewportHeight * radius / (dist * tanHalf), cur.pixels);
            if (blocked && m_settings.occlusion && distE > reach) {
                bool visible = false;
                const glm::vec3 samples[5] = {p.center(), p.center() + p.right() * p.half.x * 0.8f + p.up() * p.half.y * 0.8f, p.center() - p.right() * p.half.x * 0.8f + p.up() * p.half.y * 0.8f, p.center() + p.right() * p.half.x * 0.8f - p.up() * p.half.y * 0.8f, p.center() - p.right() * p.half.x * 0.8f - p.up() * p.half.y * 0.8f};
                for (const glm::vec3& s : samples) visible = visible || !blocked(eye, s);
                if (!visible) continue;
            }
            candidates.push_back({id, pixels});
        }
        std::ranges::stable_sort(candidates, [](const Candidate& a, const Candidate& b) { return a.pixels > b.pixels; });

        for (const Candidate& cand : candidates) {
            const Portal& src = m_portals[static_cast<size_t>(cand.id)];
            const Portal& dst = m_portals[static_cast<size_t>(src.partner)];
            PortalSurface surface;
            surface.center = src.center();
            surface.axisU = src.right() * src.half.x;
            surface.axisV = src.up() * src.half.y;
            surface.normal = src.normal();
            surface.fallback = m_cells[static_cast<size_t>(dst.cell)].params.tint;
            const float distE = glm::dot(src.normal(), eye - src.center());
            const bool near = distE < reach * 1.5f;
            const bool large = cand.pixels >= viewportHeight * 0.2f;
            if (depth < m_settings.maxDepth + (large ? 1 : 0) && frame.nodes.size() < (large ? hardCap : maxViews) && (cand.pixels >= m_settings.minPixels || near)) {
                const glm::mat4 camWorld = src.transform * cur.camWorld;
                const glm::mat4 viewC = glm::inverse(camWorld);
                const glm::vec3 eyeC(camWorld[3]);
                const glm::vec3 nB = dst.normal();
                const float distB = glm::dot(nB, eyeC - dst.center());
                PortalViewNode child;
                child.view = viewC;
                child.position = eyeC;
                child.depth = depth + 1;
                child.key = (parentKey ^ static_cast<uint64_t>(cand.id + 1)) * 1099511628211ull;
                fill(child, dst.cell);
                child.label = parentLabel + ">" + child.label;
                const glm::vec4 clipPlane(nB, -glm::dot(nB, dst.center()));
                const bool oblique = distB < -0.01f;
                child.projection = oblique ? obliqueProjection(projection, viewC, clipPlane) : projection;
                if (distB < -0.02f) {
                    child.masked = true;
                    child.entryCenter = dst.center();
                    child.entryAxisU = dst.right() * dst.half.x;
                    child.entryAxisV = dst.up() * dst.half.y;
                    child.entryNormal = dst.normal();
                    const auto corners = cornersOf(dst);
                    const glm::vec3 centerB = dst.center();
                    for (int i = 0; i < 4; ++i) {
                        glm::vec3 n = glm::normalize(glm::cross(corners[static_cast<size_t>(i)] - eyeC, corners[static_cast<size_t>((i + 1) % 4)] - eyeC));
                        if (glm::dot(n, centerB - eyeC) < 0.0f) n = -n;
                        child.planes[static_cast<size_t>(i)] = glm::vec4(n, -glm::dot(n, eyeC));
                    }
                    child.planes[4] = clipPlane;
                    const glm::vec3 f = glm::normalize(-glm::vec3(camWorld[2]));
                    child.planes[5] = glm::vec4(-f, glm::dot(f, eyeC) + farPlane);
                } else {
                    child.planes = extractFrustumPlanes(projection * viewC);
                }
                surface.child = static_cast<int>(frame.nodes.size());
                frame.nodes.push_back(std::move(child));
                queue.push_back({surface.child, camWorld, src.partner, cand.pixels});
            }
            frame.nodes[static_cast<size_t>(cur.node)].surfaces.push_back(surface);
        }
    }
    return frame;
}

int PortalSystem::stepPoint(glm::vec3& position, int& cell, const glm::vec3& previous, bool upright) const {
    for (int id : portalsOf(cell)) {
        const Portal& p = m_portals[static_cast<size_t>(id)];
        if (upright && transformDir(p.transform, glm::vec3(0.0f, 1.0f, 0.0f)).y < 0.999f) continue;
        const glm::vec3 delta = position - previous;
        const float length = glm::length(delta);
        if (length < 1.0e-9f) continue;
        const auto t = crossing(p, previous, delta / length, length);
        if (!t) continue;
        position = transformPoint(p.transform, position);
        cell = m_portals[static_cast<size_t>(p.partner)].cell;
        return id;
    }
    return -1;
}

std::optional<std::pair<int, float>> PortalSystem::firstCrossing(int cell, const glm::vec3& origin, const glm::vec3& direction, float maxT) const {
    std::optional<std::pair<int, float>> best;
    for (int id : portalsOf(cell)) {
        const auto t = crossing(m_portals[static_cast<size_t>(id)], origin, direction, best ? best->second : maxT);
        if (t && *t < (best ? best->second : maxT)) best = std::make_pair(id, *t);
    }
    return best;
}

int PortalSystem::step(Camera& camera) {
    if (m_portals.empty()) return -1;
    if (m_cameraCell < 0) m_cameraCell = std::max(cellAt(camera.getPosition()), 0);
    const glm::vec3 nearCenter = camera.getPosition() + cameraForward(camera) * camera.getNearPlane();
    if (!m_hasNear) {
        m_previousNear = nearCenter;
        m_hasNear = true;
        return -1;
    }
    glm::vec3 moved = nearCenter;
    int cell = m_cameraCell;
    const int id = stepPoint(moved, cell, m_previousNear, true);
    if (id < 0) {
        m_previousNear = nearCenter;
        m_cameraCell = resolveCell(m_cameraCell, nearCenter);
        return -1;
    }
    applyTransformToCamera(camera, m_portals[static_cast<size_t>(id)].transform);
    m_cameraCell = cell;
    m_previousNear = camera.getPosition() + cameraForward(camera) * camera.getNearPlane();
    return id;
}

PortalCellParams PortalSystem::sampleParams(int cell, const glm::vec3& position) const {
    if (cell < 0 || static_cast<size_t>(cell) >= m_cells.size()) return {};
    const PortalCellParams& base = m_cells[static_cast<size_t>(cell)].params;
    const float reach = std::max(m_settings.blendDistance, 1.0e-4f);
    struct Pull {
        float weight;
        const PortalCellParams* other;
        glm::vec3 gravity;
    };
    std::vector<Pull> pulls;
    float total = 0.0f;
    for (int id : portalsOf(cell)) {
        const Portal& p = m_portals[static_cast<size_t>(id)];
        float side = 0.0f;
        const float d = rectDistance(p, position, side);
        const float w = glm::clamp(0.5f * (1.0f - (side >= 0.0f ? d : -d) / reach), 0.0f, 1.0f);
        if (w <= 0.0f) continue;
        const PortalCellParams& other = m_cells[static_cast<size_t>(m_portals[static_cast<size_t>(p.partner)].cell)].params;
        pulls.push_back({w, &other, transformDir(glm::inverse(p.transform), other.gravity)});
        total += w;
    }
    const float scale = total > 1.0f ? 1.0f / total : 1.0f;
    PortalCellParams out = base;
    for (const Pull& pull : pulls) {
        const float w = pull.weight * scale;
        out.gravity += w * (pull.gravity - base.gravity);
        out.tint += w * (pull.other->tint - base.tint);
        out.sun += w * (pull.other->sun - base.sun);
        out.ambient += w * (pull.other->ambient - base.ambient);
        for (const auto& [key, value] : pull.other->scalars) {
            const auto it = base.scalars.find(key);
            const float from = it != base.scalars.end() ? it->second : 0.0f;
            const auto cur = out.scalars.find(key);
            out.scalars[key] = (cur != out.scalars.end() ? cur->second : from) + w * (value - from);
        }
    }
    return out;
}

PortalRayHit PortalSystem::raycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, int cell, int maxCrossings) const {
    PortalRayHit result;
    glm::vec3 o = origin;
    glm::vec3 d = glm::normalize(direction);
    float remaining = maxDistance;
    int current = cell >= 0 ? cell : cellAt(origin);
    result.cell = current;
    result.direction = d;
    float travelled = 0.0f;
    for (int i = 0; i <= maxCrossings; ++i) {
        std::optional<RayHit> hit;
        float skip = 0.0f;
        for (int tries = 0; tries < 8; ++tries) {
            hit = world.raycast(o + d * skip, d, remaining - skip);
            if (!hit) break;
            hit->distance += skip;
            if (!isGhost(hit->entity)) break;
            skip = hit->distance + 1.0e-3f;
            hit.reset();
        }
        float bestT = hit ? hit->distance : remaining;
        int bestPortal = -1;
        for (int id : portalsOf(current)) {
            const auto t = crossing(m_portals[static_cast<size_t>(id)], o, d, bestT);
            if (t && *t < bestT) {
                bestT = *t;
                bestPortal = id;
            }
        }
        if (bestPortal < 0) {
            result.hit = hit;
            result.distance = travelled + (hit ? hit->distance : remaining);
            if (result.hit) result.hit->distance = result.distance;
            result.point = o + d * (hit ? hit->distance : remaining);
            result.cell = current;
            return result;
        }
        const Portal& p = m_portals[static_cast<size_t>(bestPortal)];
        const glm::vec3 at = o + d * bestT;
        o = transformPoint(p.transform, at);
        d = glm::normalize(transformDir(p.transform, d));
        o = o + d * 1.0e-3f;
        travelled += bestT + 1.0e-3f;
        remaining -= bestT + 1.0e-3f;
        current = m_portals[static_cast<size_t>(p.partner)].cell;
        ++result.crossings;
        result.direction = d;
        if (remaining <= 0.0f) break;
    }
    result.distance = travelled;
    result.point = o;
    result.cell = current;
    return result;
}

std::vector<PortalOverlap> PortalSystem::overlapSphere(World& world, const glm::vec3& center, float radius, int cell) const {
    std::vector<PortalOverlap> out;
    std::unordered_set<uint64_t> seen;
    auto key = [](EntityHandle e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; };
    for (EntityHandle e : world.overlapSphere(center, radius)) {
        if (isGhost(e) || !seen.insert(key(e)).second) continue;
        out.push_back({e, 0});
    }
    const int current = cell >= 0 ? cell : cellAt(center);
    for (int id : portalsOf(current)) {
        const Portal& p = m_portals[static_cast<size_t>(id)];
        float side = 0.0f;
        if (rectDistance(p, center, side) >= radius || side < -radius) continue;
        for (EntityHandle e : world.overlapSphere(transformPoint(p.transform, center), radius)) {
            if (isGhost(e) || !seen.insert(key(e)).second) continue;
            out.push_back({e, 1});
        }
    }
    return out;
}

void PortalSystem::track(World& world, EntityHandle entity) {
    for (const Tracked& t : m_tracked) {
        if (t.entity == entity) return;
    }
    Tracked t;
    t.entity = entity;
    t.previous = world.getWorldPosition(entity);
    t.cell = cellAt(t.previous);
    m_tracked.push_back(t);
}

void PortalSystem::untrack(World& world, EntityHandle entity) {
    for (size_t i = 0; i < m_tracked.size(); ++i) {
        if (!(m_tracked[i].entity == entity)) continue;
        if (world.isAlive(m_tracked[i].ghost)) world.destroy(m_tracked[i].ghost);
        m_tracked.erase(m_tracked.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

EntityHandle PortalSystem::ghostOf(EntityHandle entity) const {
    for (const Tracked& t : m_tracked) {
        if (t.entity == entity) return t.ghost;
    }
    return NULL_ENTITY;
}

int PortalSystem::objectCell(EntityHandle entity) const {
    for (const Tracked& t : m_tracked) {
        if (t.entity == entity) return t.cell;
    }
    return -1;
}

bool PortalSystem::isGhost(EntityHandle entity) const {
    for (const Tracked& t : m_tracked) {
        if (t.ghost == entity && !(entity == NULL_ENTITY)) return true;
    }
    return false;
}

void PortalSystem::updateObjects(World& world) {
    for (size_t i = 0; i < m_tracked.size();) {
        Tracked& t = m_tracked[i];
        if (!world.isAlive(t.entity)) {
            if (world.isAlive(t.ghost)) world.destroy(t.ghost);
            m_tracked.erase(m_tracked.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        glm::vec3 position = world.getWorldPosition(t.entity);
        if (t.cell < 0) t.cell = std::max(cellAt(position), 0);
        glm::vec3 moved = position;
        int cell = t.cell;
        const int crossed = stepPoint(moved, cell, t.previous);
        if (crossed >= 0) {
            world.setWorldMatrix(t.entity, m_portals[static_cast<size_t>(crossed)].transform * world.getWorldMatrix(t.entity));
            position = world.getWorldPosition(t.entity);
            t.cell = cell;
            if (m_crossHook) m_crossHook(world, t.entity, m_portals[static_cast<size_t>(crossed)].transform);
        }
        t.previous = position;
        t.cell = resolveCell(t.cell, position);

        const glm::vec4 bounds = world.getWorldBounds(t.entity);
        const float radius = bounds.w;
        int straddle = -1;
        float best = 1.0e30f;
        for (int id : portalsOf(t.cell)) {
            const Portal& p = m_portals[static_cast<size_t>(id)];
            float side = 0.0f;
            const float d = rectDistance(p, position, side);
            if (d < radius && side >= -radius && d < best) {
                best = d;
                straddle = id;
            }
        }
        if (straddle >= 0) {
            const Portal& p = m_portals[static_cast<size_t>(straddle)];
            if (!world.isAlive(t.ghost)) {
                const RenderableComponent& r = world.getRenderable(t.entity);
                EntityDesc desc;
                desc.name = world.getName(t.entity) + " (portal twin)";
                desc.mesh = world.getMesh(t.entity);
                desc.material = r.material_uuid;
                desc.shader = r.shaderId;
                desc.alpha = r.alpha;
                t.ghost = world.create(desc);
            }
            world.setWorldMatrix(t.ghost, p.transform * world.getWorldMatrix(t.entity));
            world.setVisible(t.ghost, world.isVisible(t.entity));
        } else if (world.isAlive(t.ghost)) {
            world.setVisible(t.ghost, false);
        }
        ++i;
    }
}
