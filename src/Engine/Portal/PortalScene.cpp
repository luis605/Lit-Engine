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
        const EntityHandle entity = world.create(desc);
        LightComponent component;
        component.type = LightComponent::Type::Point;
        component.color = color;
        component.intensity = intensity;
        component.range = range;
        world.add<LightComponent>(entity, component);
        return entity;
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

void buildRoom(Builder& builder, const std::string& name, const glm::vec3& center, const glm::vec3& size, const RoomStyle& style, const std::vector<Opening>& openings) {
    const float thickness = 0.4f;
    const float halfX = size.x * 0.5f;
    const float halfZ = size.z * 0.5f;
    const float height = size.y;
    const uint32_t wallMat = builder.material(style.wall, 0.7f);
    const uint32_t floorMat = builder.material(style.floor, 0.55f);
    const uint32_t ceilMat = builder.material(style.ceiling, 0.8f);
    builder.box(name + " floor", center + glm::vec3(-halfX - thickness, -thickness, -halfZ - thickness), center + glm::vec3(halfX + thickness, 0.0f, halfZ + thickness), floorMat, style.floorShader);
    builder.box(name + " ceiling", center + glm::vec3(-halfX - thickness, height, -halfZ - thickness), center + glm::vec3(halfX + thickness, height + thickness, halfZ + thickness), ceilMat);
    for (int wall = 0; wall < 4; ++wall) {
        const bool alongX = wall >= 2;
        const float len = alongX ? size.x : size.z;
        const float pos = (wall == 0 ? halfX : wall == 1 ? -halfX : wall == 2 ? halfZ : -halfZ);
        std::vector<std::pair<float, float>> holes;
        float top = 0.0f;
        for (const Opening& opening : openings) {
            if (opening.wall != wall) continue;
            holes.emplace_back(opening.along - opening.halfWidth, opening.along + opening.halfWidth);
            top = opening.top;
        }
        std::ranges::sort(holes);
        auto slab = [&](float a0, float a1, float y0, float y1, const char* tag) {
            if (a1 - a0 < 1.0e-4f || y1 - y0 < 1.0e-4f) return;
            glm::vec3 lo;
            glm::vec3 hi;
            if (alongX) {
                lo = glm::vec3(a0 - thickness, y0, pos - thickness * 0.5f);
                hi = glm::vec3(a1 + thickness, y1, pos + thickness * 0.5f);
            } else {
                lo = glm::vec3(pos - thickness * 0.5f, y0, a0 - thickness);
                hi = glm::vec3(pos + thickness * 0.5f, y1, a1 + thickness);
            }
            if (a0 > -len * 0.5f + 1.0e-3f) {
                if (alongX) lo.x = a0;
                else lo.z = a0;
            }
            if (a1 < len * 0.5f - 1.0e-3f) {
                if (alongX) hi.x = a1;
                else hi.z = a1;
            }
            builder.box(name + " wall " + std::to_string(wall) + tag, center + lo, center + hi, wallMat);
        };
        float cursor = -len * 0.5f;
        for (const auto& [h0, h1] : holes) {
            slab(cursor, h0, 0.0f, height, " part");
            slab(h0, h1, top, height, " lintel");
            cursor = h1;
        }
        slab(cursor, len * 0.5f, 0.0f, height, " part");
    }
}

void buildFrames(Builder& builder, const PortalSystem& portals, const std::vector<glm::vec3>& pairColors) {
    const float bar = 0.22f;
    const float thickness = 0.4f;
    for (size_t index = 0; index < portals.portals().size(); ++index) {
        const Portal& portal = portals.portals()[index];
        const glm::vec3 color = pairColors[(index / 2) % pairColors.size()];
        const uint32_t mat = builder.material(color * 0.3f, 0.4f, color, 9000.0f);
        const glm::mat4& pose = portal.pose;
        const std::array<std::array<glm::vec3, 2>, 3> local = {{{glm::vec3(-portal.half.x - bar, -portal.half.y, 0.0f), glm::vec3(-portal.half.x + 0.02f, portal.half.y + bar, thickness * 0.5f + 0.1f)},
                                                                 {glm::vec3(portal.half.x - 0.02f, -portal.half.y, 0.0f), glm::vec3(portal.half.x + bar, portal.half.y + bar, thickness * 0.5f + 0.1f)},
                                                                 {glm::vec3(-portal.half.x - bar, portal.half.y - 0.02f, 0.0f), glm::vec3(portal.half.x + bar, portal.half.y + bar, thickness * 0.5f + 0.1f)}}};
        for (size_t face = 0; face < local.size(); ++face) {
            glm::vec3 lo(1.0e30f);
            glm::vec3 hi(-1.0e30f);
            for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex) {
                const glm::vec3 corner((cornerIndex & 1) ? local[face][1].x : local[face][0].x, (cornerIndex & 2) ? local[face][1].y : local[face][0].y, (cornerIndex & 4) ? local[face][1].z : local[face][0].z);
                const glm::vec3 worldCorner = glm::vec3(pose * glm::vec4(corner, 1.0f));
                lo = glm::min(lo, worldCorner);
                hi = glm::max(hi, worldCorner);
            }
            builder.box(portal.name + " frame", lo, hi, mat);
        }
    }
}

void props(Builder& builder, const std::string& name, const glm::vec3& center, const glm::vec3& accent, float scale) {
    const uint32_t stone = builder.material(glm::vec3(0.55f, 0.55f, 0.58f), 0.5f);
    const uint32_t glow = builder.material(accent * 0.4f, 0.3f, accent, 4000.0f);
    builder.box(name + " pedestal", center + glm::vec3(-0.5f, 0.0f, -0.5f) * scale, center + glm::vec3(0.5f, 0.9f, 0.5f) * scale, stone);
    builder.box(name + " core", center + glm::vec3(-0.3f, 0.9f, -0.3f) * scale, center + glm::vec3(0.3f, 1.5f, 0.3f) * scale, glow);
}

void loopScene(PortalSystem& portals, World& world, const PortalSceneKit& kit) {
    Builder builder{portals, world, kit};
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
    for (const Room& room : rooms) {
        PortalCell cell;
        cell.name = room.name;
        cell.min = room.center + glm::vec3(-size.x * 0.5f - 0.4f, -0.4f, -size.z * 0.5f - 0.4f);
        cell.max = room.center + glm::vec3(size.x * 0.5f + 0.4f, size.y + 0.4f, size.z * 0.5f + 0.4f);
        cell.params.tint = room.accent * 0.35f;
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
        buildRoom(builder, rooms[i].name, rooms[i].center, size, rooms[i].style, openings[i]);
        props(builder, std::string(rooms[i].name), rooms[i].center + glm::vec3(0.0f, 0.0f, 2.8f), rooms[i].accent, 1.0f);
        builder.light(std::string(rooms[i].name) + " lamp", rooms[i].center + glm::vec3(0.0f, 3.6f, 0.0f), rooms[i].accent * 0.5f + glm::vec3(0.5f), 9000.0f, 16.0f);
    }
    auto end = [&](const char* cell, const glm::vec3& at, float yaw) {
        PortalEnd portalEnd;
        portalEnd.cell = cell;
        portalEnd.position = at;
        portalEnd.eulerDegrees = glm::vec3(yaw, 0.0f, 0.0f);
        portalEnd.halfSize = glm::vec2(hw, hy);
        return portalEnd;
    };
    portals.addPair("AB", end("A", glm::vec3(5.0f, hy, 0.0f), -90.0f), end("B", glm::vec3(19.0f, hy, 0.0f), 90.0f));
    portals.addPair("BC", end("B", glm::vec3(29.0f, hy, 0.0f), -90.0f), end("C", glm::vec3(43.0f, hy, 0.0f), 90.0f));
    portals.addPair("CA", end("C", glm::vec3(53.0f, hy, 0.0f), -90.0f), end("A", glm::vec3(0.0f, hy, -5.0f), 0.0f));
    buildFrames(builder, portals, {glm::vec3(1.0f, 0.85f, 0.2f), glm::vec3(0.2f, 0.9f, 1.0f), glm::vec3(1.0f, 0.25f, 0.8f)});

    EntityDesc probe;
    probe.name = "Probe";
    probe.mesh = kit.sphere;
    probe.material = builder.material(glm::vec3(1.0f, 0.1f, 0.6f), 0.3f, glm::vec3(1.0f, 0.1f, 0.6f), 6000.0f);
    probe.position = glm::vec3(4.75f, 1.2f, 0.4f);
    probe.scale = glm::vec3(0.6f);
    const EntityHandle probeEntity = world.create(probe);
    portals.track(world, probeEntity);
}

void tortureScene(PortalSystem& portals, World& world, const PortalSceneKit& kit) {
    Builder builder{portals, world, kit};
    const float hy = 1.6f;
    const float hw = 1.1f;
    const glm::vec3 hall(18.0f, 5.0f, 18.0f);
    const glm::vec3 small(10.0f, 4.5f, 10.0f);
    const glm::vec3 hallCenter(0.0f);
    const glm::vec3 smallCenter(40.0f, 0.0f, 0.0f);
    PortalCell hallCell;
    hallCell.name = "T";
    hallCell.min = hallCenter + glm::vec3(-hall.x * 0.5f - 0.4f, -0.4f, -hall.z * 0.5f - 0.4f);
    hallCell.max = hallCenter + glm::vec3(hall.x * 0.5f + 0.4f, hall.y + 0.4f, hall.z * 0.5f + 0.4f);
    hallCell.params.tint = glm::vec3(0.3f, 0.25f, 0.2f);
    hallCell.params.sun = 0.0f;
    hallCell.params.ambient = 0.08f;
    PortalCell smallCell;
    smallCell.name = "U";
    smallCell.min = smallCenter + glm::vec3(-small.x * 0.5f - 0.4f, -0.4f, -small.z * 0.5f - 0.4f);
    smallCell.max = smallCenter + glm::vec3(small.x * 0.5f + 0.4f, small.y + 0.4f, small.z * 0.5f + 0.4f);
    smallCell.params.tint = glm::vec3(0.1f, 0.35f, 0.2f);
    smallCell.params.sun = 0.0f;
    smallCell.params.ambient = 0.08f;
    smallCell.params.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    portals.addCell(hallCell);
    portals.addCell(smallCell);

    buildRoom(builder, "T", hallCenter, hall, {glm::vec3(0.85f, 0.8f, 0.6f), glm::vec3(0.3f, 0.3f, 0.35f), glm::vec3(0.8f, 0.8f, 0.85f), kit.floorShader}, {{0, 0.0f, hw, 2.0f * hy}, {1, 0.0f, hw, 2.0f * hy}, {3, 0.0f, hw, 2.0f * hy}, {2, 0.0f, hw, 2.0f * hy}});
    buildRoom(builder, "U", smallCenter, small, {glm::vec3(0.15f, 0.8f, 0.5f), glm::vec3(0.2f, 0.4f, 0.3f), glm::vec3(0.7f, 0.9f, 0.8f), kit.floorShader}, {{1, 0.0f, hw, 2.0f * hy}, {2, 0.0f, hw, 2.0f * hy}});
    builder.light("T lamp a", glm::vec3(-4.0f, 4.0f, -4.0f), glm::vec3(1.0f, 0.85f, 0.6f), 12000.0f, 20.0f);
    builder.light("T lamp b", glm::vec3(4.0f, 4.0f, 4.0f), glm::vec3(0.7f, 0.8f, 1.0f), 12000.0f, 20.0f);
    builder.light("U lamp", smallCenter + glm::vec3(0.0f, 3.6f, 0.0f), glm::vec3(0.6f, 1.0f, 0.7f), 9000.0f, 16.0f);
    props(builder, "T north", glm::vec3(-6.0f, 0.0f, -6.0f), glm::vec3(1.0f, 0.3f, 0.2f), 1.2f);
    props(builder, "T south", glm::vec3(6.0f, 0.0f, 6.0f), glm::vec3(0.2f, 0.5f, 1.0f), 1.2f);
    props(builder, "U", smallCenter + glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.2f, 1.0f, 0.5f), 1.2f);

    auto end = [&](const char* cell, const glm::vec3& at, const glm::vec3& euler) {
        PortalEnd portalEnd;
        portalEnd.cell = cell;
        portalEnd.position = at;
        portalEnd.eulerDegrees = euler;
        portalEnd.halfSize = glm::vec2(hw, hy);
        return portalEnd;
    };
    portals.addPair("Hall", end("T", glm::vec3(-9.0f, hy, 0.0f), glm::vec3(90.0f, 0.0f, 0.0f)), end("T", glm::vec3(9.0f, hy, 0.0f), glm::vec3(-90.0f, 0.0f, 0.0f)));
    portals.addPair("Turn", end("T", glm::vec3(0.0f, hy, -9.0f), glm::vec3(0.0f, 0.0f, 0.0f)), end("T", glm::vec3(0.0f, hy, 9.0f), glm::vec3(180.0f, 0.0f, 0.0f)), 90.0f);
    portals.addPair("Window", end("T", glm::vec3(-3.0f, hy, -3.0f), glm::vec3(90.0f, 0.0f, 0.0f)), end("U", glm::vec3(35.0f, hy, 0.0f), glm::vec3(90.0f, 0.0f, 0.0f)));
    portals.addPair("Back", end("U", glm::vec3(40.0f, hy, 5.0f), glm::vec3(180.0f, 0.0f, 0.0f)), end("T", glm::vec3(3.0f, hy, 3.0f), glm::vec3(-135.0f, 0.0f, 0.0f)));
    buildFrames(builder, portals, {glm::vec3(0.2f, 0.9f, 1.0f), glm::vec3(1.0f, 0.25f, 0.8f), glm::vec3(1.0f, 0.85f, 0.2f), glm::vec3(0.5f, 1.0f, 0.3f)});
}

}

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
