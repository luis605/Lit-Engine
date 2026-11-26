module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.camera;
import Engine.mesh;
import Engine.Terrain;
import Engine.Render.material;

bool buildTerrainTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info);

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRouteRadius = 900.0f;
constexpr float kFlySpeed = 45.0f;
constexpr float kClearance = 38.0f;

float envFloat(const char* name, float fallback) {
    const char* value = std::getenv(name);
    return value ? static_cast<float>(std::atof(value)) : fallback;
}

struct MeshBuilder {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    unsigned int vertex(const glm::vec3& p, const glm::vec3& n, float u, float v) {
        vertices.insert(vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, v});
        return static_cast<unsigned int>(vertices.size() / kVertexFloats - 1);
    }

    void triangle(unsigned int a, unsigned int b, unsigned int c, const glm::vec3& outward) {
        const auto position = [&](unsigned int i) { return glm::vec3(vertices[i * kVertexFloats], vertices[i * kVertexFloats + 1], vertices[i * kVertexFloats + 2]); };
        const glm::vec3 normal = glm::cross(position(b) - position(a), position(c) - position(a));
        if (glm::dot(normal, outward) < 0.0f) std::swap(b, c);
        indices.insert(indices.end(), {a, b, c});
    }

    void cone(float y0, float y1, float r0, float r1, int sides, float u) {
        const float slope = (r0 - r1) / std::max(y1 - y0, 1.0e-4f);
        for (int i = 0; i < sides; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(sides);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(sides);
            const glm::vec3 d0(std::cos(a0), 0.0f, std::sin(a0));
            const glm::vec3 d1(std::cos(a1), 0.0f, std::sin(a1));
            const glm::vec3 n0 = glm::normalize(d0 + glm::vec3(0.0f, slope, 0.0f));
            const glm::vec3 n1 = glm::normalize(d1 + glm::vec3(0.0f, slope, 0.0f));
            const unsigned int b0 = vertex(glm::vec3(d0.x * r0, y0, d0.z * r0), n0, u, 0.0f);
            const unsigned int b1 = vertex(glm::vec3(d1.x * r0, y0, d1.z * r0), n1, u, 0.0f);
            const unsigned int t0 = vertex(glm::vec3(d0.x * r1, y1, d0.z * r1), n0, u, 1.0f);
            const unsigned int t1 = vertex(glm::vec3(d1.x * r1, y1, d1.z * r1), n1, u, 1.0f);
            const glm::vec3 outward = d0 + d1;
            triangle(b0, b1, t1, outward);
            if (r1 > 1.0e-4f) triangle(b0, t1, t0, outward);
        }
        const glm::vec3 centre(0.0f, y0, 0.0f);
        const unsigned int c = vertex(centre, glm::vec3(0.0f, -1.0f, 0.0f), u, 0.0f);
        for (int i = 0; i < sides; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(sides);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(sides);
            const unsigned int p0 = vertex(glm::vec3(std::cos(a0) * r0, y0, std::sin(a0) * r0), glm::vec3(0.0f, -1.0f, 0.0f), u, 0.0f);
            const unsigned int p1 = vertex(glm::vec3(std::cos(a1) * r0, y0, std::sin(a1) * r0), glm::vec3(0.0f, -1.0f, 0.0f), u, 0.0f);
            triangle(c, p0, p1, glm::vec3(0.0f, -1.0f, 0.0f));
        }
    }

    [[nodiscard]] Mesh build() { return Mesh(std::move(vertices), std::move(indices)); }
};

std::shared_ptr<const Mesh> makePine() {
    MeshBuilder b;
    b.cone(0.0f, 2.2f, 0.28f, 0.2f, 6, 0.0f);
    b.cone(1.6f, 4.6f, 1.7f, 0.15f, 9, 1.0f);
    b.cone(3.2f, 6.2f, 1.3f, 0.12f, 9, 1.0f);
    b.cone(4.8f, 7.6f, 0.9f, 0.0f, 9, 1.0f);
    return std::make_shared<const Mesh>(b.build());
}

std::shared_ptr<const Mesh> makeRock() {
    MeshBuilder b;
    constexpr int rings = 5;
    constexpr int sides = 9;
    std::vector<unsigned int> grid;
    for (int r = 0; r <= rings; ++r) {
        const float phi = kPi * static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s < sides; ++s) {
            const float theta = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(sides);
            const float wobble = 0.8f + 0.3f * std::sin(theta * 3.0f + phi * 2.0f) * std::cos(phi * 4.0f + 1.0f);
            const glm::vec3 dir(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            grid.push_back(b.vertex(glm::vec3(dir.x * 1.2f, dir.y * 0.8f + 0.5f, dir.z * 1.0f) * wobble, dir, 0.0f, 0.0f));
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sides; ++s) {
            const unsigned int a = grid[static_cast<size_t>(r * sides + s)];
            const unsigned int c = grid[static_cast<size_t>(r * sides + (s + 1) % sides)];
            const unsigned int d = grid[static_cast<size_t>((r + 1) * sides + s)];
            const unsigned int e = grid[static_cast<size_t>((r + 1) * sides + (s + 1) % sides)];
            const auto pos = [&](unsigned int i) { return glm::vec3(b.vertices[i * kVertexFloats], b.vertices[i * kVertexFloats + 1], b.vertices[i * kVertexFloats + 2]); };
            const glm::vec3 outward = pos(a) + pos(c) + pos(d) + pos(e) - glm::vec3(0.0f, 4.0f * 0.5f, 0.0f);
            b.triangle(a, d, e, outward);
            b.triangle(a, e, c, outward);
        }
    }
    return std::make_shared<const Mesh>(b.build());
}

struct TerrainSceneState {
    float travelled = 0.0f;
    float altitude = 0.0f;
    bool altitudeValid = false;
    uint32_t ticks = 0;
    double maxUpdateMs = 0.0;
    uint32_t peakEntities = 0;
    uint32_t peakGpuMeshes = 0;
    uint32_t peakResident = 0;
    float minClearance = 1.0e9f;
    uint32_t seed = 7;
    float clearance = kClearance;
    float heightMin = 0.0f;
    float heightMax = 0.0f;
    float heightMean = 0.0f;
    float pitch = -9.0f;
    float speed = kFlySpeed;
};

glm::vec2 routePoint(float travelled) {
    const float angle = travelled / kRouteRadius;
    return glm::vec2(std::cos(angle) * kRouteRadius, std::sin(angle) * kRouteRadius * 0.75f);
}

}

bool buildTerrainTestScene(Engine& engine, const TestSceneKit&, TestSceneInfo& info) {
    auto state = std::make_shared<TerrainSceneState>();

    TerrainSetup setup;
    NoiseParams noise;
    noise.seed = state->seed;
    noise.baseHeight = envFloat("LIT_TERRAIN_BASE", 50.0f);
    noise.amplitude = envFloat("LIT_TERRAIN_AMPLITUDE", 200.0f);
    noise.flatten = envFloat("LIT_TERRAIN_FLATTEN", 1.0f);
    setup.terrain.height = makeNoiseHeight(noise);
    setup.terrain.tileSize = 64.0f;
    setup.terrain.tileCells = 64;
    setup.terrain.minHeight = -140.0f;
    setup.terrain.maxHeight = 320.0f;
    setup.terrain.splat.snowMinHeight = 105.0f;
    setup.stream.lod.maxLevel = 5;
    setup.stream.lod.splitFactor = 2.0f;
    setup.stream.lod.viewDistance = 3600.0f;
    setup.stream.workers = 3;
    setup.stream.maxInFlight = 8;
    setup.stream.deterministic = engine.headless();
    setup.colliderMaxLevel = 0;
    setup.scatterMaxLevel = 2;
    setup.scatter.seed = 11;
    setup.instancedScatter = envFloat("LIT_TERRAIN_INSTANCED", 1.0f) > 0.5f;

    const uint32_t foliageShader = engine.loadShader("Foliage", "resources/shaders/surfaces/foliage.surface");
    Material pineMaterial;
    pineMaterial.name = "Pine";
    pineMaterial.baseColor = glm::vec3(0.5f);
    pineMaterial.roughness = 0.9f;
    Material rockMaterial;
    rockMaterial.name = "Rock";
    rockMaterial.baseColor = glm::vec3(0.36f, 0.34f, 0.32f);
    rockMaterial.roughness = 0.95f;

    ScatterSpecies pine;
    pine.name = "pine";
    pine.kind = ScatterKind::Tree;
    pine.geometry = makePine();
    pine.material = engine.createMaterial(pineMaterial);
    pine.shader = foliageShader;
    pine.spacing = 9.0f;
    pine.density = 0.55f;
    pine.minHeight = 4.0f;
    pine.maxHeight = 95.0f;
    pine.minNormalY = 0.88f;
    pine.minScale = 0.8f;
    pine.maxScale = 1.6f;
    pine.clump = 0.8f;
    pine.tag = "tree";
    setup.scatter.species.push_back(pine);

    ScatterSpecies rock;
    rock.name = "rock";
    rock.kind = ScatterKind::Rock;
    rock.geometry = makeRock();
    rock.material = engine.createMaterial(rockMaterial);
    rock.spacing = 17.0f;
    rock.density = 0.35f;
    rock.minNormalY = 0.45f;
    rock.minScale = 0.6f;
    rock.maxScale = 2.4f;
    rock.yOffset = -0.15f;
    rock.alignToSlope = true;
    rock.tag = "rock";
    setup.scatter.species.push_back(rock);

    state->travelled = envFloat("LIT_TERRAIN_TRAVEL", 0.0f);
    state->clearance = envFloat("LIT_TERRAIN_CLEARANCE", kClearance);
    state->pitch = envFloat("LIT_TERRAIN_PITCH", -9.0f);
    state->speed = envFloat("LIT_TERRAIN_SPEED", kFlySpeed);
    const glm::vec2 start = routePoint(state->travelled);
    const float groundAtStart = terrainHeight(setup.terrain, start.x, start.y);
    state->altitude = groundAtStart + state->clearance;
    state->altitudeValid = true;
    info.start = glm::vec3(start.x, state->altitude, start.y);
    {
        const glm::vec2 a = routePoint(state->travelled);
        const glm::vec2 b = routePoint(state->travelled + 60.0f);
        info.yaw = glm::degrees(std::atan2(b.y - a.y, b.x - a.x));
    }
    info.pitch = state->pitch;
    info.nearPlane = 0.3f;
    info.farPlane = 6000.0f;
    info.suggestedTicks = 900;
    {
        Camera& camera = engine.world().camera();
        camera.setPos(info.start);
        camera.setOrientation(info.yaw, info.pitch);
        camera.setNearPlane(info.nearPlane);
        camera.setFarPlane(info.farPlane);
    }

    const TerrainConfig terrain = setup.terrain;
    {
        double sum = 0.0;
        float low = 1.0e9f;
        float high = -1.0e9f;
        int count = 0;
        for (float z = -4000.0f; z <= 4000.0f; z += 100.0f) {
            for (float x = -4000.0f; x <= 4000.0f; x += 100.0f) {
                const float h = terrainHeight(terrain, x, z);
                sum += h;
                low = std::min(low, h);
                high = std::max(high, h);
                ++count;
            }
        }
        state->heightMin = low;
        state->heightMax = high;
        state->heightMean = static_cast<float>(sum / count);
    }
    engine.enableTerrain(setup);

    Engine* enginePointer = &engine;
    engine.addSystem(Phase::Update, "testscene.terrain", [state, terrain, enginePointer](World& world, float deltaTime) {
        state->travelled += state->speed * std::min(deltaTime, 0.1f);
        const glm::vec2 here = routePoint(state->travelled);
        const glm::vec2 ahead = routePoint(state->travelled + 60.0f);
        const glm::vec2 direction = glm::normalize(ahead - here);
        float ground = -1.0e9f;
        for (int i = -1; i <= 4; ++i) ground = std::max(ground, terrainHeight(terrain, here.x + direction.x * 25.0f * static_cast<float>(i), here.y + direction.y * 25.0f * static_cast<float>(i)));
        const float target = ground + state->clearance;
        state->altitude += (target - state->altitude) * std::min(1.0f, deltaTime * 1.5f);
        Camera& camera = world.camera();
        enginePointer->setCameraWorldPosition(glm::dvec3(here.x, state->altitude, here.y));
        const float yaw = glm::degrees(std::atan2(direction.y, direction.x));
        const float pitch = state->pitch - std::clamp((state->altitude - target) * 0.1f, -4.0f, 4.0f);
        camera.setOrientation(yaw, pitch);
        state->minClearance = std::min(state->minClearance, state->altitude - terrainHeight(terrain, here.x, here.y));
        ++state->ticks;
        if (const TerrainStreamer* streamer = enginePointer->terrain()) {
            const TerrainStats& stats = streamer->stats();
            state->maxUpdateMs = std::max(state->maxUpdateMs, stats.updateMillis);
            state->peakEntities = std::max(state->peakEntities, stats.entities);
            state->peakGpuMeshes = std::max(state->peakGpuMeshes, stats.gpuMeshes);
            state->peakResident = std::max(state->peakResident, stats.resident);
        }
    });

    info.metrics = [state, enginePointer]() {
        TestSceneMetrics metrics;
        metrics.set("ticks", state->ticks);
        metrics.set("height_min_m", state->heightMin);
        metrics.set("height_max_m", state->heightMax);
        metrics.set("height_mean_m", state->heightMean);
        metrics.set("travelled_m", state->travelled);
        metrics.set("camera_altitude_m", state->altitude);
        metrics.set("min_clearance_m", state->minClearance);
        metrics.set("terrain_update_ms_max", state->maxUpdateMs);
        metrics.set("terrain_peak_entities", state->peakEntities);
        metrics.set("terrain_peak_gpu_meshes", state->peakGpuMeshes);
        metrics.set("terrain_peak_resident", state->peakResident);
        if (const TerrainStreamer* streamer = enginePointer->terrain()) {
            const TerrainStats& stats = streamer->stats();
            metrics.set("terrain_wanted", stats.wanted);
            metrics.set("terrain_resident", stats.resident);
            metrics.set("terrain_visible", stats.visible);
            metrics.set("terrain_loaded", stats.loaded);
            metrics.set("terrain_evicted", stats.evicted);
            metrics.set("terrain_frustum_culled", stats.culledByFrustum);
            metrics.set("terrain_entities", stats.entities);
        }
        return metrics;
    };
    return true;
}
