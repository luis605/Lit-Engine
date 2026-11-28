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
constexpr int kBowlSides = 20;
constexpr int kFlameTongues = 3;
constexpr float kLavaEmission = 2800.0f;
constexpr float kMushroomEmission = 260.0f;
constexpr float kBrightShardEmission = 260.0f;
constexpr float kDullShardEmission = 35.0f;
constexpr int kCrystalSides = 6;
constexpr int kClusterShards = 7;
constexpr int kIcicles = 70;
constexpr int kSnowDrifts = 22;
constexpr int kStalactites = 80;
constexpr int kGrottoBoulders = 26;
constexpr int kGrottoTufts = 260;
constexpr int kBasaltColumns = 9;
constexpr float kCeilingBand = 2.2f;
constexpr float kGrassPatchMeters = 7.0f;

float spread(uint32_t seed, float low, float high) { return low + (high - low) * scatterUnit(seed); }

glm::quat turned(float yaw) { return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)); }

glm::vec3 alongWalls(const Room& room, uint32_t seed, float inset, float depth) {
    const int wall = static_cast<int>(scatterUnit(seed) * 4.0f) % 4;
    const float along = spread(seed + 1u, -0.47f, 0.47f);
    const float back = inset + depth * scatterUnit(seed + 2u);
    const float halfX = room.size.x * 0.5f - back;
    const float halfZ = room.size.z * 0.5f - back;
    if (wall == 0) return inRoom(room, along * room.size.x, 0.0f, -halfZ);
    if (wall == 1) return inRoom(room, along * room.size.x, 0.0f, halfZ);
    if (wall == 2) return inRoom(room, -halfX, 0.0f, along * room.size.z);
    return inRoom(room, halfX, 0.0f, along * room.size.z);
}

bool facesDoorway(const Room& room, const glm::vec3& position, float clearance) {
    for (const PairSpec& pair : portalPairs()) {
        for (const PortalEnd* end : {&pair.a, &pair.b}) {
            if (end->cell != room.name) continue;
            const glm::vec2 offset(position.x - end->position.x, position.z - end->position.z);
            if (glm::length(offset) < std::max(end->halfSize.x, end->halfSize.y) + clearance) return true;
        }
    }
    return false;
}

void crystalCluster(const Context& context, uint32_t shape, uint32_t surface, const glm::vec3& base, float size, uint32_t seed) {
    for (int shard = 0; shard < kClusterShards; ++shard) {
        const uint32_t local = seed * 17u + static_cast<uint32_t>(shard) * 5u;
        const float angle = 2.0f * kPi * static_cast<float>(shard) / static_cast<float>(kClusterShards) + scatterUnit(local);
        const float lean = shard == 0 ? 0.05f : spread(local + 1u, 0.25f, 0.75f);
        const float height = size * (shard == 0 ? 1.0f : spread(local + 2u, 0.35f, 0.7f));
        const glm::vec3 outward(std::cos(angle), 0.0f, std::sin(angle));
        const glm::quat tilt = glm::angleAxis(lean, glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), outward)));
        context.prop("crystal", surface, shape, base + outward * (shard == 0 ? 0.0f : size * 0.16f), glm::vec3(height), tilt);
    }
}

uint32_t crystalMesh(const Context& context) {
    MeshData data;
    appendCrystal(data, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.13f, 1.0f, kCrystalSides, 0.28f);
    return context.mesh("crystal", data);
}

void placeChunks(const Context& context, const glm::vec3& origin, const std::string& name, const std::string& key, uint32_t surface, const std::vector<MeshData>& chunks, uint32_t shader = 0) {
    for (size_t chunk = 0; chunk < chunks.size(); ++chunk) context.prop(name, surface, context.mesh(key + "." + name + "." + std::to_string(chunk), chunks[chunk]), origin, glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), shader);
}

void hangSpikes(const Context& context, const Room& room, uint32_t surface, int count, float shortest, float longest, uint32_t seed) {
    MeshData spike;
    appendLathe(spike, {{0.16f, 0.0f}, {0.11f, -0.28f}, {0.075f, -0.5f}, {0.05f, -0.7f}, {0.001f, -1.0f}}, 9, 0.16f);
    std::vector<MeshData> merged;
    for (int index = 0; index < count; ++index) {
        const uint32_t local = seed + static_cast<uint32_t>(index) * 13u;
        glm::vec3 position = alongWalls(room, local, 0.15f, kCeilingBand);
        if (facesDoorway(room, position, 0.4f)) continue;
        position.y = room.center.y + room.size.y;
        const float length = spread(local + 3u, shortest, longest) * (scatterUnit(local + 4u) > 0.85f ? 1.7f : 1.0f);
        const float girth = spread(local + 5u, 0.45f, 1.1f);
        appendChunked(merged, spike, position - room.center, turned(spread(local + 6u, 0.0f, 2.0f * kPi)), glm::vec3(girth, length, girth));
    }
    placeChunks(context, room.center, "ceiling spikes", room.name, surface, merged);
}

}

void buildBrazierBowl(const Context& context, const glm::vec3& top, uint32_t flame, float scale) {
    MeshData bowl;
    appendLathe(bowl, {{0.14f, 0.0f}, {0.24f, 0.03f}, {0.36f, 0.11f}, {0.42f, 0.21f}, {0.4f, 0.24f}, {0.35f, 0.21f}, {0.26f, 0.15f}, {0.02f, 0.12f}}, kBowlSides, 0.0f);
    const uint32_t iron = context.paint(glm::vec3(0.09f, 0.075f, 0.065f), 0.38f, 0.9f);
    context.prop("brazier bowl", iron, context.mesh("bowl", bowl), top, glm::vec3(scale));
    const uint32_t tongue = context.mesh("flame", flameMesh());
    for (int index = 0; index < kFlameTongues; ++index) {
        const float angle = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(kFlameTongues);
        const float offset = index == 0 ? 0.0f : 0.13f;
        const float height = (index == 0 ? 0.46f : 0.3f) * scale;
        const glm::vec3 position = top + glm::vec3(std::cos(angle) * offset, 0.12f, std::sin(angle) * offset) * scale;
        context.prop("flame", flame, tongue, position, glm::vec3((index == 0 ? 0.9f : 0.65f) * scale, height, (index == 0 ? 0.9f : 0.65f) * scale), turned(angle), context.looks.flame);
    }
}

void buildPillarTrim(const Context& context, uint32_t stone, const glm::vec3& base, float size, float height) {
    const float foot = size * 0.66f;
    const float head = size * 0.62f;
    context.rounded("pillar foot", stone, base + glm::vec3(-foot, 0.0f, -foot), base + glm::vec3(foot, 0.34f, foot), 0.08f);
    context.rounded("pillar collar", stone, base + glm::vec3(-head * 0.9f, 0.34f, -head * 0.9f), base + glm::vec3(head * 0.9f, 0.46f, head * 0.9f), 0.05f);
    context.rounded("pillar head", stone, base + glm::vec3(-head, height - 0.4f, -head), base + glm::vec3(head, height, head), 0.08f);
}

void decorateEmber(const Context& context, const Room& room) {
    const uint32_t lava = context.glow(glm::vec3(1.0f, 0.42f, 0.1f), kLavaEmission);
    const uint32_t channel = context.mesh("molten", moltenStripMesh(13.0f, 0.9f));
    const uint32_t curb = context.surfaced(Finish::Rock, glm::vec3(0.2f, 0.16f, 0.15f), 0.75f, {.tileMeters = 0.9f});
    for (const float z : {-2.25f, 6.25f}) {
        context.prop("lava channel", lava, channel, inRoom(room, 0.0f, 0.025f, z), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), context.looks.molten);
        for (const float side : {-0.52f, 0.52f}) context.rounded("lava curb", curb, inRoom(room, -6.55f, 0.0f, z + side - 0.09f), inRoom(room, 6.55f, 0.07f, z + side + 0.09f), 0.035f, 0.016f);
    }
    MeshData column;
    appendCrystal(column, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.34f, 1.0f, kCrystalSides, 0.04f);
    std::vector<MeshData> columns;
    for (int cluster = 0; cluster < kBasaltColumns * 6; ++cluster) {
        const uint32_t seed = 900u + static_cast<uint32_t>(cluster) * 7u;
        const glm::vec3 position = alongWalls(room, seed, 0.3f, 0.7f);
        if (facesDoorway(room, position, 0.9f)) continue;
        const float height = spread(seed + 3u, 0.25f, 1.5f);
        appendChunked(columns, column, position - room.center, turned(spread(seed + 4u, 0.0f, 1.0f)), glm::vec3(1.0f, height, 1.0f));
    }
    placeChunks(context, room.center, "basalt columns", room.name, curb, columns);
}

void decorateFrost(const Context& context, const Room& room) {
    Material shard;
    shard.name = "Orb Run Crystal";
    shard.baseColor = glm::vec3(0.55f, 0.82f, 1.0f);
    shard.roughness = 0.12f;
    shard.clearcoat = 1.0f;
    shard.emissive = glm::vec3(0.2f, 0.6f, 1.0f);
    shard.emissiveIntensity = kBrightShardEmission;
    const uint32_t bright = context.material(shard);
    shard.emissiveIntensity = kDullShardEmission;
    const uint32_t dull = context.material(shard);
    const uint32_t shape = crystalMesh(context);
    uint32_t seed = 40u;
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            crystalCluster(context, shape, bright, inRoom(room, sx * 4.9f, 0.0f, sz * 5.6f), 1.5f, ++seed);
            crystalCluster(context, shape, dull, inRoom(room, sx * 5.9f, 0.0f, sz * 4.7f), 0.9f, ++seed);
        }
    }
    hangSpikes(context, room, dull, kIcicles, 0.25f, 0.75f, 3000u);
    const uint32_t snow = context.surfaced(Finish::Plaster, glm::vec3(0.86f, 0.92f, 0.98f), 0.65f, {.tileMeters = 0.8f});
    const MeshData drift = boulderMesh();
    std::vector<MeshData> drifts;
    for (int index = 0; index < kSnowDrifts; ++index) {
        const uint32_t local = 5000u + static_cast<uint32_t>(index) * 11u;
        const glm::vec3 position = alongWalls(room, local, 0.1f, 0.35f);
        if (facesDoorway(room, position, 0.8f)) continue;
        appendChunked(drifts, drift, position - room.center - glm::vec3(0.0f, 0.16f, 0.0f), turned(spread(local + 6u, 0.0f, 2.0f * kPi)), glm::vec3(spread(local + 3u, 0.5f, 1.0f), spread(local + 4u, 0.2f, 0.4f), spread(local + 5u, 0.4f, 0.8f)));
    }
    placeChunks(context, room.center, "snow drifts", room.name, snow, drifts);
}

void decorateGrotto(const Context& context, const Room& room) {
    const uint32_t wetRock = context.surfaced(Finish::Rock, glm::vec3(0.22f, 0.3f, 0.2f), 0.55f, {.tileMeters = 0.9f});
    const uint32_t moss = context.surfaced(Finish::Rock, glm::vec3(0.3f, 0.46f, 0.22f), 0.95f, {.tileMeters = 0.8f});
    hangSpikes(context, room, wetRock, kStalactites, 0.3f, 1.0f, 7000u);
    const MeshData boulder = boulderMesh();
    std::vector<MeshData> wetBoulders;
    std::vector<MeshData> mossBoulders;
    for (int index = 0; index < kGrottoBoulders; ++index) {
        const uint32_t seed = 8000u + static_cast<uint32_t>(index) * 9u;
        const glm::vec3 position = alongWalls(room, seed, 0.15f, 0.5f);
        if (facesDoorway(room, position, 1.1f)) continue;
        const float size = spread(seed + 3u, 0.35f, 0.95f);
        appendChunked(index % 3 == 0 ? wetBoulders : mossBoulders, boulder, position - room.center - glm::vec3(0.0f, size * 0.25f, 0.0f), turned(spread(seed + 5u, 0.0f, 2.0f * kPi)), glm::vec3(size, size * spread(seed + 4u, 0.6f, 1.3f), size));
    }
    placeChunks(context, room.center, "wet boulders", room.name, wetRock, wetBoulders);
    placeChunks(context, room.center, "moss boulders", room.name, moss, mossBoulders);
    MeshData stalk;
    appendLathe(stalk, {{0.1f, 0.0f}, {0.07f, 0.14f}, {0.055f, 0.34f}, {0.065f, 0.46f}}, 12, 0.06f);
    MeshData dome;
    appendLathe(dome, {{0.05f, 0.0f}, {0.24f, 0.012f}, {0.32f, 0.05f}, {0.29f, 0.12f}, {0.2f, 0.18f}, {0.09f, 0.215f}, {0.001f, 0.225f}}, 18, 0.04f);
    const uint32_t stemShape = context.mesh("mushroom.stem", stalk);
    const uint32_t capShape = context.mesh("mushroom.cap", dome);
    const uint32_t stem = context.surfaced(Finish::Plaster, glm::vec3(0.85f, 0.84f, 0.74f), 0.75f, {.tileMeters = 0.5f});
    const uint32_t cap = context.glow(glm::vec3(0.55f, 0.3f, 1.0f), kMushroomEmission);
    for (int index = 0; index < 6; ++index) {
        const float x = -2.0f + 1.7f * static_cast<float>(index);
        const float z = (index % 2 == 0 ? 1.0f : -1.0f) * (3.6f + 0.4f * static_cast<float>(index % 3));
        const glm::vec3 base = inRoom(room, x, 0.0f, z);
        for (int sprout = 0; sprout < 4; ++sprout) {
            const uint32_t seed = 9000u + static_cast<uint32_t>(index * 4 + sprout) * 7u;
            const float size = sprout == 0 ? 1.15f : spread(seed, 0.35f, 0.65f);
            const glm::vec3 offset = sprout == 0 ? glm::vec3(0.0f) : glm::vec3(spread(seed + 1u, -0.5f, 0.5f), 0.0f, spread(seed + 2u, -0.5f, 0.5f));
            context.prop("mushroom stem", stem, stemShape, base + offset, glm::vec3(size));
            context.prop("mushroom cap", cap, capShape, base + offset + glm::vec3(0.0f, 0.44f * size, 0.0f), glm::vec3(size), turned(spread(seed + 3u, 0.0f, 2.0f * kPi)), context.looks.glowcap);
        }
        context.lamp("mushroom light", base + glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(0.6f, 0.35f, 1.0f), 20000.0f, 6.0f, 0.2f);
    }
    scatterGrass(context, room.center, glm::vec2(room.size.x, room.size.z) * 0.48f, kGrottoTufts, 0.0f);
}

void scatterGrass(const Context& context, const glm::vec3& center, const glm::vec2& half, int count, float clearHalf) {
    const MeshData tuft = grassTuftMesh();
    const int columns = std::max(1, static_cast<int>(std::ceil(2.0f * half.x / kGrassPatchMeters)));
    const int rows = std::max(1, static_cast<int>(std::ceil(2.0f * half.y / kGrassPatchMeters)));
    std::vector<std::vector<MeshData>> patches(static_cast<size_t>(columns * rows));
    for (int index = 0; index < count; ++index) {
        const uint32_t seed = 12000u + static_cast<uint32_t>(index) * 5u + static_cast<uint32_t>(center.x);
        const glm::vec3 offset(spread(seed, -half.x, half.x), 0.0f, spread(seed + 1u, -half.y, half.y));
        if (std::abs(offset.x) < clearHalf && std::abs(offset.z) < clearHalf) continue;
        const int column = std::clamp(static_cast<int>((offset.x + half.x) / kGrassPatchMeters), 0, columns - 1);
        const int row = std::clamp(static_cast<int>((offset.z + half.y) / kGrassPatchMeters), 0, rows - 1);
        appendChunked(patches[static_cast<size_t>(row * columns + column)], tuft, offset, turned(spread(seed + 3u, 0.0f, 2.0f * kPi)), glm::vec3(spread(seed + 2u, 0.45f, 0.95f)));
    }
    Material material;
    material.name = "Orb Run Plaza Grass";
    material.baseColor = glm::vec3(1.0f);
    material.roughness = 0.85f;
    const uint32_t surface = context.material(material);
    for (size_t patch = 0; patch < patches.size(); ++patch) {
        const std::string key = std::to_string(static_cast<int>(center.x)) + "." + std::to_string(patch);
        placeChunks(context, center, "grass patch", key, surface, patches[patch], context.looks.grass);
    }
}

}
