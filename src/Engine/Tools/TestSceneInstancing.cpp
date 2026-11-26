module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "Engine/Log/Log.hpp"
#include "Engine/Tools/TestSceneShared.hpp"

module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.Instancing;
import Engine.mesh;
import Engine.asset;
import Engine.Rng;
import Engine.camera;

uint32_t testSceneMaterial(Engine& engine, const TestSceneKit& kit, const Material& material);
uint32_t testSceneMesh(Engine& engine, const std::string& key, const std::function<uint32_t()>& upload);
uint32_t testSceneRockMesh(Engine& engine, const TestSceneKit& kit, int subdivisions, float roughness, float seed);
bool buildInstancingTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info);

namespace {

constexpr float kTile = 128.0f;
constexpr int kTiles = 16;
constexpr float kPi = 3.14159265f;

float envValue(const char* name, float fallback) {
    const char* value = std::getenv(name);
    return value && value[0] ? std::strtof(value, nullptr) : fallback;
}

struct Builder {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    unsigned int vertex(const glm::vec3& p, const glm::vec3& n, float u, float v) {
        vertices.insert(vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, v});
        return static_cast<unsigned int>(vertices.size() / kVertexFloats - 1);
    }
    void cone(float y0, float y1, float r, float r1, int sides, float crown) {
        for (int s = 0; s < sides; ++s) {
            const float a0 = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(sides);
            const float a1 = 2.0f * kPi * static_cast<float>(s + 1) / static_cast<float>(sides);
            const glm::vec3 n0 = glm::normalize(glm::vec3(std::cos(a0), (r - r1) / (y1 - y0), std::sin(a0)));
            const glm::vec3 n1 = glm::normalize(glm::vec3(std::cos(a1), (r - r1) / (y1 - y0), std::sin(a1)));
            const unsigned int b0 = vertex(glm::vec3(std::cos(a0) * r, y0, std::sin(a0) * r), n0, crown, 0.0f);
            const unsigned int b1 = vertex(glm::vec3(std::cos(a1) * r, y0, std::sin(a1) * r), n1, crown, 0.0f);
            const unsigned int t0 = vertex(glm::vec3(std::cos(a0) * r1, y1, std::sin(a0) * r1), n0, crown, 1.0f);
            const unsigned int t1 = vertex(glm::vec3(std::cos(a1) * r1, y1, std::sin(a1) * r1), n1, crown, 1.0f);
            indices.insert(indices.end(), {b0, t0, b1, b1, t0, t1});
        }
    }
    Mesh build() { return Mesh(std::move(vertices), std::move(indices)); }
};

Mesh makeTree() {
    Builder b;
    b.cone(0.0f, 2.2f, 0.28f, 0.2f, 6, 0.0f);
    b.cone(1.6f, 4.6f, 1.7f, 0.15f, 9, 1.0f);
    b.cone(3.2f, 6.2f, 1.3f, 0.12f, 9, 1.0f);
    b.cone(4.8f, 7.6f, 0.9f, 0.0f, 9, 1.0f);
    return b.build();
}

Mesh makeGrass() {
    Builder b;
    for (int card = 0; card < 3; ++card) {
        const float a = static_cast<float>(card) * kPi / 3.0f;
        const glm::vec3 side(std::cos(a) * 0.35f, 0.0f, std::sin(a) * 0.35f);
        const glm::vec3 normal = glm::normalize(glm::vec3(-std::sin(a), 0.4f, std::cos(a)));
        for (int face = 0; face < 2; ++face) {
            const glm::vec3 n = face == 0 ? normal : glm::vec3(-normal.x, normal.y, -normal.z);
            const unsigned int p0 = b.vertex(-side, n, 1.0f, 0.0f);
            const unsigned int p1 = b.vertex(side, n, 1.0f, 0.0f);
            const unsigned int p2 = b.vertex(side * 0.4f + glm::vec3(0.0f, 0.7f, 0.0f), n, 1.0f, 1.0f);
            const unsigned int p3 = b.vertex(-side * 0.4f + glm::vec3(0.0f, 0.7f, 0.0f), n, 1.0f, 1.0f);
            if (face == 0) b.indices.insert(b.indices.end(), {p0, p1, p2, p0, p2, p3});
            else b.indices.insert(b.indices.end(), {p0, p2, p1, p0, p3, p2});
        }
    }
    return b.build();
}

struct InstancingSceneState {
    float angle = 0.0f;
    float speed = 12.0f;
    uint32_t ticks = 0;
    uint32_t batches = 0;
    uint64_t instances = 0;
};

}

bool buildInstancingTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    World& world = engine.world();
    auto state = std::make_shared<InstancingSceneState>();
    state->speed = envValue("LIT_INSTANCING_SPEED", 12.0f);
    const float density = std::clamp(envValue("LIT_INSTANCING_DENSITY", 1.0f), 0.01f, 4.0f);

    const uint32_t foliage = engine.headless() ? 0u : engine.loadShader("Foliage", "resources/shaders/surfaces/foliage.surface");
    const uint32_t treeMesh = engine.headless() ? kit.cube : testSceneMesh(engine, "instancing.tree", [&] { return engine.uploadProcessedMesh(AssetManager::processMesh(makeTree())); });
    const uint32_t grassMesh = engine.headless() ? kit.cube : testSceneMesh(engine, "instancing.grass", [&] { return engine.uploadProcessedMesh(AssetManager::processMesh(makeGrass())); });
    const uint32_t rockMesh = testSceneRockMesh(engine, kit, 3, 0.3f, 7.0f);
    Material leaf;
    leaf.name = "Instanced Pine";
    leaf.baseColor = glm::vec3(1.0f);
    leaf.roughness = 0.9f;
    leaf.custom0 = glm::vec4(0.35f, 7.0f, 1.3f, 0.0f);
    Material blade = leaf;
    blade.name = "Instanced Grass";
    blade.custom0 = glm::vec4(0.12f, 0.7f, 2.2f, 0.0f);
    Material stone;
    stone.name = "Instanced Rock";
    stone.baseColor = glm::vec3(0.42f, 0.4f, 0.37f);
    stone.roughness = 0.85f;
    const uint32_t leafMaterial = testSceneMaterial(engine, kit, leaf);
    const uint32_t bladeMaterial = testSceneMaterial(engine, kit, blade);
    const uint32_t stoneMaterial = testSceneMaterial(engine, kit, stone);

    Material ground;
    ground.baseColor = glm::vec3(0.22f, 0.3f, 0.16f);
    ground.roughness = 0.95f;
    EntityDesc floor;
    floor.name = "ground";
    floor.mesh = kit.cube;
    floor.material = testSceneMaterial(engine, kit, ground);
    floor.shader = kit.floorShader;
    const float half = kTile * kTiles * 0.5f;
    floor.position = glm::vec3(0.0f, -0.1f, 0.0f);
    floor.scale = glm::vec3(2.0f * half + 200.0f, 0.2f, 2.0f * half + 200.0f);
    world.create(floor);

    struct Species {
        uint32_t mesh, material, shader;
        float spacing, density, minScale, maxScale;
        InstanceBatchSettings settings;
        bool tint;
    };
    InstanceBatchSettings trees;
    InstanceBatchSettings rocks;
    rocks.drawDistance = 900.0f;
    rocks.fadeStart = 700.0f;
    InstanceBatchSettings grass;
    grass.drawDistance = 160.0f;
    grass.fadeStart = 110.0f;
    grass.castShadows = false;
    const Species species[3] = {
        {treeMesh,  leafMaterial,  foliage, 9.0f, 0.6f,  0.8f, 1.6f, trees, true },
        {rockMesh,  stoneMaterial, 0u,      6.0f, 0.4f,  0.3f, 1.2f, rocks, false},
        {grassMesh, bladeMaterial, foliage, 1.4f, 0.55f, 0.7f, 1.4f, grass, true },
    };
    std::vector<InstanceData> instances;
    for (int tz = 0; tz < kTiles; ++tz) {
        for (int tx = 0; tx < kTiles; ++tx) {
            const glm::vec3 origin(-half + static_cast<float>(tx) * kTile, 0.0f, -half + static_cast<float>(tz) * kTile);
            for (uint32_t kind = 0; kind < 3; ++kind) {
                const Species& sp = species[kind];
                Pcg32 rng(static_cast<uint64_t>(tz * kTiles + tx) * 3u + kind, 17);
                const float spacing = sp.spacing / std::sqrt(density);
                const int cells = static_cast<int>(kTile / spacing);
                instances.clear();
                for (int cz = 0; cz < cells; ++cz) {
                    for (int cx = 0; cx < cells; ++cx) {
                        const float roll = rng.uniform();
                        const float jx = rng.uniform();
                        const float jz = rng.uniform();
                        const float yaw = rng.uniform() * 2.0f * kPi;
                        const float scale = rng.range(sp.minScale, sp.maxScale);
                        const uint32_t seed = rng.next();
                        const glm::vec2 p(origin.x + (static_cast<float>(cx) + jx) * spacing, origin.z + (static_cast<float>(cz) + jz) * spacing);
                        const float clump = 0.5f + 0.5f * std::sin(p.x * 0.013f + kind) * std::cos(p.y * 0.011f - kind * 2.0f);
                        if (roll > sp.density * (0.4f + 1.2f * clump)) continue;
                        InstanceData d;
                        d.position = glm::vec3(p.x - origin.x, kind == 1 ? -0.2f * scale : 0.0f, p.y - origin.z);
                        d.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                        d.scale = scale;
                        d.seed = seed;
                        const float tint = sp.tint ? 0.75f + 0.25f * instanceHash01(seed) : 1.0f;
                        d.color = packInstanceColor(glm::vec4(tint, sp.tint ? 0.85f + 0.15f * tint : 1.0f, tint, 1.0f));
                        instances.push_back(d);
                    }
                }
                if (instances.empty()) continue;
                engine.createInstanceBatch(sp.mesh, sp.material, instances, sp.settings, sp.shader, origin);
                ++state->batches;
                state->instances += instances.size();
            }
        }
    }
    Lit::Log::Info("Instancing scene: {} instances in {} batches", state->instances, state->batches);

    info.start = glm::vec3(300.0f, 14.0f, 0.0f);
    info.yaw = 90.0f;
    info.pitch = -8.0f;
    info.nearPlane = 0.2f;
    info.farPlane = 3000.0f;
    info.suggestedTicks = 600;
    Engine* enginePointer = &engine;
    engine.addSystem(Phase::Update, "testscene.instancing", [state](World& world, float deltaTime) {
        const float radius = 300.0f;
        state->angle += state->speed * std::min(deltaTime, 0.1f) / radius;
        const glm::vec3 position(std::cos(state->angle) * radius, 14.0f + 4.0f * std::sin(state->angle * 3.0f), std::sin(state->angle) * radius);
        Camera& camera = world.camera();
        camera.setPos(position);
        const float yaw = glm::degrees(state->angle) + 90.0f;
        camera.setOrientation(yaw, -8.0f);
        ++state->ticks;
    });
    info.metrics = [state, enginePointer]() {
        TestSceneMetrics metrics;
        metrics.set("ticks", state->ticks);
        metrics.set("batches", state->batches);
        metrics.set("instances", static_cast<double>(state->instances));
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        const InstanceRenderStats stats = enginePointer->instanceStats();
        metrics.set("gpu_instances", stats.instances);
        metrics.set("gpu_visible", stats.early.visible + stats.retest.visible);
        metrics.set("gpu_shadow_visible", stats.shadow.visible);
        return metrics;
    };
    return true;
}
