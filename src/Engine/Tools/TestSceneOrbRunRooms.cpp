module;

#include <algorithm>
#include <cmath>
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

constexpr float kPi = 3.14159265f;
constexpr int kSocketCount = 8;
constexpr float kSocketRingRadius = 2.2f;
constexpr float kSocketSize = 0.5f;
constexpr float kSocketHeight = 0.06f;
constexpr float kPillarSize = 0.9f;
constexpr float kBraziersLightIntensity = 32000.0f;
constexpr float kBraziersLightRange = 11.0f;
constexpr float kRoomLampIntensity = 120000.0f;
constexpr float kRoomLampRange = 22.0f;
constexpr float kDimTintBoost = 2.6f;
constexpr float kCoolTintBoost = 2.2f;
constexpr float kDeepTintBoost = 4.6f;
constexpr float kLampHeight = 4.6f;
constexpr float kProbeHalfHeight = 2.5f;

void pillar(const Context& context, const Room& room, const std::string& name, uint32_t surface, float x, float z, float height, float size = kPillarSize) {
    const glm::vec3 base = inRoom(room, x, 0.0f, z);
    context.block(name, surface, base + glm::vec3(-size * 0.5f, 0.0f, -size * 0.5f), base + glm::vec3(size * 0.5f, height, size * 0.5f));
}

void brazier(const Context& context, const Room& room, const std::string& name, uint32_t stone, uint32_t flame, const glm::vec3& color, float x, float z) {
    const glm::vec3 base = inRoom(room, x, 0.0f, z);
    context.block(name + " base", stone, base + glm::vec3(-0.35f, 0.0f, -0.35f), base + glm::vec3(0.35f, 0.9f, 0.35f));
    context.visual(name + " flame", flame, base + glm::vec3(-0.22f, 0.9f, -0.22f), base + glm::vec3(0.22f, 1.35f, 0.22f));
    context.lamp(name + " light", base + glm::vec3(0.0f, 1.6f, 0.0f), color, kBraziersLightIntensity, kBraziersLightRange, 0.25f);
}

void ceilingLamp(const Context& context, const Room& room, const glm::vec3& color, float length, float boost = 1.0f) {
    context.lamp(room.name + " tube", inRoom(room, 0.0f, kLampHeight, 0.0f), color, kRoomLampIntensity * boost, kRoomLampRange, 0.15f, length);
    const uint32_t panel = context.glow(color, 14.0f);
    context.visual(room.name + " lamp panel", panel, inRoom(room, -length * 0.5f, room.size.y - 0.08f, -0.2f), inRoom(room, length * 0.5f, room.size.y, 0.2f));
}

void roomProbe(const Context& context, const Room& room) {
    context.probe(room.name + " probe", inRoom(room, 0.0f, kProbeHalfHeight, 0.0f), glm::vec3(room.size.x * 0.5f, kProbeHalfHeight, room.size.z * 0.5f));
}

void buildSockets(const Context& context, const Room& room) {
    context.game.socketDim = context.paint(glm::vec3(0.08f, 0.07f, 0.06f), 0.4f, 0.8f);
    context.game.socketLit = context.glow(glm::vec3(1.0f, 0.8f, 0.3f), 9.0f);
    context.game.sockets.clear();
    for (int index = 0; index < kSocketCount; ++index) {
        const float angle = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(kSocketCount);
        const glm::vec3 center = inRoom(room, std::cos(angle) * kSocketRingRadius, 0.0f, std::sin(angle) * kSocketRingRadius);
        const glm::vec3 half(kSocketSize * 0.5f, 0.0f, kSocketSize * 0.5f);
        context.game.sockets.push_back(context.visual("orb socket", context.game.socketDim, center - half, center + half + glm::vec3(0.0f, kSocketHeight, 0.0f)));
    }
}

}

void buildHub(const Context& context) {
    const Room room = hubRoom();
    RoomStyle style;
    style.wall = glm::vec3(0.82f, 0.77f, 0.68f);
    style.floor = glm::vec3(0.42f, 0.4f, 0.42f);
    style.floorRoughness = 0.75f;
    style.floorTileMeters = 4.0f;
    style.ceiling = glm::vec3(0.88f, 0.86f, 0.82f);
    buildShell(context, room, style, context.kit.portal.floorShader);
    const uint32_t stone = context.surfaced(Finish::Masonry, glm::vec3(0.72f, 0.68f, 0.64f), 0.75f, {.tileMeters = 1.8f});
    const uint32_t flame = context.glow(glm::vec3(1.0f, 0.6f, 0.25f), 8.0f);
    const float edge = room.size.x * 0.5f - 0.7f;
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            pillar(context, room, "hub pillar", stone, sx * edge, sz * edge, room.size.y);
            brazier(context, room, "hub brazier", stone, flame, glm::vec3(1.0f, 0.62f, 0.3f), sx * (edge - 1.3f), sz * (edge - 1.3f));
        }
    }
    ceilingLamp(context, room, glm::vec3(1.0f, 0.9f, 0.75f), 6.0f);
    buildSockets(context, room);
    const glm::vec3 plinth = inRoom(room, -6.2f, 0.0f, -6.2f);
    context.block("hub orb plinth", stone, plinth + glm::vec3(-0.45f, 0.0f, -0.45f), plinth + glm::vec3(0.45f, 0.85f, 0.45f));
    roomProbe(context, room);
}

void buildEmber(const Context& context) {
    const Room room = emberRoom();
    RoomStyle style;
    style.wall = glm::vec3(0.45f, 0.17f, 0.12f);
    style.floor = glm::vec3(0.24f, 0.18f, 0.16f);
    style.floorRoughness = 0.7f;
    style.floorFinish = Finish::Flagstone;
    style.ceiling = glm::vec3(0.25f, 0.12f, 0.1f);
    buildShell(context, room, style, context.kit.portal.floorShader);
    const uint32_t basalt = context.surfaced(Finish::Rock, glm::vec3(0.3f, 0.26f, 0.25f), 0.7f, {.tileMeters = 1.6f});
    const uint32_t lava = context.glow(glm::vec3(1.0f, 0.35f, 0.08f), 12.0f);
    const glm::vec3 ember(1.0f, 0.42f, 0.12f);
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            pillar(context, room, "ember pillar", basalt, sx * 6.2f, sz * 6.2f, room.size.y, 1.1f);
            brazier(context, room, "ember brazier", basalt, lava, ember, sx * 5.0f, sz * 5.0f);
        }
    }
    for (const float z : {-2.5f, 6.0f}) context.visual("lava channel", lava, inRoom(room, -6.5f, 0.0f, z), inRoom(room, 6.5f, 0.03f, z + 0.5f));
    ceilingLamp(context, room, glm::vec3(1.0f, 0.6f, 0.35f), 5.0f, kDimTintBoost);
    roomProbe(context, room);
}

void buildFrost(const Context& context) {
    const Room room = frostRoom();
    RoomStyle style;
    style.wall = glm::vec3(0.55f, 0.75f, 0.9f);
    style.floor = glm::vec3(0.42f, 0.62f, 0.78f);
    style.floorRoughness = 0.55f;
    style.ceiling = glm::vec3(0.75f, 0.88f, 0.95f);
    buildShell(context, room, style, context.kit.portal.floorShader);
    const uint32_t ice = context.glass(glm::vec3(0.75f, 0.92f, 1.0f));
    const uint32_t crystal = context.glow(glm::vec3(0.35f, 0.75f, 1.0f), 7.0f);
    const uint32_t stone = context.surfaced(Finish::Masonry, glm::vec3(0.5f, 0.6f, 0.72f), 0.55f, {.tileMeters = 1.4f});
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            const glm::vec3 base = inRoom(room, sx * 5.6f, 0.0f, sz * 5.6f);
            context.block("ice column", ice, base + glm::vec3(-0.5f, 0.0f, -0.5f), base + glm::vec3(0.5f, room.size.y, 0.5f));
            context.visual("column crystal", crystal, base + glm::vec3(-0.15f, 0.0f, -0.15f), base + glm::vec3(0.15f, 2.4f, 0.15f));
            context.lamp("frost light", base + glm::vec3(0.0f, 2.2f, 0.0f), glm::vec3(0.45f, 0.75f, 1.0f), kBraziersLightIntensity, kBraziersLightRange, 0.3f);
        }
    }
    context.block("frost plinth", stone, inRoom(room, -1.0f, 0.0f, 3.0f), inRoom(room, 1.0f, 0.5f, 5.0f));
    ceilingLamp(context, room, glm::vec3(0.6f, 0.82f, 1.0f), 5.0f, kCoolTintBoost);
    roomProbe(context, room);
}

void buildGrotto(const Context& context) {
    const Room room = grottoRoom();
    RoomStyle style;
    style.wall = glm::vec3(0.18f, 0.32f, 0.2f);
    style.floor = glm::vec3(0.22f, 0.34f, 0.2f);
    style.floorRoughness = 0.9f;
    style.floorFinish = Finish::Ground;
    style.ceiling = glm::vec3(0.12f, 0.2f, 0.14f);
    buildShell(context, room, style, context.kit.portal.floorShader);
    const uint32_t moss = context.surfaced(Finish::Rock, glm::vec3(0.32f, 0.5f, 0.26f), 0.9f, {.tileMeters = 1.2f});
    const uint32_t cap = context.glow(glm::vec3(0.55f, 0.3f, 1.0f), 8.0f);
    const uint32_t stem = context.surfaced(Finish::Plaster, glm::vec3(0.85f, 0.84f, 0.74f), 0.75f, {.tileMeters = 0.5f});
    const float stepWidth = 3.0f;
    const float stepDepth = 2.0f;
    context.block("grotto step low", moss, inRoom(room, -3.0f, 0.0f, -stepWidth * 0.5f), inRoom(room, -3.0f + stepDepth, 0.7f, stepWidth * 0.5f));
    context.block("grotto step high", moss, inRoom(room, -5.0f, 0.0f, -stepWidth * 0.5f), inRoom(room, -3.0f, 1.4f, stepWidth * 0.5f));
    for (int index = 0; index < 6; ++index) {
        const float x = -2.0f + 1.7f * static_cast<float>(index);
        const float z = (index % 2 == 0 ? 1.0f : -1.0f) * (3.6f + 0.4f * static_cast<float>(index % 3));
        const glm::vec3 base = inRoom(room, x, 0.0f, z);
        context.visual("mushroom stem", stem, base + glm::vec3(-0.07f, 0.0f, -0.07f), base + glm::vec3(0.07f, 0.45f, 0.07f));
        context.ball("mushroom cap", cap, base + glm::vec3(0.0f, 0.5f, 0.0f), 0.28f);
        context.lamp("mushroom light", base + glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.6f, 0.35f, 1.0f), 20000.0f, 6.0f, 0.2f);
    }
    ceilingLamp(context, room, glm::vec3(0.55f, 1.0f, 0.6f), 4.0f, kDeepTintBoost);
    roomProbe(context, room);
}

}
