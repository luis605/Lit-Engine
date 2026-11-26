module;

#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.OrbRun;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Portal;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace orbrun {

namespace {

constexpr float kYawEast = -90.0f;
constexpr float kYawWest = 90.0f;
constexpr float kYawNorth = 0.0f;
constexpr float kYawSouth = 180.0f;
constexpr float kPitchFloor = -90.0f;
constexpr float kPitchCeiling = 90.0f;
constexpr float kRolledDegrees = 45.0f;
constexpr float kRoomSpacing = 40.0f;
constexpr float kOrbHeight = 1.3f;

float wallYaw(Wall wall) {
    switch (wall) {
    case Wall::East: return kYawEast;
    case Wall::West: return kYawWest;
    case Wall::North: return kYawNorth;
    case Wall::South: return kYawSouth;
    }
    return kYawNorth;
}

glm::vec3 wallPosition(const Room& room, Wall wall, float along, float height) {
    const float halfX = room.size.x * 0.5f - kPortalInset;
    const float halfZ = room.size.z * 0.5f - kPortalInset;
    switch (wall) {
    case Wall::East: return room.center + glm::vec3(halfX, height, along);
    case Wall::West: return room.center + glm::vec3(-halfX, height, along);
    case Wall::North: return room.center + glm::vec3(along, height, -halfZ);
    case Wall::South: return room.center + glm::vec3(along, height, halfZ);
    }
    return room.center;
}

PortalEnd makeEnd(const std::string& cell, const glm::vec3& position, const glm::vec3& euler, const glm::vec2& half) {
    PortalEnd end;
    end.cell = cell;
    end.position = position;
    end.eulerDegrees = euler;
    end.halfSize = half;
    return end;
}

PortalEnd wallEnd(const Room& room, Wall wall, float along, float height, const glm::vec2& half, float rollDegrees = 0.0f) {
    return makeEnd(room.name, wallPosition(room, wall, along, height), glm::vec3(wallYaw(wall), 0.0f, rollDegrees), half);
}

PortalEnd floorEnd(const Room& room, float x, float z, const glm::vec2& half) {
    return makeEnd(room.name, room.center + glm::vec3(x, kPortalInset, z), glm::vec3(0.0f, kPitchFloor, 0.0f), half);
}

PortalEnd ceilingEnd(const Room& room, float x, float z, const glm::vec2& half) {
    return makeEnd(room.name, room.center + glm::vec3(x, room.size.y - kPortalInset, z), glm::vec3(0.0f, kPitchCeiling, 0.0f), half);
}

PortalEnd archEnd(const glm::vec3& position, const glm::vec2& half) {
    return makeEnd(kOrbRunCourtyard, position, glm::vec3(kYawSouth, 0.0f, 0.0f), half);
}

}

Room hubRoom() { return {kOrbRunHub, glm::vec3(0.0f), glm::vec3(16.0f, 5.0f, 16.0f)}; }
Room emberRoom() { return {kOrbRunEmber, glm::vec3(kRoomSpacing, 0.0f, 0.0f), glm::vec3(14.0f, 5.0f, 14.0f)}; }
Room frostRoom() { return {kOrbRunFrost, glm::vec3(2.0f * kRoomSpacing, 0.0f, 0.0f), glm::vec3(14.0f, 5.0f, 14.0f)}; }
Room grottoRoom() { return {kOrbRunGrotto, glm::vec3(3.0f * kRoomSpacing, 0.0f, 0.0f), glm::vec3(12.0f, 5.0f, 12.0f)}; }

glm::vec3 interiorMin(const Room& room) { return room.center + glm::vec3(-room.size.x * 0.5f, 0.0f, -room.size.z * 0.5f); }
glm::vec3 interiorMax(const Room& room) { return room.center + glm::vec3(room.size.x * 0.5f, room.size.y, room.size.z * 0.5f); }
glm::vec3 inRoom(const Room& room, float x, float y, float z) { return room.center + glm::vec3(x, y, z); }

PortalLook portalLook(const glm::vec3& accent, PortalShape shape, float frameWidth) {
    PortalLook look;
    look.shape = shape;
    look.cornerRadius = 0.3f;
    look.frame.width = frameWidth;
    look.frame.color = glm::vec3(0.78f, 0.76f, 0.72f);
    look.frame.metalness = 0.6f;
    look.frame.roughness = 0.38f;
    look.frame.emissive = accent;
    look.frame.emissiveIntensity = 2500.0f;
    look.edge.rimColor = accent;
    return look;
}

std::vector<PairSpec> portalPairs() {
    const glm::vec2 door(1.1f, 1.6f);
    const glm::vec2 porthole(1.3f, 1.3f);
    const glm::vec2 grandDoor(2.0f, 2.2f);
    const float grandCenterY = 2.3f;
    const float doorFrame = 0.16f;
    const float portholeFrame = 0.6f;
    const Room hub = hubRoom();
    const Room ember = emberRoom();
    const Room frost = frostRoom();
    const Room grotto = grottoRoom();
    const glm::vec3 courtyardArch(kCourtyardX, grandCenterY, kCourtyardArchZ);
    std::vector<PairSpec> pairs;
    pairs.push_back({kOrbRunDropPortal, wallEnd(hub, Wall::East, 0.0f, kDoorCenterY, door), floorEnd(ember, 0.0f, 3.0f, door), portalLook(glm::vec3(1.0f, 0.5f, 0.15f), PortalShape::Rounded, doorFrame)});
    pairs.push_back({kOrbRunCeilingPortal, wallEnd(ember, Wall::North, 0.0f, kDoorCenterY, door), ceilingEnd(frost, 0.0f, -1.0f, door), portalLook(glm::vec3(0.3f, 0.8f, 1.0f), PortalShape::Rounded, doorFrame)});
    pairs.push_back({kOrbRunRolledPortal, wallEnd(frost, Wall::West, 0.0f, kPortholeCenterY, porthole), wallEnd(hub, Wall::South, 0.0f, kPortholeCenterY, porthole, kRolledDegrees), portalLook(glm::vec3(0.9f, 0.3f, 1.0f), PortalShape::Ellipse, portholeFrame)});
    pairs.push_back({kOrbRunCourtyardPortal, wallEnd(hub, Wall::North, 0.0f, grandCenterY, grandDoor), archEnd(courtyardArch, grandDoor), portalLook(glm::vec3(1.0f, 0.75f, 0.3f), PortalShape::Rounded, doorFrame)});
    pairs.push_back({kOrbRunGrottoPortal, wallEnd(hub, Wall::West, 0.0f, kDoorCenterY, door), wallEnd(grotto, Wall::East, 0.0f, kDoorCenterY, door), portalLook(glm::vec3(0.4f, 1.0f, 0.4f), PortalShape::Rounded, doorFrame)});
    return pairs;
}

}

OrbRunLayout orbRunLayout() {
    using namespace orbrun;
    const Room hub = hubRoom();
    const Room ember = emberRoom();
    const Room frost = frostRoom();
    const Room grotto = grottoRoom();
    const glm::vec3 courtyard(kCourtyardX, 0.0f, 0.0f);
    constexpr float kSpiderGround = 0.0f;
    OrbRunLayout layout;
    layout.spawn.feet = inRoom(hub, 0.0f, 0.05f, 3.0f);
    layout.spawn.yaw = -90.0f;
    layout.spawn.pitch = -3.0f;
    layout.courtyardCenter = courtyard;
    layout.plazaHalf = kPlazaHalf;
    layout.orbs = {
        inRoom(hub, -6.2f, kOrbHeight, -6.2f),
        inRoom(ember, -5.0f, kOrbHeight, -5.5f),
        inRoom(ember, 5.0f, kOrbHeight, 5.5f),
        inRoom(frost, -4.5f, kOrbHeight, -5.5f),
        inRoom(frost, 4.5f, kOrbHeight, 5.5f),
        inRoom(grotto, -4.0f, 2.4f, 0.0f),
        courtyard + glm::vec3(-12.0f, kOrbHeight, 12.0f),
        courtyard + glm::vec3(14.0f, kOrbHeight, -12.0f),
    };
    layout.spiders = {
        {inRoom(ember, 4.0f, kSpiderGround, -4.0f), {inRoom(ember, 4.0f, kSpiderGround, -4.0f), inRoom(ember, -4.0f, kSpiderGround, -4.0f), inRoom(ember, -4.0f, kSpiderGround, 5.5f), inRoom(ember, 4.0f, kSpiderGround, 5.5f)}},
        {inRoom(frost, 4.0f, kSpiderGround, 3.0f), {inRoom(frost, 4.0f, kSpiderGround, -4.0f), inRoom(frost, -4.0f, kSpiderGround, -4.0f), inRoom(frost, -4.0f, kSpiderGround, 5.0f), inRoom(frost, 4.0f, kSpiderGround, 5.0f)}},
        {courtyard + glm::vec3(-5.0f, kSpiderGround, -8.0f), {courtyard + glm::vec3(-12.0f, kSpiderGround, -14.0f), courtyard + glm::vec3(12.0f, kSpiderGround, -14.0f), courtyard + glm::vec3(12.0f, kSpiderGround, 8.0f), courtyard + glm::vec3(-12.0f, kSpiderGround, 8.0f)}},
    };
    layout.crates = {
        inRoom(grotto, 0.5f, 0.4f, -3.5f),
        inRoom(grotto, 1.5f, 0.4f, -3.5f),
        inRoom(grotto, 1.0f, 0.4f, 4.0f),
        courtyard + glm::vec3(4.0f, 0.4f, 14.0f),
        courtyard + glm::vec3(5.0f, 0.4f, 14.0f),
    };
    return layout;
}
