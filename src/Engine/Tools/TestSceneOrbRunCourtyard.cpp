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
constexpr int kBoughSides = 22;
constexpr int kBoughTiers = 7;
constexpr float kBoughNotch = 0.62f;
constexpr size_t kTextureU = 6;
constexpr float kWoodCoordinate = 0.0f;
constexpr float kCrownCoordinate = 1.0f;
constexpr int kBoulderRings = 20;
constexpr int kBoulderSides = 28;
constexpr int kStandingStones = 6;
constexpr float kStoneRingRadius = 17.0f;
constexpr float kBraziersLightIntensity = 9000.0f;
constexpr float kBraziersLightRange = 16.0f;
constexpr float kSunIlluminanceMatchingLamps = 7000.0f;
constexpr float kFixedExposureEv100 = 10.0f;

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

void paintFoliageCoordinate(MeshData& mesh, size_t firstVertex, float woodOrCrown) {
    for (size_t vertex = firstVertex; vertex * kVertexFloats < mesh.vertices.size(); ++vertex) mesh.vertices[vertex * kVertexFloats + kTextureU] = woodOrCrown;
}

void appendBranchTier(MeshData& mesh, float bottom, float top, float radius, float twist) {
    const unsigned int first = static_cast<unsigned int>(mesh.vertices.size() / kVertexFloats);
    const float height = top - bottom;
    for (int side = 0; side <= kBoughSides; ++side) {
        const float angle = twist + 2.0f * kPi * static_cast<float>(side % kBoughSides) / static_cast<float>(kBoughSides);
        const bool tip = side % 2 == 0;
        const float reach = radius * (tip ? 1.0f + 0.12f * std::sin(angle * 5.0f + bottom) : kBoughNotch);
        const float droop = tip ? 0.0f : height * 0.17f;
        const glm::vec2 around(std::cos(angle), std::sin(angle));
        const glm::vec3 normal = glm::normalize(glm::vec3(around.x * height, reach, around.y * height));
        mesh.vertices.insert(mesh.vertices.end(), {reach * around.x, bottom + droop, reach * around.y, normal.x, normal.y, normal.z, kCrownCoordinate, 0.0f});
        mesh.vertices.insert(mesh.vertices.end(), {0.02f * around.x, top, 0.02f * around.y, normal.x, normal.y, normal.z, kCrownCoordinate, 1.0f});
    }
    for (unsigned int side = 0; side < static_cast<unsigned int>(kBoughSides); ++side) {
        const unsigned int rim = first + side * 2;
        mesh.indices.insert(mesh.indices.end(), {rim, rim + 1, rim + 2, rim + 2, rim + 1, rim + 3});
    }
}

std::shared_ptr<const Mesh> makePine() {
    MeshData data;
    appendLathe(data, {{0.32f, 0.0f}, {0.2f, 2.6f}}, kLatheSides, 0.0f);
    paintFoliageCoordinate(data, 0, kWoodCoordinate);
    for (int tier = 0; tier < kBoughTiers; ++tier) {
        const float along = static_cast<float>(tier) / static_cast<float>(kBoughTiers - 1);
        const float bottom = 1.4f + along * 4.8f;
        appendBranchTier(data, bottom, bottom + 2.1f - along * 0.7f, 1.85f - along * 1.35f, static_cast<float>(tier) * 0.7f);
    }
    return std::make_shared<const Mesh>(std::move(data.vertices), std::move(data.indices));
}

float boulderRadius(const glm::vec3& direction) {
    const float broad = std::sin(direction.x * 2.3f + 1.7f) * std::sin(direction.y * 2.9f + 0.4f) * std::sin(direction.z * 2.1f + 2.6f);
    const float medium = std::sin(direction.x * 5.7f + direction.z * 3.1f) * std::sin(direction.y * 6.3f - direction.x * 2.2f);
    const float fine = std::sin(direction.x * 13.0f - direction.y * 7.0f) * std::sin(direction.z * 11.0f + direction.y * 5.0f);
    return 1.0f + 0.28f * broad + 0.12f * medium + 0.045f * fine;
}

std::shared_ptr<const Mesh> makeRock() {
    MeshData data;
    std::vector<glm::vec3> points;
    for (int ring = 0; ring <= kBoulderRings; ++ring) {
        const float latitude = kPi * (static_cast<float>(ring) / static_cast<float>(kBoulderRings) - 0.5f);
        for (int side = 0; side <= kBoulderSides; ++side) {
            const float longitude = 2.0f * kPi * static_cast<float>(side % kBoulderSides) / static_cast<float>(kBoulderSides);
            const glm::vec3 direction(std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude));
            points.push_back(glm::vec3(0.0f, 0.65f, 0.0f) + direction * boulderRadius(direction) * glm::vec3(1.15f, 0.75f, 1.05f));
        }
    }
    const auto at = [&](int ring, int side) { return points[static_cast<size_t>(ring * (kBoulderSides + 1) + (side + kBoulderSides) % kBoulderSides)]; };
    for (int ring = 0; ring <= kBoulderRings; ++ring) {
        for (int side = 0; side <= kBoulderSides; ++side) {
            const glm::vec3 point = points[static_cast<size_t>(ring * (kBoulderSides + 1) + side)];
            const glm::vec3 acrossRing = at(ring, side + 1) - at(ring, side - 1);
            const glm::vec3 alongSide = at(std::min(ring + 1, kBoulderRings), side) - at(std::max(ring - 1, 0), side);
            const glm::vec3 cross = glm::cross(alongSide, acrossRing);
            const bool pole = ring == 0 || ring == kBoulderRings;
            const glm::vec3 normal = pole || glm::dot(cross, cross) < 1.0e-8f ? glm::vec3(0.0f, ring == 0 ? -1.0f : 1.0f, 0.0f) : glm::normalize(cross);
            data.vertices.insert(data.vertices.end(), {point.x, point.y, point.z, normal.x, normal.y, normal.z, static_cast<float>(side) / static_cast<float>(kBoulderSides), static_cast<float>(ring) / static_cast<float>(kBoulderRings)});
        }
    }
    for (int ring = 0; ring < kBoulderRings; ++ring) {
        for (int side = 0; side < kBoulderSides; ++side) {
            const unsigned int a = static_cast<unsigned int>(ring * (kBoulderSides + 1) + side);
            const unsigned int c = a + static_cast<unsigned int>(kBoulderSides + 1);
            data.indices.insert(data.indices.end(), {a, c, a + 1, a + 1, c, c + 1});
        }
    }
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
    setup.albedoTextures = {finishPath(Finish::Ground, "albedo"), finishPath(Finish::Rock, "albedo"), "", ""};
    setup.normalTextures = {finishPath(Finish::Ground, "normal"), finishPath(Finish::Rock, "normal"), "", ""};
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
    rockMaterial.triplanar = true;
    rockMaterial.uvScale = 0.4f;
    if (!context.engine.headless()) {
        rockMaterial.baseColorTexture = context.engine.loadTexture(finishPath(Finish::Rock, "albedo"), true);
        rockMaterial.normalTexture = context.engine.loadTexture(finishPath(Finish::Rock, "normal"), false);
        rockMaterial.ormTexture = context.engine.loadTexture(finishPath(Finish::Rock, "orm"), false);
    }
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
    const uint32_t stone = context.surfaced(Finish::Masonry, glm::vec3(0.72f, 0.68f, 0.62f), 0.85f, {.tileMeters = 2.2f});
    const uint32_t flagstone = context.surfaced(Finish::Flagstone, glm::vec3(0.62f, 0.58f, 0.53f), 0.75f, {.tileMeters = 3.5f});
    const uint32_t flame = context.glow(glm::vec3(1.0f, 0.55f, 0.2f), 9.0f);
    buildBoundary(context, center);
    buildLowWalls(context, center, stone);
    buildArch(context, center, stone, flame);
    buildStandingStones(context, center, context.surfaced(Finish::Rock, glm::vec3(0.55f, 0.52f, 0.48f), 0.9f, {.tileMeters = 2.0f}));
    const glm::vec3 half(kFlagstoneHalf, 0.0f, kFlagstoneHalf);
    context.block("courtyard flagstones", flagstone, center - half + glm::vec3(0.0f, -0.2f, 0.0f), center + half + glm::vec3(0.0f, kFlagstoneTop, 0.0f));
    context.probe("courtyard probe", center + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(kBoundaryHalf, 6.0f, kBoundaryHalf));
    context.engine.enableTerrain(makeTerrainSetup(context));
}

void applyRenderSettings(Engine& engine) {
    RenderSettings& settings = engine.renderSettings();
    settings.timeOfDay = 16.4f;
    settings.sunIlluminance = kSunIlluminanceMatchingLamps;
    settings.autoExposure = false;
    settings.manualEV100 = kFixedExposureEv100;
    settings.sunAzimuth = 250.0f;
    settings.clouds = true;
    settings.cloudCoverage = 0.3f;
    settings.cloudSpeed = 0.0f;
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
