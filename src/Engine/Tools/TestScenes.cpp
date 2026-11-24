module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>
#include "Engine/Log/Log.hpp"
#include "Engine/Tools/TestSceneShared.hpp"

module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.camera;
import Engine.Portal;
import Engine.Portal.view;
import Engine.Nav;
import Engine.Geometry;
import Engine.Rng;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Skin;
import Engine.Terrain;
import Engine.Streaming;
import Engine.GalleryScene;
import Engine.IkTestScene;

bool buildSkinningTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info);
bool buildTerrainTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info);
bool buildShowcaseTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
bool buildProbesTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
bool buildTexturesTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
bool buildLightStressTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
bool buildDenseTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
bool buildInstancingTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info);
bool buildMixedTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
bool buildLargeWorldTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
void resetLargeWorldState(Engine& engine);
uint32_t testSceneMaterial(Engine& engine, const TestSceneKit& kit, const Material& material);
uint32_t testSceneProceduralMesh(Engine& engine, const std::string& key, const std::vector<float>& vertices, const std::vector<unsigned int>& indices);
PortalSceneKit testScenePortalKit(Engine& engine, const TestSceneKit& kit);

namespace {
constexpr size_t kNumberBufferSize = 64;
constexpr size_t kKeyBufferSize = 48;
}

void TestSceneMetrics::set(const std::string& name, double value) {
    for (auto& [key, stored] : values) {
        if (key == name) {
            stored = value;
            return;
        }
    }
    values.emplace_back(name, value);
}

double TestSceneMetrics::get(const std::string& name, double fallback) const {
    for (const auto& [key, stored] : values) {
        if (key == name) return stored;
    }
    return fallback;
}

std::string TestSceneMetrics::toJson() const {
    std::string out = "{";
    char buffer[kNumberBufferSize];
    for (size_t index = 0; index < values.size(); ++index) {
        std::snprintf(buffer, sizeof(buffer), "%.6g", values[index].second);
        out += (index ? ", \"" : "\"") + values[index].first + "\": " + buffer;
    }
    return out + "}";
}

double forwardDepthStep(float nearPlane, float farPlane, float distance) {
    if (distance <= 0.0f || farPlane == nearPlane) return 0.0;
    const double nearDepth = nearPlane;
    const double farDepth = farPlane;
    const double depth = farDepth * (distance - nearDepth) / (distance * (farDepth - nearDepth));
    const double ulp = std::nextafter(static_cast<float>(depth), 2.0f) - static_cast<float>(depth);
    const double slope = farDepth * nearDepth / (distance * distance * (farDepth - nearDepth));
    return ulp / slope;
}

namespace {

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

uint32_t makeMaterial(const TestSceneKit& kit, const glm::vec3& color, float roughness = 0.7f, const glm::vec3& emissive = glm::vec3(0.0f), float intensity = 0.0f) {
    return kit.material ? kit.material(color, emissive, intensity, roughness) : 0u;
}

EntityHandle spawn(World& world, const std::string& name, uint32_t mesh, uint32_t material, const glm::vec3& position, const glm::vec3& scale, uint32_t shader = 0) {
    EntityDesc description;
    description.name = name;
    description.mesh = mesh;
    description.material = material;
    description.shader = shader;
    description.position = position;
    description.scale = scale;
    return world.create(description);
}

struct TortureState {
    uint32_t ticks = 0;
    uint32_t crossings = 0;
    uint32_t maxNodes = 0;
    uint32_t maxDepth = 0;
    double frameMs = 0.0;
    double maxFrameMs = 0.0;
};

constexpr float kTortureWalkSpeed = 1.5f;
constexpr float kTortureViewportHeight = 1080.0f;

}

// Portal scene kit backed by the test scene pools: metallic frame materials and procedural frame meshes (uploaded once per key).
PortalSceneKit testScenePortalKit(Engine& engine, const TestSceneKit& kit) {
    PortalSceneKit portalKit;
    portalKit.cube = kit.cube;
    portalKit.sphere = kit.sphere;
    portalKit.floorShader = kit.floorShader;
    portalKit.material = kit.material;
    portalKit.metalMaterial = [&engine, kit](const glm::vec3& color, float metalness, float roughness, const glm::vec3& emissive, float intensity) {
        Material material;
        material.name = "Portal frame";
        material.baseColor = color;
        material.metallic = metalness;
        material.roughness = roughness;
        material.emissive = emissive;
        material.emissiveIntensity = intensity;
        return testSceneMaterial(engine, kit, material);
    };
    portalKit.mesh = [&engine](const std::string& key, const std::vector<float>& vertices, const std::vector<unsigned int>& indices) { return testSceneProceduralMesh(engine, key, vertices, indices); };
    return portalKit;
}

namespace {

// Static view of the three-room loop (portal_loop): the camera stands in room A looking through the AB portal.
bool buildLoop(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    buildPortalScene(engine.portals(), engine.world(), testScenePortalKit(engine, kit), PortalTestScene::Loop);
    info.start = portalSceneStart(PortalTestScene::Loop, info.yaw, info.pitch);
    info.nearPlane = 0.05f;
    info.farPlane = 300.0f;
    info.suggestedTicks = 120;
    Engine* enginePointer = &engine;
    info.metrics = [enginePointer]() {
        TestSceneMetrics metrics;
        metrics.set("cells", static_cast<double>(enginePointer->portals().cells().size()));
        metrics.set("portals", static_cast<double>(enginePointer->portals().portals().size()));
        return metrics;
    };
    return true;
}

bool buildTorture(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    buildPortalScene(engine.portals(), engine.world(), testScenePortalKit(engine, kit), PortalTestScene::Torture);
    info.start = portalSceneStart(PortalTestScene::Torture, info.yaw, info.pitch);
    info.nearPlane = 0.05f;
    info.farPlane = 300.0f;
    info.suggestedTicks = 600;
    auto state = std::make_shared<TortureState>();
    Engine* enginePointer = &engine;
    engine.addSystem(Phase::Update, "testscene.torture", [enginePointer, state](World& world, float deltaTime) {
        Camera& camera = world.camera();
        // Walk along the view's heading in the camera's own up frame (stays level after any crossing).
        const glm::vec3 up = camera.getFrameUp();
        glm::vec3 forward = camera.getFront() - up * glm::dot(camera.getFront(), up);
        forward = glm::length(forward) > 1.0e-4f ? glm::normalize(forward) : camera.getFront();
        camera.setPos(camera.getPosition() + forward * kTortureWalkSpeed * deltaTime);
        if (enginePointer->portals().step(camera) >= 0) ++state->crossings;
        const double startMs = nowMs();
        const PortalFrame frame = enginePointer->portals().buildFrame(camera, kTortureViewportHeight);
        const double elapsedMs = nowMs() - startMs;
        ++state->ticks;
        state->frameMs += elapsedMs;
        state->maxFrameMs = std::max(state->maxFrameMs, elapsedMs);
        state->maxNodes = std::max<uint32_t>(state->maxNodes, static_cast<uint32_t>(frame.nodes.size()));
        for (const PortalViewNode& node : frame.nodes) state->maxDepth = std::max<uint32_t>(state->maxDepth, static_cast<uint32_t>(node.depth));
    });
    info.metrics = [enginePointer, state]() {
        TestSceneMetrics metrics;
        metrics.set("cells", static_cast<double>(enginePointer->portals().cells().size()));
        metrics.set("portals", static_cast<double>(enginePointer->portals().portals().size()));
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        metrics.set("portal_crossings", state->crossings);
        metrics.set("max_view_nodes", state->maxNodes);
        metrics.set("max_view_depth", state->maxDepth);
        metrics.set("frame_build_ms_mean", state->ticks ? state->frameMs / state->ticks : 0.0);
        metrics.set("frame_build_ms_max", state->maxFrameMs);
        return metrics;
    };
    return true;
}

struct StreamState {
    uint32_t created = 0;
    uint32_t destroyed = 0;
    uint32_t peakEntities = 0;
    uint32_t peakChunks = 0;
    double spawnMs = 0.0;
    double maxTickMs = 0.0;
    float travelled = 0.0f;
    int firstChunk = 0;
    std::vector<std::vector<EntityHandle>> chunks;
    std::vector<int> chunkIndex;
};

constexpr float kChunkLength = 40.0f;
constexpr int kAhead = 6;
constexpr int kBehind = 2;
constexpr int kObjectsPerChunk = 48;
constexpr float kStreamSpeed = 14.0f;
constexpr float kWidth = 30.0f;
constexpr float kRouteAmplitude = 12.0f;
constexpr float kRouteFrequency = 0.02f;

float routeX(float z) { return kRouteAmplitude * std::sin(z * kRouteFrequency); }

bool buildStreaming(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    World& world = engine.world();
        const uint32_t ground = makeMaterial(kit, glm::vec3(0.35f, 0.38f, 0.3f), 0.9f);
    const uint32_t stone = makeMaterial(kit, glm::vec3(0.6f, 0.58f, 0.55f), 0.6f);
    const uint32_t glow = makeMaterial(kit, glm::vec3(0.2f, 0.3f, 0.6f), 0.3f, glm::vec3(0.3f, 0.5f, 1.0f), 3000.0f);
    auto state = std::make_shared<StreamState>();
    auto spawnChunk = [&world, kit, state, ground, stone, glow](int index) {
        const double startMs = nowMs();
        Pcg32 rng(static_cast<uint64_t>(index + 1000) * 7919u, 11u);
        std::vector<EntityHandle> items;
        const float z0 = -static_cast<float>(index) * kChunkLength;
        items.push_back(spawn(world, "chunk ground", kit.cube, ground, glm::vec3(routeX(z0 - kChunkLength * 0.5f) - kWidth, -1.0f, z0 - kChunkLength), glm::vec3(kWidth * 2.0f, 1.0f, kChunkLength + 0.5f), kit.floorShader));
        for (int object = 0; object < kObjectsPerChunk; ++object) {
            const float z = z0 - rng.range(0.0f, kChunkLength);
            const float side = rng.chance(0.5f) ? 1.0f : -1.0f;
            const float x = routeX(z) + side * rng.range(4.0f, kWidth);
            const float height = rng.range(0.5f, 6.0f);
            const bool orb = rng.chance(0.25f);
            items.push_back(spawn(world, orb ? "chunk orb" : "chunk pillar", orb ? kit.sphere : kit.cube, orb ? glow : stone, glm::vec3(x, orb ? height : 0.0f, z), orb ? glm::vec3(1.0f) : glm::vec3(rng.range(0.6f, 2.0f), height, rng.range(0.6f, 2.0f))));
        }
        state->created += 1;
        state->spawnMs += nowMs() - startMs;
        return items;
    };
    state->firstChunk = 0;
    for (int chunk = -kBehind; chunk <= kAhead; ++chunk) {
        state->chunks.push_back(spawnChunk(chunk));
        state->chunkIndex.push_back(chunk);
    }
    state->firstChunk = -kBehind;
    engine.addSystem(Phase::Update, "testscene.stream", [state, spawnChunk](World& world, float deltaTime) {
        const double startMs = nowMs();
        state->travelled += kStreamSpeed * deltaTime;
        Camera& camera = world.camera();
        const float z = -state->travelled;
        camera.setPos(glm::vec3(routeX(z), 2.0f, z));
        camera.setOrientation(glm::degrees(std::atan2(-1.0f, -0.24f * std::cos(z * kRouteFrequency))), -3.0f);
        const int current = static_cast<int>(state->travelled / kChunkLength);
        while (state->chunkIndex.back() < current + kAhead) {
            const int next = state->chunkIndex.back() + 1;
            state->chunks.push_back(spawnChunk(next));
            state->chunkIndex.push_back(next);
        }
        while (state->chunkIndex.front() < current - kBehind) {
            for (const EntityHandle entity : state->chunks.front()) world.destroy(entity);
            state->destroyed += 1;
            state->chunks.erase(state->chunks.begin());
            state->chunkIndex.erase(state->chunkIndex.begin());
        }
        state->peakEntities = std::max<uint32_t>(state->peakEntities, static_cast<uint32_t>(world.aliveCount()));
        state->peakChunks = std::max<uint32_t>(state->peakChunks, static_cast<uint32_t>(state->chunks.size()));
        state->maxTickMs = std::max(state->maxTickMs, nowMs() - startMs);
    });
    info.start = glm::vec3(0.0f, 2.0f, 0.0f);
    info.yaw = -90.0f;
    info.pitch = -3.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 600.0f;
    info.suggestedTicks = 1200;
    Engine* enginePointer = &engine;
    info.metrics = [enginePointer, state]() {
        TestSceneMetrics metrics;
        metrics.set("chunks_created", state->created);
        metrics.set("chunks_destroyed", state->destroyed);
        metrics.set("chunks_resident", static_cast<double>(state->chunks.size()));
        metrics.set("peak_chunks", state->peakChunks);
        metrics.set("peak_entities", state->peakEntities);
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        metrics.set("route_travelled_m", state->travelled);
        metrics.set("spawn_ms_total", state->spawnMs);
        metrics.set("max_stream_tick_ms", state->maxTickMs);
        return metrics;
    };
    return true;
}

struct CrowdState {
    NavMesh mesh;
    std::vector<NavAgent> agents;
    std::vector<EntityHandle> entities;
    Pcg32 rng{5u, 17u};
    double stepMs = 0.0;
    double maxStepMs = 0.0;
    uint32_t ticks = 0;
    uint32_t retargets = 0;
    uint32_t arrivals = 0;
    float distance = 0.0f;
};

constexpr int kAgents = 1300;
constexpr float kHalf = 40.0f;

bool buildCrowd(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    World& world = engine.world();
    const uint32_t ground = makeMaterial(kit, glm::vec3(0.3f, 0.3f, 0.32f), 0.9f);
    spawn(world, "crowd ground", kit.cube, ground, glm::vec3(-kHalf, -1.0f, -kHalf), glm::vec3(kHalf * 2.0f, 1.0f, kHalf * 2.0f), kit.floorShader);
    auto state = std::make_shared<CrowdState>();
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(kHalf, 0.5f, kHalf));
    NavConfig config;
    config.cellSize = 0.5f;
    config.originX = -kHalf;
    config.originZ = -kHalf;
    state->mesh.configure(config);
    state->mesh.setGeometry(soup);
    state->mesh.rebuildAll();
    for (int agentIndex = 0; agentIndex < kAgents; ++agentIndex) {
        NavAgent agent;
        agent.position = glm::vec3(state->rng.range(-kHalf + 2.0f, kHalf - 2.0f), 0.0f, state->rng.range(-kHalf + 2.0f, kHalf - 2.0f));
        agent.maxSpeed = state->rng.range(1.5f, 3.5f);
        const glm::vec3 hue(state->rng.range(0.2f, 1.0f), state->rng.range(0.2f, 1.0f), state->rng.range(0.2f, 1.0f));
        state->agents.push_back(agent);
        const EntityHandle body = spawn(world, "agent", kit.sphere, makeMaterial(kit, hue, 0.5f), agent.position + glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.5f, 0.9f, 0.5f));
        world.setTag(body, "observed");
        state->entities.push_back(body);
    }
    engine.addSystem(Phase::Update, "testscene.crowd", [state](World& world, float deltaTime) {
        const double startMs = nowMs();
        const float time = static_cast<float>(world.time().elapsed);
        std::vector<std::pair<EntityHandle, glm::vec3>> moves;
        moves.reserve(state->agents.size());
        for (size_t index = 0; index < state->agents.size(); ++index) {
            NavAgent& agent = state->agents[index];
            if (!agent.hasTarget || agent.state == AgentState::Arrived || agent.state == AgentState::Failed) {
                if (agent.state == AgentState::Arrived) ++state->arrivals;
                navSetTarget(state->mesh, agent, glm::vec3(state->rng.range(-kHalf + 2.0f, kHalf - 2.0f), 0.0f, state->rng.range(-kHalf + 2.0f, kHalf - 2.0f)));
                ++state->retargets;
            }
            const glm::vec3 before = agent.position;
            navStepAgent(state->mesh, agent, deltaTime);
            state->distance += glm::length(agent.position - before);
            const float bob = 0.5f + 0.08f * std::sin(time * 8.0f + static_cast<float>(index));
            moves.emplace_back(state->entities[index], agent.position + glm::vec3(0.0f, bob, 0.0f));
        }
        world.setPositions(moves);
        const double elapsedMs = nowMs() - startMs;
        state->stepMs += elapsedMs;
        state->maxStepMs = std::max(state->maxStepMs, elapsedMs);
        ++state->ticks;
    });
    info.start = glm::vec3(0.0f, 28.0f, 40.0f);
    info.yaw = -90.0f;
    info.pitch = -40.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 400.0f;
    info.suggestedTicks = 600;
    Engine* enginePointer = &engine;
    info.metrics = [enginePointer, state]() {
        TestSceneMetrics metrics;
        metrics.set("agents", static_cast<double>(state->agents.size()));
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        metrics.set("navmesh_tiles", static_cast<double>(state->mesh.tileCount()));
        metrics.set("retargets", state->retargets);
        metrics.set("arrivals", state->arrivals);
        metrics.set("distance_walked_m", state->distance);
        metrics.set("agent_step_ms_mean", state->ticks ? state->stepMs / state->ticks : 0.0);
        metrics.set("agent_step_ms_max", state->maxStepMs);
        return metrics;
    };
    return true;
}

struct VistaState {
    float farthest = 0.0f;
    uint32_t objects = 0;
};

bool buildVista(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    World& world = engine.world();
    auto state = std::make_shared<VistaState>();
    Pcg32 rng(77u, 3u);
    auto track = [&](const glm::vec3& position) {
        ++state->objects;
        state->farthest = std::max(state->farthest, glm::length(glm::vec2(position.x, position.z)));
    };
    const uint32_t ground = makeMaterial(kit, glm::vec3(0.28f, 0.32f, 0.22f), 0.95f);
    const uint32_t slab = makeMaterial(kit, glm::vec3(0.8f, 0.3f, 0.2f), 0.8f);
    const uint32_t slabAlt = makeMaterial(kit, glm::vec3(0.2f, 0.4f, 0.8f), 0.8f);
    const uint32_t stone = makeMaterial(kit, glm::vec3(0.55f, 0.5f, 0.46f), 0.7f);
    const uint32_t rock = makeMaterial(kit, glm::vec3(0.4f, 0.42f, 0.48f), 0.9f);
    const uint32_t beacon = makeMaterial(kit, glm::vec3(0.2f, 0.2f, 0.2f), 0.5f, glm::vec3(1.0f, 0.6f, 0.2f), 20000.0f);
    spawn(world, "vista ground", kit.cube, ground, glm::vec3(-200000.0f, -2.0f, -200000.0f), glm::vec3(400000.0f, 2.0f, 400000.0f), kit.floorShader);
    track(glm::vec3(200000.0f, 0.0f, 200000.0f));
    for (int row = 1; row <= 80; ++row) {
        const float z = -static_cast<float>(row) * 250.0f;
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec3 position(side * 30.0f, 0.0f, z);
            spawn(world, "vista pillar", kit.cube, stone, position - glm::vec3(1.5f, 0.0f, 1.5f), glm::vec3(3.0f, 14.0f + static_cast<float>(row) * 0.6f, 3.0f));
            track(position);
        }
    }
    for (int peak = 0; peak < 90; ++peak) {
        const float angle = rng.range(0.0f, 6.2831853f);
        const float distance = rng.range(3000.0f, 60000.0f);
        const float radius = rng.range(300.0f, 4200.0f) * (0.4f + distance / 60000.0f);
        const glm::vec3 position(std::cos(angle) * distance, 0.0f, std::sin(angle) * distance);
        spawn(world, "vista peak", kit.sphere, rock, position - glm::vec3(0.0f, radius * 0.25f, 0.0f), glm::vec3(radius * 2.0f, radius * rng.range(0.8f, 1.6f), radius * 2.0f));
        track(position);
    }
    for (int beaconIndex = 0; beaconIndex < 12; ++beaconIndex) {
        const float z = -(2000.0f + 3500.0f * static_cast<float>(beaconIndex));
        const glm::vec3 position(rng.range(-600.0f, 600.0f), 0.0f, z);
        spawn(world, "vista beacon", kit.sphere, beacon, position + glm::vec3(0.0f, 60.0f, 0.0f), glm::vec3(40.0f));
        track(position);
    }
    for (const float distance : {400.0f, 1500.0f, 6000.0f, 20000.0f}) {
        const float size = distance * 0.04f;
        const float x = -distance * 0.1f;
        spawn(world, "vista slab a", kit.cube, slab, glm::vec3(x, 0.0f, -distance), glm::vec3(size, size * 0.25f, size));
        spawn(world, "vista slab b", kit.cube, slabAlt, glm::vec3(x + size * 0.5f, 0.0f, -distance - size * 0.5f), glm::vec3(size, size * 0.25f, size));
        track(glm::vec3(x, 0.0f, -distance));
    }
    info.start = glm::vec3(0.0f, 60.0f, 0.0f);
    info.yaw = -90.0f;
    info.pitch = 2.0f;
    info.nearPlane = 0.5f;
    info.farPlane = 400000.0f;
    info.suggestedTicks = 120;
    Engine* enginePointer = &engine;
    info.metrics = [enginePointer, state, info]() {
        TestSceneMetrics metrics;
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        metrics.set("vista_objects", state->objects);
        metrics.set("farthest_object_m", state->farthest);
        metrics.set("camera_near_m", info.nearPlane);
        metrics.set("camera_far_m", info.farPlane);
        for (const float probeDistance : {100.0f, 1000.0f, 10000.0f, 50000.0f}) {
            char key[kKeyBufferSize];
            std::snprintf(key, sizeof(key), "depth_step_m_at_%.0f", probeDistance);
            metrics.set(key, forwardDepthStep(info.nearPlane, info.farPlane, probeDistance));
        }
        return metrics;
    };
    return true;
}

}

namespace {

constexpr const char* kStateMarker = "tools.testscene.state";
constexpr const char* kFloatingOriginSystem = "floating_origin";
constexpr const char* kIkDebugSystem = "ik test debug";

std::unordered_map<const Engine*, std::shared_ptr<TestSceneShared>>& sharedRegistry() {
    static std::unordered_map<const Engine*, std::shared_ptr<TestSceneShared>> registry;
    return registry;
}

bool hasSystem(Engine& engine, Phase phase, const char* name) {
    for (const std::string& system : engine.systemNames(phase)) {
        if (system == name) return true;
    }
    return false;
}

// portal_tilt (docs/Portals.md): hub with a wall-to-floor, a ceiling, a 45 degree rolled and a 180 degree flipped portal. The camera
// stands still unless LIT_TILT_WALK names a portal (floor, ceiling, rolled, flip): then it starts 2.5 m before that portal's hub end
// looking at its centre and moves along its view direction at 1.5 m/s (Engine::updatePortals crosses and turns the camera upright).
struct TiltWalk {
    int cell = -1;
    uint32_t crossings = 0;
    bool active = false;
};

bool buildTilt(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    PortalSystem& portals = engine.portals();
    buildPortalScene(portals, engine.world(), testScenePortalKit(engine, kit), PortalTestScene::Tilt);
    info.start = portalSceneStart(PortalTestScene::Tilt, info.yaw, info.pitch);
    info.nearPlane = 0.05f;
    info.farPlane = 300.0f;
    info.suggestedTicks = 240;
    // LIT_PORTAL_SPILL=<scale>: scale the portals' light spill (0 = off), to compare frames with and without it.
    if (const char* spill = std::getenv("LIT_PORTAL_SPILL")) portals.settings().spill = std::strtof(spill, nullptr);
    auto walk = std::make_shared<TiltWalk>();
    if (const char* name = std::getenv("LIT_TILT_WALK")) {
        for (const Portal& portal : portals.portals()) {
            if (portal.name != std::string(name) + ".a") continue;
            constexpr float kRunUp = 2.5f;
            constexpr float kMaxStartPitch = 75.0f;
            const glm::vec3 d = -portal.normal();
            info.yaw = std::abs(d.y) > 0.9f ? -90.0f : glm::degrees(std::atan2(d.z, d.x));
            info.pitch = std::clamp(glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f))), -kMaxStartPitch, kMaxStartPitch);
            const float yawRadians = glm::radians(info.yaw);
            const float pitchRadians = glm::radians(info.pitch);
            const glm::vec3 view(std::cos(yawRadians) * std::cos(pitchRadians), std::sin(pitchRadians), std::sin(yawRadians) * std::cos(pitchRadians));
            info.start = portal.center() - view * (kRunUp / std::max(glm::dot(view, d), 0.2f));
            walk->active = true;
        }
    }
    Engine* enginePointer = &engine;
    constexpr float kTiltWalkSpeed = 1.5f;
    engine.addSystem(Phase::Update, "testscene.tilt", [walk, enginePointer](World& world, float deltaTime) {
        const int cell = enginePointer->portals().cameraCell();
        if (walk->cell >= 0 && cell != walk->cell) ++walk->crossings;
        walk->cell = cell;
        if (!walk->active) return;
        // Along the view: a crossing turns the view with the portal transform, the upright relax only rolls it.
        Camera& camera = world.camera();
        camera.setPos(camera.getPosition() + camera.getFront() * kTiltWalkSpeed * deltaTime);
    });
    info.metrics = [enginePointer, walk]() {
        TestSceneMetrics metrics;
        metrics.set("cells", static_cast<double>(enginePointer->portals().cells().size()));
        metrics.set("portals", static_cast<double>(enginePointer->portals().portals().size()));
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        metrics.set("portal_crossings", walk->crossings);
        return metrics;
    };
    return true;
}

}

// An Engine at a recycled address must not inherit the bookkeeping of a dead one: the marker system lives and dies with the Engine.
TestSceneShared& testSceneShared(Engine& engine) {
    auto& entry = sharedRegistry()[&engine];
    if (!entry || !hasSystem(engine, Phase::PreUpdate, kStateMarker)) {
        entry = std::make_shared<TestSceneShared>();
        engine.addSystem(Phase::PreUpdate, kStateMarker, [](World&, float) {}, true);
    }
    return *entry;
}

uint32_t testSceneMaterial(Engine& engine, const TestSceneKit& kit, const Material& material) {
    if (kit.materialFull) return kit.materialFull(material);
    return kit.material ? kit.material(material.baseColor, material.emissive, material.emissiveIntensity, material.roughness) : 0u;
}

uint32_t testSceneMesh(Engine& engine, const std::string& key, const std::function<uint32_t()>& upload) {
    TestSceneShared& shared = testSceneShared(engine);
    if (const auto it = shared.meshes.find(key); it != shared.meshes.end()) return it->second;
    const uint32_t id = upload();
    shared.meshes[key] = id;
    return id;
}

uint32_t testSceneDynamicSource(Engine& engine, const std::string& key, const DynamicMesh& mesh) {
    TestSceneShared& shared = testSceneShared(engine);
    if (const auto it = shared.dynamicSources.find(key); it != shared.dynamicSources.end()) return it->second;
    const uint32_t id = engine.createDynamicSource(mesh);
    if (id != 0xFFFFFFFFu) shared.dynamicSources[key] = id;
    return id;
}

uint32_t testSceneDynamicInstance(Engine& engine, uint32_t source) {
    if (source == 0xFFFFFFFFu) return 0xFFFFFFFFu;
    TestSceneShared& shared = testSceneShared(engine);
    auto& free = shared.freeInstances[source];
    uint32_t instance = 0xFFFFFFFFu;
    if (!free.empty()) {
        instance = free.back();
        free.pop_back();
    } else {
        instance = engine.createDynamicInstance(source);
    }
    if (instance != 0xFFFFFFFFu) shared.usedInstances.emplace_back(source, instance);
    return instance;
}

std::vector<std::string> testSceneNames() {
    return {"portal_loop", "portal_torture", "portal_tilt", "streaming_route", "crowd", "vista", "skinning", "terrain", "showcase", "probes", "light_stress", "largeworld", "vsm_mixed", "dense", "ik", "gallery", "textures", "instancing"};
}

void clearTestSceneSystems(Engine& engine) {
    for (const Phase phase : {Phase::PreUpdate, Phase::FixedUpdate, Phase::Update, Phase::PostUpdate}) {
        for (const std::string& name : engine.systemNames(phase)) {
            if (name.starts_with("testscene.")) engine.removeSystem(name);
        }
    }
}

void leaveTestScene(Engine& engine) {
    clearTestSceneSystems(engine);
    engine.removeSystem(kIkDebugSystem);
    engine.portals().clear();
    // Terrain streamer (terrain scene): stop it and hand every resident tile's GPU meshes back (the renderer has 4096 mesh slots).
    if (TerrainStreamer* terrain = engine.terrain()) {
        engine.removeSystem("terrain");
        TerrainTileLoader& loader = terrain->loader();
        StreamBudget budget;
        budget.unlimited = true;
        // The streamer numbers its tile cells 1, 2, 3, ... and the loader only releases a tile through its cell id (no key -> cell lookup without
        // a DeltaStore), so walk the ids until no GPU mesh is left.
        constexpr int kMaxCells = 1 << 16;
        const size_t before = loader.meshCount();
        for (int cell = 1; cell < kMaxCells && loader.meshCount() > 0; ++cell) loader.unload(cell, budget);
        const size_t unloaded = before - loader.meshCount();
        const size_t resident = before;
        if (before > 0) Lit::Log::Info("Test scene terrain released: {} of {} GPU meshes, {} left", unloaded, resident, loader.meshCount());
    }
    TestSceneShared& shared = testSceneShared(engine);
    for (const auto& [source, instance] : shared.usedInstances) shared.freeInstances[source].push_back(instance);
    shared.usedInstances.clear();
    shared.materialCursor = 0;
    resetLargeWorldState(engine);
    if (hasSystem(engine, Phase::PostUpdate, kFloatingOriginSystem)) {
        FloatingOriginConfig off;
        off.enabled = false;
        engine.enableFloatingOrigin(off);
        engine.removeSystem(kFloatingOriginSystem);
    }
    if (const glm::dvec3 origin = engine.worldOrigin(); origin != glm::dvec3(0.0)) engine.rebaseOrigin(-origin);
    if (shared.settingsSaved) {
        RenderSettings& settings = engine.renderSettings();
        settings.timeOfDay = shared.timeOfDay;
        settings.sunAzimuth = shared.sunAzimuth;
        settings.cloudCoverage = shared.cloudCoverage;
        settings.cloudSpeed = shared.cloudSpeed;
        settings.clouds = shared.clouds;
        settings.fog = shared.fog;
        settings.reflectionMode = static_cast<ReflectionMode>(shared.reflectionMode);
    }
}

TestSceneKit makeTestSceneKit(Engine& engine, uint32_t floorShader) {
    TestSceneKit kit;
    if (!engine.headless()) {
        kit.cube = engine.loadMesh("cube");
        kit.sphere = engine.loadMesh("sphere");
    }
    kit.floorShader = floorShader;
    auto pooled = [&engine](Material material) {
        if (engine.headless()) return 0u;
        TestSceneShared& shared = testSceneShared(engine);
        if (material.name.empty()) material.name = "Test scene";
        if (shared.materialCursor < shared.materialSlots.size()) {
            const uint32_t id = shared.materialSlots[shared.materialCursor++];
            engine.setMaterial(id, material);
            return id;
        }
        const uint32_t id = engine.createMaterial(material);
        shared.materialSlots.push_back(id);
        ++shared.materialCursor;
        return id;
    };
    kit.materialFull = pooled;
    kit.material = [pooled](const glm::vec3& color, const glm::vec3& emissive, float intensity, float roughness) {
        Material material;
        material.baseColor = color;
        material.roughness = roughness;
        material.emissive = emissive;
        material.emissiveIntensity = intensity;
        return pooled(material);
    };
    return kit;
}

namespace {

bool buildIkRegistry(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    IkTestKit ikKit;
    ikKit.cube = kit.cube;
    ikKit.sphere = kit.sphere;
    ikKit.debugDraw = true;
    ikKit.material = [&engine, kit](const Material& material) { return testSceneMaterial(engine, kit, material); };
    buildIkTestScene(engine, ikKit);
    info.start = glm::vec3(0.0f, 6.5f, 22.0f);
    info.yaw = -90.0f;
    info.pitch = -17.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 400.0f;
    info.suggestedTicks = 300;
    return true;
}

bool buildGalleryRegistry(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    GalleryKit galleryKit;
    galleryKit.cube = kit.cube;
    galleryKit.sphere = kit.sphere;
    galleryKit.material = [&engine, kit](const Material& material) { return testSceneMaterial(engine, kit, material); };
    buildGalleryScene(engine, galleryKit);
    info.start = glm::vec3(0.0f, 5.2f, 17.5f);
    info.yaw = -90.0f;
    info.pitch = -11.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 400.0f;
    info.suggestedTicks = 120;
    return true;
}

}

bool buildTestScene(Engine& engine, const std::string& name, const TestSceneKit& kit, TestSceneInfo& info) { return buildTestScene(engine, name, kit, info, TestSceneParams{}); }

bool buildTestScene(Engine& engine, const std::string& name, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params) {
    const std::vector<std::string> names = testSceneNames();
    if (std::ranges::find(names, name) == names.end()) return false;
    leaveTestScene(engine);
    TestSceneShared& shared = testSceneShared(engine);
    if (!shared.settingsSaved) {
        const RenderSettings& settings = engine.renderSettings();
        shared.timeOfDay = settings.timeOfDay;
        shared.sunAzimuth = settings.sunAzimuth;
        shared.cloudCoverage = settings.cloudCoverage;
        shared.cloudSpeed = settings.cloudSpeed;
        shared.clouds = settings.clouds;
        shared.fog = settings.fog;
        shared.reflectionMode = static_cast<int>(settings.reflectionMode);
        shared.settingsSaved = true;
    }
    info = TestSceneInfo{};
    info.name = name;
    bool built = false;
    if (name == "portal_loop") built = buildLoop(engine, kit, info);
    else if (name == "portal_torture") built = buildTorture(engine, kit, info);
    else if (name == "portal_tilt") built = buildTilt(engine, kit, info);
    else if (name == "streaming_route") built = buildStreaming(engine, kit, info);
    else if (name == "crowd") built = buildCrowd(engine, kit, info);
    else if (name == "vista") built = buildVista(engine, kit, info);
    else if (name == "skinning") built = buildSkinningTestScene(engine, kit, info);
    else if (name == "terrain") built = buildTerrainTestScene(engine, kit, info);
    else if (name == "showcase") built = buildShowcaseTestScene(engine, kit, info, params);
    else if (name == "probes") built = buildProbesTestScene(engine, kit, info, params);
    else if (name == "textures") built = buildTexturesTestScene(engine, kit, info, params);
    else if (name == "light_stress") built = buildLightStressTestScene(engine, kit, info, params);
    else if (name == "largeworld") built = buildLargeWorldTestScene(engine, kit, info, params);
    else if (name == "vsm_mixed") built = buildMixedTestScene(engine, kit, info, params);
    else if (name == "dense") built = buildDenseTestScene(engine, kit, info, params);
    else if (name == "instancing") built = buildInstancingTestScene(engine, kit, info);
    else if (name == "ik") built = buildIkRegistry(engine, kit, info);
    else if (name == "gallery") built = buildGalleryRegistry(engine, kit, info);
    if (built && !info.metrics) {
        Engine* enginePointer = &engine;
        info.metrics = [enginePointer]() {
            TestSceneMetrics metrics;
            metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
            return metrics;
        };
    }
    return built;
}

void applyTestSceneCamera(Engine& engine, const TestSceneInfo& info) {
    Camera& camera = engine.world().camera();
    camera.setPos(info.start);
    camera.setOrientation(info.yaw, info.pitch);
    camera.setNearPlane(info.nearPlane);
    camera.setFarPlane(info.farPlane);
    if (engine.portals().active()) engine.portals().setCameraCell(engine.portals().cellAt(info.start));
}
