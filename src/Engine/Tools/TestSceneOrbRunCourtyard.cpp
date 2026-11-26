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
#include <utility>
#include <vector>

module Engine.OrbRun;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Portal;
import Engine.Physics;
import Engine.Terrain;
import Engine.mesh;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace orbrun {

namespace {

constexpr float kPi = 3.14159265f;
constexpr uint64_t kTerrainSeed = 7;
constexpr float kVoidX = 250.0f;
constexpr float kVoidHeight = -400.0f;
constexpr float kFlatRadius = 34.0f;
constexpr float kRimRadius = 95.0f;
constexpr float kRimHeight = 14.0f;
constexpr float kRimVariation = 16.0f;
constexpr float kNoiseFrequency = 0.018f;
constexpr float kBoundaryHalf = 29.0f;
constexpr float kBoundaryThickness = 1.0f;
constexpr float kBoundaryHeight = 40.0f;
constexpr float kLowWallHeight = 1.1f;
constexpr float kBackWallThickness = 0.8f;
constexpr float kArchHeight = 5.4f;
constexpr float kFlagstoneHalf = 11.0f;
constexpr float kFlagstoneTop = 0.06f;
constexpr int kLatheSides = 9;
constexpr int kStandingStones = 6;
constexpr float kStoneRingRadius = 17.0f;
constexpr float kBraziersLightIntensity = 9000.0f;
constexpr float kBraziersLightRange = 16.0f;

struct MeshData {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
};

void appendLathe(MeshData& mesh, const std::vector<glm::vec2>& profile, int sides, float wobble) {
    const unsigned int first = static_cast<unsigned int>(mesh.vertices.size() / kVertexFloats);
    const size_t rings = profile.size();
    for (size_t ring = 0; ring < rings; ++ring) {
        const glm::vec2 before = profile[ring == 0 ? 0 : ring - 1];
        const glm::vec2 after = profile[std::min(ring + 1, rings - 1)];
        const glm::vec2 tangent = after - before;
        const glm::vec2 outward = glm::normalize(glm::vec2(tangent.y, -tangent.x));
        for (int side = 0; side < sides; ++side) {
            const float angle = 2.0f * kPi * static_cast<float>(side) / static_cast<float>(sides);
            const float radius = profile[ring].x * (1.0f + wobble * std::sin(3.0f * angle + profile[ring].y * 2.0f));
            const glm::vec3 normal(outward.x * std::cos(angle), outward.y, outward.x * std::sin(angle));
            mesh.vertices.insert(mesh.vertices.end(), {radius * std::cos(angle), profile[ring].y, radius * std::sin(angle), normal.x, normal.y, normal.z, static_cast<float>(side) / static_cast<float>(sides), static_cast<float>(ring) / static_cast<float>(rings)});
        }
    }
    const auto position = [&](unsigned int index) { return glm::vec3(mesh.vertices[index * kVertexFloats], mesh.vertices[index * kVertexFloats + 1], mesh.vertices[index * kVertexFloats + 2]); };
    const auto addTriangle = [&](unsigned int a, unsigned int b, unsigned int c) {
        const glm::vec3 facing = glm::cross(position(b) - position(a), position(c) - position(a));
        const glm::vec3 center = (position(a) + position(b) + position(c)) / 3.0f;
        const glm::vec3 away(center.x, 0.0f, center.z);
        if (glm::dot(facing, away) < 0.0f) std::swap(b, c);
        mesh.indices.insert(mesh.indices.end(), {a, b, c});
    };
    for (size_t ring = 0; ring + 1 < rings; ++ring) {
        for (int side = 0; side < sides; ++side) {
            const unsigned int next = static_cast<unsigned int>((side + 1) % sides);
            const unsigned int a = first + static_cast<unsigned int>(ring * static_cast<size_t>(sides)) + static_cast<unsigned int>(side);
            const unsigned int b = first + static_cast<unsigned int>(ring * static_cast<size_t>(sides)) + next;
            const unsigned int c = a + static_cast<unsigned int>(sides);
            const unsigned int d = b + static_cast<unsigned int>(sides);
            addTriangle(a, b, d);
            addTriangle(a, d, c);
        }
    }
}

std::shared_ptr<const Mesh> makePine() {
    MeshData data;
    appendLathe(data, {{0.28f, 0.0f}, {0.2f, 2.2f}}, 6, 0.0f);
    appendLathe(data, {{1.7f, 1.6f}, {0.15f, 4.6f}}, kLatheSides, 0.0f);
    appendLathe(data, {{1.3f, 3.2f}, {0.12f, 6.2f}}, kLatheSides, 0.0f);
    appendLathe(data, {{0.9f, 4.8f}, {0.01f, 7.6f}}, kLatheSides, 0.0f);
    return std::make_shared<const Mesh>(std::move(data.vertices), std::move(data.indices));
}

std::shared_ptr<const Mesh> makeRock() {
    MeshData data;
    appendLathe(data, {{0.01f, 0.0f}, {0.9f, 0.15f}, {1.2f, 0.7f}, {0.9f, 1.15f}, {0.01f, 1.35f}}, kLatheSides, 0.18f);
    return std::make_shared<const Mesh>(std::move(data.vertices), std::move(data.indices));
}

ScatterSpecies makeSpecies(const char* name, ScatterKind kind, std::shared_ptr<const Mesh> geometry, uint32_t material, uint32_t shader) {
    ScatterSpecies species;
    species.name = name;
    species.kind = kind;
    species.geometry = std::move(geometry);
    species.material = material;
    species.shader = shader;
    return species;
}

TerrainSetup makeTerrainSetup(const Context& context) {
    TerrainSetup setup;
    setup.terrain.height = [](float x, float z) { return orbRunTerrainHeight(x, z); };
    setup.terrain.tileSize = 64.0f;
    setup.terrain.tileCells = 32;
    setup.terrain.minHeight = kVoidHeight - 10.0f;
    setup.terrain.maxHeight = kRimHeight + kRimVariation + 10.0f;
    setup.terrain.splat.sandMaxHeight = -1.0e6f;
    setup.terrain.splat.snowMinHeight = 1.0e6f;
    setup.stream.lod.maxLevel = 4;
    setup.stream.lod.splitFactor = 2.0f;
    setup.stream.lod.viewDistance = 500.0f;
    setup.stream.workers = 2;
    setup.stream.maxInFlight = 6;
    setup.stream.deterministic = context.engine.headless();
    setup.colliderMaxLevel = 0;
    setup.scatterMaxLevel = 1;
    setup.scatter.seed = 11;
    setup.instancedScatter = true;
    const uint32_t foliage = context.engine.headless() ? 0u : context.engine.loadShader("Foliage", "resources/shaders/surfaces/foliage.surface");
    Material pineMaterial;
    pineMaterial.name = "Orb Run Pine";
    pineMaterial.baseColor = glm::vec3(0.35f, 0.5f, 0.3f);
    pineMaterial.roughness = 0.9f;
    Material rockMaterial;
    rockMaterial.name = "Orb Run Rock";
    rockMaterial.baseColor = glm::vec3(0.4f, 0.37f, 0.35f);
    rockMaterial.roughness = 0.95f;
    ScatterSpecies pine = makeSpecies("pine", ScatterKind::Tree, makePine(), context.material(pineMaterial), foliage);
    pine.spacing = 8.0f;
    pine.density = 0.7f;
    pine.minHeight = 4.0f;
    pine.minNormalY = 0.85f;
    pine.minScale = 0.9f;
    pine.maxScale = 1.7f;
    pine.clump = 0.7f;
    pine.tag = "tree";
    ScatterSpecies rock = makeSpecies("rock", ScatterKind::Rock, makeRock(), context.material(rockMaterial), 0u);
    rock.spacing = 11.0f;
    rock.density = 0.5f;
    rock.minHeight = 2.0f;
    rock.minNormalY = 0.5f;
    rock.minScale = 0.7f;
    rock.maxScale = 2.4f;
    rock.yOffset = -0.15f;
    rock.alignToSlope = true;
    rock.tag = "rock";
    setup.scatter.species = {pine, rock};
    return setup;
}

void buildBoundary(const Context& context, const glm::vec3& center) {
    const glm::vec3 low = center + glm::vec3(-kBoundaryHalf, -2.0f, -kBoundaryHalf);
    const glm::vec3 high = center + glm::vec3(kBoundaryHalf, kBoundaryHeight, kBoundaryHalf);
    context.solid(low, glm::vec3(high.x, 0.0f, high.z));
    context.solid(glm::vec3(low.x - kBoundaryThickness, low.y, low.z - kBoundaryThickness), glm::vec3(low.x, high.y, high.z + kBoundaryThickness));
    context.solid(glm::vec3(high.x, low.y, low.z - kBoundaryThickness), glm::vec3(high.x + kBoundaryThickness, high.y, high.z + kBoundaryThickness));
    context.solid(glm::vec3(low.x, low.y, low.z - kBoundaryThickness), glm::vec3(high.x, high.y, low.z));
    context.solid(glm::vec3(low.x, low.y, high.z), glm::vec3(high.x, high.y, high.z + kBoundaryThickness));
}

void buildLowWalls(const Context& context, const glm::vec3& center, uint32_t stone) {
    const float inner = kBoundaryHalf - 0.8f;
    const float outer = kBoundaryHalf;
    const float top = kLowWallHeight;
    context.visual("courtyard west wall", stone, center + glm::vec3(-outer, 0.0f, -outer), center + glm::vec3(-inner, top, outer));
    context.visual("courtyard east wall", stone, center + glm::vec3(inner, 0.0f, -outer), center + glm::vec3(outer, top, outer));
    context.visual("courtyard north wall", stone, center + glm::vec3(-outer, 0.0f, -outer), center + glm::vec3(outer, top, -inner));
}

void buildArch(const Context& context, const glm::vec3& center, uint32_t stone, uint32_t flame) {
    const float pillarInner = 2.3f;
    const float pillarOuter = 3.5f;
    const float lintelBottom = 4.7f;
    const float frontZ = center.z + kCourtyardArchZ - 0.4f;
    const float backZ = center.z + kCourtyardArchZ + 0.1f;
    context.block("arch left pillar", stone, glm::vec3(center.x - pillarOuter, 0.0f, frontZ), glm::vec3(center.x - pillarInner, kArchHeight, backZ + kBackWallThickness));
    context.block("arch right pillar", stone, glm::vec3(center.x + pillarInner, 0.0f, frontZ), glm::vec3(center.x + pillarOuter, kArchHeight, backZ + kBackWallThickness));
    context.block("arch lintel", stone, glm::vec3(center.x - pillarOuter, lintelBottom, frontZ), glm::vec3(center.x + pillarOuter, kArchHeight, backZ + kBackWallThickness));
    context.block("arch left wing", stone, glm::vec3(center.x - kBoundaryHalf, 0.0f, backZ), glm::vec3(center.x - pillarInner, kArchHeight, backZ + kBackWallThickness));
    context.block("arch right wing", stone, glm::vec3(center.x + pillarInner, 0.0f, backZ), glm::vec3(center.x + kBoundaryHalf, kArchHeight, backZ + kBackWallThickness));
    for (const float side : {-1.0f, 1.0f}) {
        const glm::vec3 base(center.x + side * 4.6f, 0.0f, center.z + kCourtyardArchZ - 1.5f);
        context.block("courtyard brazier base", stone, base + glm::vec3(-0.4f, 0.0f, -0.4f), base + glm::vec3(0.4f, 1.0f, 0.4f));
        context.visual("courtyard flame", flame, base + glm::vec3(-0.25f, 1.0f, -0.25f), base + glm::vec3(0.25f, 1.5f, 0.25f));
        context.lamp("courtyard brazier light", base + glm::vec3(0.0f, 1.9f, 0.0f), glm::vec3(1.0f, 0.62f, 0.3f), kBraziersLightIntensity, kBraziersLightRange, 0.3f);
    }
}

void buildStandingStones(const Context& context, const glm::vec3& center, uint32_t stone) {
    for (int index = 0; index < kStandingStones; ++index) {
        const float angle = 2.0f * kPi * (static_cast<float>(index) + 0.5f) / static_cast<float>(kStandingStones);
        const glm::vec3 base = center + glm::vec3(std::cos(angle) * kStoneRingRadius, 0.0f, std::sin(angle) * kStoneRingRadius);
        const float height = 2.2f + 0.5f * static_cast<float>(index % 3);
        context.block("standing stone", stone, base + glm::vec3(-0.6f, 0.0f, -0.4f), base + glm::vec3(0.6f, height, 0.4f));
    }
}

}

void buildCourtyard(const Context& context) {
    const glm::vec3 center(kCourtyardX, 0.0f, 0.0f);
    const uint32_t stone = context.paint(glm::vec3(0.62f, 0.58f, 0.52f), 0.8f);
    const uint32_t flagstone = context.paint(glm::vec3(0.5f, 0.47f, 0.43f), 0.6f);
    const uint32_t flame = context.glow(glm::vec3(1.0f, 0.55f, 0.2f), 9.0f);
    buildBoundary(context, center);
    buildLowWalls(context, center, stone);
    buildArch(context, center, stone, flame);
    buildStandingStones(context, center, stone);
    const glm::vec3 half(kFlagstoneHalf, 0.0f, kFlagstoneHalf);
    context.block("courtyard flagstones", flagstone, center - half + glm::vec3(0.0f, -0.2f, 0.0f), center + half + glm::vec3(0.0f, kFlagstoneTop, 0.0f));
    context.probe("courtyard probe", center + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(kBoundaryHalf, 6.0f, kBoundaryHalf));
    context.engine.enableTerrain(makeTerrainSetup(context));
}

void applyRenderSettings(Engine& engine) {
    RenderSettings& settings = engine.renderSettings();
    settings.timeOfDay = 18.2f;
    settings.sunAzimuth = 250.0f;
    settings.clouds = true;
    settings.cloudCoverage = 0.55f;
    settings.cloudSpeed = 8.0f;
    settings.bloom = true;
    settings.bloomIntensity = 0.06f;
    settings.godRays = true;
    settings.fog = true;
    settings.fogDensity = 0.0012f;
    settings.reflectionMode = ReflectionMode::SSRProbes;
    settings.virtualShadows = true;
    settings.areaLights = true;
}

}

float orbRunTerrainHeight(float x, float z) {
    using namespace orbrun;
    if (x < kVoidX) return kVoidHeight;
    const float radius = std::hypot(x - kCourtyardX, z);
    const float blend = std::clamp((radius - kFlatRadius) / (kRimRadius - kFlatRadius), 0.0f, 1.0f);
    const float rise = blend * blend * (3.0f - 2.0f * blend);
    const float noise = std::clamp(0.5f + 0.5f * terrainValueNoise(kTerrainSeed, x * kNoiseFrequency, z * kNoiseFrequency), 0.0f, 1.0f);
    return rise * (kRimHeight + kRimVariation * noise);
}
