module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

module Engine.Portal;

import Engine.glm;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Portal.view;

namespace {

struct Builder {
    PortalSystem& portals;
    World& world;
    const PortalSceneKit& kit;

    uint32_t material(const glm::vec3& color, float roughness = 0.6f, const glm::vec3& emissive = glm::vec3(0.0f), float intensity = 0.0f) const {
        return kit.material ? kit.material(color, emissive, intensity, roughness) : 0u;
    }

    EntityHandle box(const std::string& name, const glm::vec3& lo, const glm::vec3& hi, uint32_t mat, uint32_t shader = 0) const {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = kit.cube;
        desc.material = mat;
        desc.shader = shader;
        desc.position = lo;
        desc.scale = hi - lo;
        return world.create(desc);
    }

    EntityHandle light(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, float range) const {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = NO_MESH;
        desc.position = position;
        const EntityHandle e = world.create(desc);
        LightComponent l;
        l.type = LightComponent::Type::Point;
        l.color = color;
        l.intensity = intensity;
        l.range = range;
        world.add<LightComponent>(e, l);
        return e;
    }
};

struct Opening {
    int wall;
    float along;
    float halfWidth;
    float top;
};

struct RoomStyle {
    glm::vec3 wall;
    glm::vec3 floor;
    glm::vec3 ceiling;
    uint32_t floorShader = 0;
};

void buildRoom(Builder& b, const std::string& name, const glm::vec3& center, const glm::vec3& size, const RoomStyle& style, const std::vector<Opening>& openings) {
    const float t = 0.4f;
    const float hx = size.x * 0.5f;
    const float hz = size.z * 0.5f;
    const float h = size.y;
    const uint32_t wallMat = b.material(style.wall, 0.7f);
    const uint32_t floorMat = b.material(style.floor, 0.55f);
    const uint32_t ceilMat = b.material(style.ceiling, 0.8f);
    b.box(name + " floor", center + glm::vec3(-hx - t, -t, -hz - t), center + glm::vec3(hx + t, 0.0f, hz + t), floorMat, style.floorShader);
    b.box(name + " ceiling", center + glm::vec3(-hx - t, h, -hz - t), center + glm::vec3(hx + t, h + t, hz + t), ceilMat);
    for (int wall = 0; wall < 4; ++wall) {
        const bool alongX = wall >= 2;
        const float len = alongX ? size.x : size.z;
        const float pos = (wall == 0 ? hx : wall == 1 ? -hx : wall == 2 ? hz : -hz);
        std::vector<std::pair<float, float>> holes;
        float top = 0.0f;
        for (const Opening& o : openings) {
            if (o.wall != wall) continue;
            holes.emplace_back(o.along - o.halfWidth, o.along + o.halfWidth);
            top = o.top;
        }
        std::ranges::sort(holes);
        auto slab = [&](float a0, float a1, float y0, float y1, const char* tag) {
            if (a1 - a0 < 1.0e-4f || y1 - y0 < 1.0e-4f) return;
            glm::vec3 lo;
            glm::vec3 hi;
            if (alongX) {
                lo = glm::vec3(a0 - t, y0, pos - t * 0.5f);
                hi = glm::vec3(a1 + t, y1, pos + t * 0.5f);
            } else {
                lo = glm::vec3(pos - t * 0.5f, y0, a0 - t);
                hi = glm::vec3(pos + t * 0.5f, y1, a1 + t);
            }
            if (a0 > -len * 0.5f + 1.0e-3f) {
                if (alongX) lo.x = a0;
                else lo.z = a0;
            }
            if (a1 < len * 0.5f - 1.0e-3f) {
                if (alongX) hi.x = a1;
                else hi.z = a1;
            }
            b.box(name + " wall " + std::to_string(wall) + tag, center + lo, center + hi, wallMat);
        };
        float cursor = -len * 0.5f;
        for (const auto& [h0, h1] : holes) {
            slab(cursor, h0, 0.0f, h, " part");
            slab(h0, h1, top, h, " lintel");
            cursor = h1;
        }
        slab(cursor, len * 0.5f, 0.0f, h, " part");
    }
}

void buildFrames(Builder& b, const PortalSystem& portals, const std::vector<glm::vec3>& pairColors) {
    const float bar = 0.22f;
    const float t = 0.4f;
    for (size_t i = 0; i < portals.portals().size(); ++i) {
        const Portal& p = portals.portals()[i];
        const glm::vec3 color = pairColors[(i / 2) % pairColors.size()];
        const uint32_t mat = b.material(color * 0.3f, 0.4f, color, 9000.0f);
        const glm::mat4& m = p.pose;
        const std::array<std::array<glm::vec3, 2>, 3> local = {{{glm::vec3(-p.half.x - bar, -p.half.y, 0.0f), glm::vec3(-p.half.x + 0.02f, p.half.y + bar, t * 0.5f + 0.1f)},
                                                                 {glm::vec3(p.half.x - 0.02f, -p.half.y, 0.0f), glm::vec3(p.half.x + bar, p.half.y + bar, t * 0.5f + 0.1f)},
                                                                 {glm::vec3(-p.half.x - bar, p.half.y - 0.02f, 0.0f), glm::vec3(p.half.x + bar, p.half.y + bar, t * 0.5f + 0.1f)}}};
        for (size_t k = 0; k < local.size(); ++k) {
            glm::vec3 lo(1.0e30f);
            glm::vec3 hi(-1.0e30f);
            for (int c = 0; c < 8; ++c) {
                const glm::vec3 corner((c & 1) ? local[k][1].x : local[k][0].x, (c & 2) ? local[k][1].y : local[k][0].y, (c & 4) ? local[k][1].z : local[k][0].z);
                const glm::vec3 w = glm::vec3(m * glm::vec4(corner, 1.0f));
                lo = glm::min(lo, w);
                hi = glm::max(hi, w);
            }
            b.box(p.name + " frame", lo, hi, mat);
        }
    }
}

void props(Builder& b, const std::string& name, const glm::vec3& center, const glm::vec3& accent, float scale) {
    const uint32_t stone = b.material(glm::vec3(0.55f, 0.55f, 0.58f), 0.5f);
    const uint32_t glow = b.material(accent * 0.4f, 0.3f, accent, 4000.0f);
    b.box(name + " pedestal", center + glm::vec3(-0.5f, 0.0f, -0.5f) * scale, center + glm::vec3(0.5f, 0.9f, 0.5f) * scale, stone);
    b.box(name + " core", center + glm::vec3(-0.3f, 0.9f, -0.3f) * scale, center + glm::vec3(0.3f, 1.5f, 0.3f) * scale, glow);
}

void loopScene(PortalSystem& portals, World& world, const PortalSceneKit& kit) {
    Builder b{portals, world, kit};
    const float hy = 1.6f;
    const float hw = 1.1f;
    const glm::vec3 size(10.0f, 4.5f, 10.0f);
    struct Room {
        const char* name;
        glm::vec3 center;
        RoomStyle style;
        glm::vec3 accent;
    };
    const std::array<Room, 3> rooms = {{
        {"A", glm::vec3(0.0f, 0.0f, 0.0f), {glm::vec3(0.92f, 0.38f, 0.12f), glm::vec3(0.45f, 0.30f, 0.22f), glm::vec3(0.85f, 0.75f, 0.65f), kit.floorShader}, glm::vec3(1.0f, 0.35f, 0.1f)},
        {"B", glm::vec3(24.0f, 0.0f, 0.0f), {glm::vec3(0.1f, 0.75f, 0.48f), glm::vec3(0.18f, 0.40f, 0.30f), glm::vec3(0.65f, 0.85f, 0.75f), kit.floorShader}, glm::vec3(0.2f, 1.0f, 0.5f)},
        {"C", glm::vec3(48.0f, 0.0f, 0.0f), {glm::vec3(0.38f, 0.28f, 0.95f), glm::vec3(0.28f, 0.22f, 0.48f), glm::vec3(0.72f, 0.7f, 0.9f), kit.floorShader}, glm::vec3(0.5f, 0.4f, 1.0f)},
    }};
    for (const Room& r : rooms) {
        PortalCell cell;
        cell.name = r.name;
        cell.min = r.center + glm::vec3(-size.x * 0.5f - 0.4f, -0.4f, -size.z * 0.5f - 0.4f);
        cell.max = r.center + glm::vec3(size.x * 0.5f + 0.4f, size.y + 0.4f, size.z * 0.5f + 0.4f);
        cell.params.tint = r.accent * 0.35f;
        cell.params.sun = 0.0f;
        cell.params.ambient = 0.06f;
        cell.params.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
        cell.params.scalars["fog"] = 0.0f;
        portals.addCell(cell);
    }
    std::vector<std::vector<Opening>> openings(3);
    openings[0] = {{0, 0.0f, hw, 2.0f * hy}, {3, 0.0f, hw, 2.0f * hy}};
    openings[1] = {{1, 0.0f, hw, 2.0f * hy}, {0, 0.0f, hw, 2.0f * hy}};
    openings[2] = {{1, 0.0f, hw, 2.0f * hy}, {0, 0.0f, hw, 2.0f * hy}};
    for (size_t i = 0; i < rooms.size(); ++i) {
        buildRoom(b, rooms[i].name, rooms[i].center, size, rooms[i].style, openings[i]);
        props(b, std::string(rooms[i].name), rooms[i].center + glm::vec3(0.0f, 0.0f, 2.8f), rooms[i].accent, 1.0f);
        b.light(std::string(rooms[i].name) + " lamp", rooms[i].center + glm::vec3(0.0f, 3.6f, 0.0f), rooms[i].accent * 0.5f + glm::vec3(0.5f), 9000.0f, 16.0f);
    }
    auto end = [&](const char* cell, const glm::vec3& at, float yaw) {
        PortalEnd e;
        e.cell = cell;
        e.position = at;
        e.eulerDegrees = glm::vec3(yaw, 0.0f, 0.0f);
        e.halfSize = glm::vec2(hw, hy);
        return e;
    };
    portals.addPair("AB", end("A", glm::vec3(5.0f, hy, 0.0f), -90.0f), end("B", glm::vec3(19.0f, hy, 0.0f), 90.0f));
    portals.addPair("BC", end("B", glm::vec3(29.0f, hy, 0.0f), -90.0f), end("C", glm::vec3(43.0f, hy, 0.0f), 90.0f));
    portals.addPair("CA", end("C", glm::vec3(53.0f, hy, 0.0f), -90.0f), end("A", glm::vec3(0.0f, hy, -5.0f), 0.0f));
    buildFrames(b, portals, {glm::vec3(1.0f, 0.85f, 0.2f), glm::vec3(0.2f, 0.9f, 1.0f), glm::vec3(1.0f, 0.25f, 0.8f)});

    EntityDesc probe;
    probe.name = "Probe";
    probe.mesh = kit.sphere;
    probe.material = b.material(glm::vec3(1.0f, 0.1f, 0.6f), 0.3f, glm::vec3(1.0f, 0.1f, 0.6f), 6000.0f);
    probe.position = glm::vec3(4.75f, 1.2f, 0.4f);
    probe.scale = glm::vec3(0.6f);
    const EntityHandle probeEntity = world.create(probe);
    portals.track(world, probeEntity);
}

void tortureScene(PortalSystem& portals, World& world, const PortalSceneKit& kit) {
    Builder b{portals, world, kit};
    const float hy = 1.6f;
    const float hw = 1.1f;
    const glm::vec3 hall(18.0f, 5.0f, 18.0f);
    const glm::vec3 small(10.0f, 4.5f, 10.0f);
    const glm::vec3 hallCenter(0.0f);
    const glm::vec3 smallCenter(40.0f, 0.0f, 0.0f);
    PortalCell t;
    t.name = "T";
    t.min = hallCenter + glm::vec3(-hall.x * 0.5f - 0.4f, -0.4f, -hall.z * 0.5f - 0.4f);
    t.max = hallCenter + glm::vec3(hall.x * 0.5f + 0.4f, hall.y + 0.4f, hall.z * 0.5f + 0.4f);
    t.params.tint = glm::vec3(0.3f, 0.25f, 0.2f);
    t.params.sun = 0.0f;
    t.params.ambient = 0.08f;
    PortalCell u;
    u.name = "U";
    u.min = smallCenter + glm::vec3(-small.x * 0.5f - 0.4f, -0.4f, -small.z * 0.5f - 0.4f);
    u.max = smallCenter + glm::vec3(small.x * 0.5f + 0.4f, small.y + 0.4f, small.z * 0.5f + 0.4f);
    u.params.tint = glm::vec3(0.1f, 0.35f, 0.2f);
    u.params.sun = 0.0f;
    u.params.ambient = 0.08f;
    u.params.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    portals.addCell(t);
    portals.addCell(u);

    buildRoom(b, "T", hallCenter, hall, {glm::vec3(0.85f, 0.8f, 0.6f), glm::vec3(0.3f, 0.3f, 0.35f), glm::vec3(0.8f, 0.8f, 0.85f), kit.floorShader}, {{0, 0.0f, hw, 2.0f * hy}, {1, 0.0f, hw, 2.0f * hy}, {3, 0.0f, hw, 2.0f * hy}, {2, 0.0f, hw, 2.0f * hy}});
    buildRoom(b, "U", smallCenter, small, {glm::vec3(0.15f, 0.8f, 0.5f), glm::vec3(0.2f, 0.4f, 0.3f), glm::vec3(0.7f, 0.9f, 0.8f), kit.floorShader}, {{1, 0.0f, hw, 2.0f * hy}, {2, 0.0f, hw, 2.0f * hy}});
    b.light("T lamp a", glm::vec3(-4.0f, 4.0f, -4.0f), glm::vec3(1.0f, 0.85f, 0.6f), 12000.0f, 20.0f);
    b.light("T lamp b", glm::vec3(4.0f, 4.0f, 4.0f), glm::vec3(0.7f, 0.8f, 1.0f), 12000.0f, 20.0f);
    b.light("U lamp", smallCenter + glm::vec3(0.0f, 3.6f, 0.0f), glm::vec3(0.6f, 1.0f, 0.7f), 9000.0f, 16.0f);
    props(b, "T north", glm::vec3(-6.0f, 0.0f, -6.0f), glm::vec3(1.0f, 0.3f, 0.2f), 1.2f);
    props(b, "T south", glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(0.2f, 0.5f, 1.0f), 1.2f);
    props(b, "U", smallCenter + glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.2f, 1.0f, 0.5f), 1.2f);

    auto end = [&](const char* cell, const glm::vec3& at, const glm::vec3& euler) {
        PortalEnd e;
        e.cell = cell;
        e.position = at;
        e.eulerDegrees = euler;
        e.halfSize = glm::vec2(hw, hy);
        return e;
    };
    portals.addPair("Hall", end("T", glm::vec3(-9.0f, hy, 0.0f), glm::vec3(90.0f, 0.0f, 0.0f)), end("T", glm::vec3(9.0f, hy, 0.0f), glm::vec3(-90.0f, 0.0f, 0.0f)));
    portals.addPair("Turn", end("T", glm::vec3(0.0f, hy, -9.0f), glm::vec3(0.0f, 0.0f, 0.0f)), end("T", glm::vec3(0.0f, hy, 9.0f), glm::vec3(180.0f, 0.0f, 0.0f)), 90.0f);
    portals.addPair("Window", end("T", glm::vec3(-3.0f, hy, -3.0f), glm::vec3(90.0f, 0.0f, 0.0f)), end("U", glm::vec3(35.0f, hy, 0.0f), glm::vec3(90.0f, 0.0f, 0.0f)));
    portals.addPair("Back", end("U", glm::vec3(40.0f, hy, 5.0f), glm::vec3(180.0f, 0.0f, 0.0f)), end("T", glm::vec3(3.0f, hy, 3.0f), glm::vec3(-135.0f, 0.0f, 0.0f)));
    buildFrames(b, portals, {glm::vec3(0.2f, 0.9f, 1.0f), glm::vec3(1.0f, 0.25f, 0.8f), glm::vec3(1.0f, 0.85f, 0.2f), glm::vec3(0.5f, 1.0f, 0.3f)});
}

}  // namespace

void buildPortalScene(PortalSystem& portals, World& world, const PortalSceneKit& kit, PortalTestScene which) {
    portals.clear();
    if (which == PortalTestScene::Loop) loopScene(portals, world, kit);
    else tortureScene(portals, world, kit);
}

glm::vec3 portalSceneStart(PortalTestScene which, float& yawDegrees, float& pitchDegrees) {
    pitchDegrees = 0.0f;
    if (which == PortalTestScene::Loop) {
        yawDegrees = 0.0f;
        return glm::vec3(-3.2f, 1.7f, 0.0f);
    }
    yawDegrees = 0.0f;
    return glm::vec3(-5.0f, 1.7f, 3.0f);
}
