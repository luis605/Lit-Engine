module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.IkTestScene;

import Engine.glm;
import Engine.World;

namespace {

constexpr float kGroundHalfSize = 36.0f;
constexpr float kGroundDepth = 1.0f;
constexpr float kHillCell = 0.8f;
constexpr float kHeightQuantum = 0.1f;

struct TerrainPaints {
    uint32_t grass = 0;
    uint32_t stone = 0;
    uint32_t sand = 0;
    uint32_t moss = 0;
};

TerrainPaints makePaints(const IkTestKit& kit) {
    TerrainPaints paints;
    paints.grass = ikTestPaint(kit, glm::vec3(0.28f, 0.36f, 0.2f), 0.95f);
    paints.stone = ikTestPaint(kit, glm::vec3(0.55f, 0.53f, 0.5f), 0.8f);
    paints.sand = ikTestPaint(kit, glm::vec3(0.72f, 0.62f, 0.42f), 0.9f);
    paints.moss = ikTestPaint(kit, glm::vec3(0.2f, 0.42f, 0.28f), 0.9f);
    return paints;
}

void buildGround(World& world, const IkTestKit& kit, const TerrainPaints& paints) {
    ikTestStaticBox(world, kit, "ground", paints.grass, glm::vec3(-kGroundHalfSize, -kGroundDepth, -kGroundHalfSize), glm::vec3(kGroundHalfSize * 2.0f, kGroundDepth, kGroundHalfSize * 2.0f));
}

void buildRampWithPlateau(World& world, const IkTestKit& kit, const TerrainPaints& paints) {
    constexpr int rampSteps = 12;
    constexpr float rise = 0.1f;
    constexpr float run = 0.5f;
    constexpr float width = 4.0f;
    constexpr float startX = -16.0f;
    constexpr float startZ = 8.0f;
    for (int step = 0; step < rampSteps; ++step) {
        const float top = rise * static_cast<float>(step + 1);
        const float z = startZ - run * static_cast<float>(step + 1);
        ikTestStaticBox(world, kit, "ramp step", paints.sand, glm::vec3(startX, 0.0f, z), glm::vec3(width, top, run));
    }
    const float plateauTop = rise * static_cast<float>(rampSteps);
    const float plateauZ = startZ - run * static_cast<float>(rampSteps);
    ikTestStaticBox(world, kit, "plateau", paints.stone, glm::vec3(startX, 0.0f, plateauZ - 4.0f), glm::vec3(width, plateauTop, 4.0f));
    constexpr int descentSteps = 4;
    constexpr float descentRise = 0.3f;
    for (int step = 0; step < descentSteps; ++step) {
        const float top = plateauTop - descentRise * static_cast<float>(step + 1);
        const float z = plateauZ - 4.0f - run * 1.2f * static_cast<float>(step + 1);
        ikTestStaticBox(world, kit, "descent step", paints.stone, glm::vec3(startX, 0.0f, z), glm::vec3(width, std::max(top, 0.05f), run * 1.2f));
    }
}

void buildStaircase(World& world, const IkTestKit& kit, const TerrainPaints& paints) {
    constexpr int steps = 6;
    constexpr float rise = 0.25f;
    constexpr float depth = 0.9f;
    constexpr float width = 5.0f;
    for (int step = 0; step < steps; ++step) {
        const float top = rise * static_cast<float>(step + 1);
        ikTestStaticBox(world, kit, "stair", paints.stone, glm::vec3(2.0f, 0.0f, -8.0f - depth * static_cast<float>(step + 1)), glm::vec3(width, top, depth));
    }
    ikTestStaticBox(world, kit, "stair landing", paints.stone, glm::vec3(2.0f, 0.0f, -8.0f - depth * static_cast<float>(steps) - 3.0f), glm::vec3(width, rise * static_cast<float>(steps), 3.0f));
}

float hillHeight(const glm::vec2& offset, float radius, float peak) {
    const float distance = glm::length(offset);
    if (distance >= radius) return 0.0f;
    const float t = 1.0f - distance / radius;
    return peak * t * t * (3.0f - 2.0f * t);
}

void buildHill(World& world, const IkTestKit& kit, uint32_t paint, const glm::vec2& center, float radius, float peak) {
    const int cells = static_cast<int>(std::ceil(radius / kHillCell));
    for (int row = -cells; row < cells; ++row) {
        for (int column = -cells; column < cells; ++column) {
            const glm::vec2 offset((static_cast<float>(column) + 0.5f) * kHillCell, (static_cast<float>(row) + 0.5f) * kHillCell);
            const float raw = hillHeight(offset, radius, peak);
            const float top = std::round(raw / kHeightQuantum) * kHeightQuantum;
            if (top < kHeightQuantum) continue;
            const glm::vec3 corner(center.x + static_cast<float>(column) * kHillCell, 0.0f, center.y + static_cast<float>(row) * kHillCell);
            ikTestStaticBox(world, kit, "hill cell", paint, corner, glm::vec3(kHillCell, top, kHillCell));
        }
    }
}

void buildBumps(World& world, const IkTestKit& kit, const TerrainPaints& paints) {
    constexpr int bumpCount = 14;
    for (int index = 0; index < bumpCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(bumpCount - 1);
        const float x = -9.0f + t * 18.0f;
        const float z = 3.0f + 2.5f * std::sin(t * 9.0f);
        const float radius = 0.3f + 0.12f * static_cast<float>(index % 3);
        ikTestStaticBump(world, kit, "bump", index % 2 == 0 ? paints.moss : paints.stone, glm::vec3(x, 0.05f, z), radius);
    }
}

void buildPatrolPath(World& world, const IkTestKit& kit) {
    const uint32_t marker = ikTestGlow(kit, glm::vec3(0.3f, 0.9f, 0.4f), 3.0f);
    const glm::vec3 waypoints[] = {{6.0f, 0.0f, 9.0f}, {12.0f, 0.0f, 9.0f}, {12.0f, 0.0f, 2.0f}, {6.0f, 0.0f, 0.0f}};
    for (size_t index = 0; index < std::size(waypoints); ++index) {
        EntityDesc description;
        description.name = std::string(kIkTestPatrolPrefix) + std::to_string(index);
        description.mesh = kit.sphere;
        description.material = marker;
        description.position = waypoints[index] + glm::vec3(0.0f, 0.06f, 0.0f);
        description.scale = glm::vec3(0.18f, 0.06f, 0.18f);
        world.create(description);
    }
}

}

void buildIkTestTerrain(World& world, const IkTestKit& kit) {
    const TerrainPaints paints = makePaints(kit);
    buildGround(world, kit, paints);
    buildRampWithPlateau(world, kit, paints);
    buildStaircase(world, kit, paints);
    buildHill(world, kit, paints.moss, glm::vec2(15.0f, -6.0f), 5.5f, 1.5f);
    buildHill(world, kit, paints.sand, glm::vec2(-6.0f, -12.0f), 4.5f, 1.0f);
    buildBumps(world, kit, paints);
    buildPatrolPath(world, kit);
}
