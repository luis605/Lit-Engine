module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

module Engine.Portal;

import Engine.glm;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Portal.view;

#include "PortalSceneInternal.hpp"

namespace {

using namespace portal_scene;

constexpr float kSill = 0.03f;


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
    // Openings sit a sill above the floor so the frame reveal never shares the floor's plane.
    const float y = hy + kSill;
    portals.addPair("AB", end("A", glm::vec3(5.0f, y, 0.0f), -90.0f), end("B", glm::vec3(19.0f, y, 0.0f), 90.0f), 0.0f, accentLook(glm::vec3(1.0f, 0.85f, 0.2f), PortalShape::Rounded));
    portals.addPair("BC", end("B", glm::vec3(29.0f, y, 0.0f), -90.0f), end("C", glm::vec3(43.0f, y, 0.0f), 90.0f), 0.0f, accentLook(glm::vec3(0.2f, 0.9f, 1.0f), PortalShape::Ellipse));
    portals.addPair("CA", end("C", glm::vec3(53.0f, y, 0.0f), -90.0f), end("A", glm::vec3(0.0f, y, -5.0f), 0.0f), 0.0f, accentLook(glm::vec3(1.0f, 0.25f, 0.8f)));
    buildPortalFrames(portals, world, kit);

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
    const float y = hy + kSill;
    portals.addPair("Hall", end("T", glm::vec3(-9.0f, y, 0.0f), glm::vec3(90.0f, 0.0f, 0.0f)), end("T", glm::vec3(9.0f, y, 0.0f), glm::vec3(-90.0f, 0.0f, 0.0f)), 0.0f, accentLook(glm::vec3(0.2f, 0.9f, 1.0f)));
    portals.addPair("Turn", end("T", glm::vec3(0.0f, y, -9.0f), glm::vec3(0.0f, 0.0f, 0.0f)), end("T", glm::vec3(0.0f, y, 9.0f), glm::vec3(180.0f, 0.0f, 0.0f)), 90.0f, accentLook(glm::vec3(1.0f, 0.25f, 0.8f), PortalShape::Rounded));
    portals.addPair("Window", end("T", glm::vec3(-3.0f, y, -3.0f), glm::vec3(90.0f, 0.0f, 0.0f)), end("U", glm::vec3(35.0f, y, 0.0f), glm::vec3(90.0f, 0.0f, 0.0f)), 0.0f, accentLook(glm::vec3(1.0f, 0.85f, 0.2f), PortalShape::Ellipse));
    portals.addPair("Back", end("U", glm::vec3(40.0f, y, 5.0f), glm::vec3(180.0f, 0.0f, 0.0f)), end("T", glm::vec3(3.0f, y, 3.0f), glm::vec3(-135.0f, 0.0f, 0.0f)), 0.0f, accentLook(glm::vec3(0.5f, 1.0f, 0.3f)));
    buildPortalFrames(portals, world, kit);
}

}

void buildPortalFrames(const PortalSystem& portals, World& world, const PortalSceneKit& kit) {
    for (const Portal& portal : portals.portals()) {
        const PortalLook& look = portal.look;
        const PortalFrameStyle& style = look.frame;
        if (!style.enabled || style.width <= 0.0f) continue;
        auto materialOf = [&](const glm::vec3& color, const glm::vec3& emissive, float intensity) -> uint32_t {
            if (kit.metalMaterial) return kit.metalMaterial(color, style.metalness, style.roughness, emissive, intensity);
            return kit.material ? kit.material(color, emissive, intensity, style.roughness) : 0u;
        };
        const uint32_t bodyMaterial = materialOf(style.color, glm::vec3(0.0f), 0.0f);
        const bool glow = style.emissiveIntensity > 0.0f && glm::dot(style.emissive, style.emissive) > 0.0f;
        const uint32_t revealMaterial = glow ? materialOf(style.color * 0.5f, style.emissive, style.emissiveIntensity) : bodyMaterial;
        const glm::quat rotation = glm::quat_cast(glm::mat3(portal.pose));
        if (kit.mesh) {
            const std::string key = portalFrameKey(portal.half, look);
            PortalFrameMesh body;
            PortalFrameMesh reveal;
            buildPortalFrameMesh(portal.half, look, body, reveal);
            for (const auto& [suffix, mesh, material] : {std::tuple<const char*, const PortalFrameMesh*, uint32_t>{" frame", &body, bodyMaterial}, {" frame reveal", &reveal, revealMaterial}}) {
                EntityDesc desc;
                desc.name = portal.name + suffix;
                desc.mesh = kit.mesh(key + suffix, mesh->vertices, mesh->indices);
                desc.material = material;
                desc.position = portal.center();
                desc.rotation = rotation;
                world.create(desc);
            }
            continue;
        }
        // No procedural meshes: four bars of the unit cube, oriented with the portal.
        const glm::vec2 h = portal.half;
        const float w = style.width;
        const std::array<std::array<glm::vec3, 2>, 4> bars = {{{glm::vec3(-h.x - w, -h.y - w, -style.back), glm::vec3(-h.x, h.y + w, style.depth)},
                                                                {glm::vec3(h.x, -h.y - w, -style.back), glm::vec3(h.x + w, h.y + w, style.depth)},
                                                                {glm::vec3(-h.x, h.y, -style.back), glm::vec3(h.x, h.y + w, style.depth)},
                                                                {glm::vec3(-h.x, -h.y - w, -style.back), glm::vec3(h.x, -h.y, style.depth)}}};
        for (const auto& bar : bars) {
            EntityDesc desc;
            desc.name = portal.name + " frame";
            desc.mesh = kit.cube;
            desc.material = bodyMaterial;
            desc.position = glm::vec3(portal.pose * glm::vec4(bar[0], 1.0f));
            desc.rotation = rotation;
            desc.scale = bar[1] - bar[0];
            world.create(desc);
        }
    }
}

void buildPortalScene(PortalSystem& portals, World& world, const PortalSceneKit& kit, PortalTestScene which) {
    portals.clear();
    if (which == PortalTestScene::Loop) loopScene(portals, world, kit);
    else if (which == PortalTestScene::Torture) tortureScene(portals, world, kit);
    else tiltScene(portals, world, kit);
}

glm::vec3 portalSceneStart(PortalTestScene which, float& yawDegrees, float& pitchDegrees) {
    pitchDegrees = 0.0f;
    if (which == PortalTestScene::Tilt) {
        yawDegrees = -60.0f;
        pitchDegrees = 8.0f;
        return glm::vec3(-2.5f, 1.7f, 4.5f);
    }
    if (which == PortalTestScene::Loop) {
        yawDegrees = 0.0f;
        return glm::vec3(-3.2f, 1.7f, 0.0f);
    }
    yawDegrees = 0.0f;
    return glm::vec3(-5.0f, 1.7f, 3.0f);
}
