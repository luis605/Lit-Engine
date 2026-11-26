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

#include "PortalSceneInternal.hpp"

void portal_scene::tiltScene(PortalSystem& portals, World& world, const PortalSceneKit& kit) {
    Builder builder{portals, world, kit};
    constexpr float kSurfaceOffset = 0.01f;
    constexpr float kWallHalfThickness = 0.2f;
    const glm::vec3 hubSize(14.0f, 5.0f, 14.0f);
    const glm::vec3 roomSize(10.0f, 5.0f, 10.0f);
    struct Room {
        const char* name;
        glm::vec3 center;
        glm::vec3 size;
        RoomStyle style;
        glm::vec3 accent;
    };
    const std::array<Room, 5> rooms = {{
        {"H", glm::vec3(0.0f), hubSize, {glm::vec3(0.8f, 0.78f, 0.72f), glm::vec3(0.32f, 0.3f, 0.3f), glm::vec3(0.85f, 0.85f, 0.88f), kit.floorShader}, glm::vec3(1.0f, 0.9f, 0.7f)},
        {"F", glm::vec3(40.0f, 0.0f, 0.0f), roomSize, {glm::vec3(0.95f, 0.5f, 0.15f), glm::vec3(0.45f, 0.3f, 0.2f), glm::vec3(0.9f, 0.8f, 0.7f), kit.floorShader}, glm::vec3(1.0f, 0.55f, 0.15f)},
        {"C", glm::vec3(80.0f, 0.0f, 0.0f), roomSize, {glm::vec3(0.15f, 0.7f, 0.9f), glm::vec3(0.2f, 0.35f, 0.45f), glm::vec3(0.7f, 0.85f, 0.95f), kit.floorShader}, glm::vec3(0.2f, 0.8f, 1.0f)},
        {"R", glm::vec3(120.0f, 0.0f, 0.0f), roomSize, {glm::vec3(0.85f, 0.2f, 0.65f), glm::vec3(0.4f, 0.2f, 0.35f), glm::vec3(0.9f, 0.75f, 0.85f), kit.floorShader}, glm::vec3(1.0f, 0.25f, 0.8f)},
        {"U", glm::vec3(160.0f, 0.0f, 0.0f), roomSize, {glm::vec3(0.45f, 0.85f, 0.2f), glm::vec3(0.25f, 0.4f, 0.2f), glm::vec3(0.8f, 0.9f, 0.75f), kit.floorShader}, glm::vec3(0.5f, 1.0f, 0.3f)},
    }};
    for (const Room& room : rooms) {
        PortalCell cell;
        cell.name = room.name;
        cell.min = room.center + glm::vec3(-room.size.x * 0.5f - 0.4f, -0.4f, -room.size.z * 0.5f - 0.4f);
        cell.max = room.center + glm::vec3(room.size.x * 0.5f + 0.4f, room.size.y + 0.4f, room.size.z * 0.5f + 0.4f);
        cell.params.tint = room.accent * 0.35f;
        cell.params.sun = 0.0f;
        cell.params.ambient = 0.07f;
        cell.params.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
        portals.addCell(cell);
        buildRoom(builder, room.name, room.center, room.size, room.style, {});
        const std::string name(room.name);
        builder.light(name + " lamp", room.center + glm::vec3(0.0f, room.size.y - 1.0f, 0.0f), room.accent * 0.5f + glm::vec3(0.5f), 11000.0f, 18.0f);
        props(builder, name + " a", room.center + glm::vec3(-2.6f, 0.0f, 2.4f), room.accent, 1.0f);
        props(builder, name + " b", room.center + glm::vec3(2.4f, 0.0f, -2.6f), glm::vec3(1.0f) - room.accent * 0.6f, 0.8f);
    }
    const uint32_t arrow = builder.material(glm::vec3(0.95f), 0.5f, glm::vec3(1.0f), 1500.0f);
    for (const Room& room : rooms) {
        builder.box(std::string(room.name) + " arrow", room.center + glm::vec3(-3.4f, 0.0f, -3.2f), room.center + glm::vec3(-1.4f, 0.02f, -3.0f), arrow);
        builder.box(std::string(room.name) + " arrow tip", room.center + glm::vec3(-1.6f, 0.0f, -3.45f), room.center + glm::vec3(-1.4f, 0.02f, -2.75f), arrow);
    }

    auto end = [](const char* cell, const glm::vec3& position, const glm::vec3& euler, const glm::vec2& half) {
        PortalEnd portalEnd;
        portalEnd.cell = cell;
        portalEnd.position = position;
        portalEnd.eulerDegrees = euler;
        portalEnd.halfSize = half;
        return portalEnd;
    };
    const glm::vec2 door(1.1f, 1.6f);
    const float doorY = door.y + 0.05f;
    const float hubWall = hubSize.x * 0.5f - kWallHalfThickness - kSurfaceOffset;
    const float roomWall = roomSize.x * 0.5f - kWallHalfThickness - kSurfaceOffset;
    portals.addPair("floor", end("H", glm::vec3(hubWall, doorY, 0.0f), glm::vec3(-90.0f, 0.0f, 0.0f), door), end("F", rooms[1].center + glm::vec3(0.0f, kSurfaceOffset, 0.0f), glm::vec3(0.0f, -90.0f, 0.0f), door), 0.0f,
                    accentLook(rooms[1].accent, PortalShape::Rounded));
    portals.addPair("ceiling", end("H", glm::vec3(-3.0f, hubSize.y - kSurfaceOffset, -2.5f), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec2(1.3f)), end("C", rooms[2].center + glm::vec3(0.0f, kSurfaceOffset, 0.0f), glm::vec3(0.0f, -90.0f, 0.0f), glm::vec2(1.3f)), 0.0f,
                    accentLook(rooms[2].accent, PortalShape::Ellipse));
    portals.addPair("rolled", end("H", glm::vec3(0.0f, doorY, -hubWall), glm::vec3(0.0f, 0.0f, 0.0f), door), end("R", rooms[3].center + glm::vec3(0.0f, 2.4f, roomWall), glm::vec3(180.0f, 0.0f, 45.0f), door), 0.0f, accentLook(rooms[3].accent));
    portals.addPair("flip", end("H", glm::vec3(-hubWall, doorY, 0.0f), glm::vec3(90.0f, 0.0f, 0.0f), door), end("U", rooms[4].center + glm::vec3(roomWall, 2.4f, 0.0f), glm::vec3(-90.0f, 0.0f, 180.0f), door), 0.0f,
                    accentLook(rooms[4].accent, PortalShape::Rounded));
    buildPortalFrames(portals, world, kit);

    EntityDesc probe;
    probe.name = "Probe";
    probe.mesh = kit.sphere;
    probe.material = builder.material(glm::vec3(1.0f, 0.1f, 0.6f), 0.3f, glm::vec3(1.0f, 0.1f, 0.6f), 6000.0f);
    probe.position = glm::vec3(hubWall - 0.25f, 0.7f, -0.8f);
    probe.scale = glm::vec3(0.6f);
    portals.track(world, world.create(probe));
}
