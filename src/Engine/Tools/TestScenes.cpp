module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

void TestSceneMetrics::set(const std::string& name, double value) {
    for (auto& [key, v] : values) {
        if (key == name) {
            v = value;
            return;
        }
    }
    values.emplace_back(name, value);
}

double TestSceneMetrics::get(const std::string& name, double fallback) const {
    for (const auto& [key, v] : values) {
        if (key == name) return v;
    }
    return fallback;
}

std::string TestSceneMetrics::toJson() const {
    std::string out = "{";
    char buffer[64];
    for (size_t i = 0; i < values.size(); ++i) {
        std::snprintf(buffer, sizeof(buffer), "%.6g", values[i].second);
        out += (i ? ", \"" : "\"") + values[i].first + "\": " + buffer;
    }
    return out + "}";
}

double forwardDepthStep(float nearPlane, float farPlane, float distance) {
    const double n = nearPlane;
    const double f = farPlane;
    const double d = f * (distance - n) / (distance * (f - n));
    const double ulp = std::nextafter(static_cast<float>(d), 2.0f) - static_cast<float>(d);
    const double slope = f * n / (distance * distance * (f - n));
    return ulp / slope;
}

namespace {

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

uint32_t makeMaterial(const TestSceneKit& kit, const glm::vec3& color, float roughness = 0.7f, const glm::vec3& emissive = glm::vec3(0.0f), float intensity = 0.0f) {
    return kit.material ? kit.material(color, emissive, intensity, roughness) : 0u;
}

EntityHandle spawn(World& world, const std::string& name, uint32_t mesh, uint32_t material, const glm::vec3& position, const glm::vec3& scale, uint32_t shader = 0) {
    EntityDesc desc;
    desc.name = name;
    desc.mesh = mesh;
    desc.material = material;
    desc.shader = shader;
    desc.position = position;
    desc.scale = scale;
    return world.create(desc);
}

struct TortureState {
    uint32_t ticks = 0;
    uint32_t crossings = 0;
    uint32_t maxNodes = 0;
    uint32_t maxDepth = 0;
    double frameMs = 0.0;
    double maxFrameMs = 0.0;
};

bool buildTorture(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    PortalSceneKit pk;
    pk.cube = kit.cube;
    pk.sphere = kit.sphere;
    pk.floorShader = kit.floorShader;
    pk.material = kit.material;
    buildPortalScene(engine.portals(), engine.world(), pk, PortalTestScene::Torture);
    info.start = portalSceneStart(PortalTestScene::Torture, info.yaw, info.pitch);
    info.nearPlane = 0.05f;
    info.farPlane = 300.0f;
    info.suggestedTicks = 600;
    auto state = std::make_shared<TortureState>();
    Engine* e = &engine;
    engine.addSystem(Phase::Update, "testscene.torture", [e, state](World& world, float dt) {
        Camera& camera = world.camera();
        const glm::vec3 forward(std::cos(glm::radians(camera.getYaw())), 0.0f, std::sin(glm::radians(camera.getYaw())));
        camera.setPos(camera.getPosition() + forward * 1.5f * dt);
        if (e->portals().step(camera) >= 0) ++state->crossings;
        const double t0 = nowMs();
        const PortalFrame frame = e->portals().buildFrame(camera, 1080.0f);
        const double ms = nowMs() - t0;
        ++state->ticks;
        state->frameMs += ms;
        state->maxFrameMs = std::max(state->maxFrameMs, ms);
        state->maxNodes = std::max<uint32_t>(state->maxNodes, static_cast<uint32_t>(frame.nodes.size()));
        for (const PortalViewNode& n : frame.nodes) state->maxDepth = std::max<uint32_t>(state->maxDepth, static_cast<uint32_t>(n.depth));
    });
    Engine* ep = &engine;
    info.metrics = [ep, state]() {
        TestSceneMetrics m;
        m.set("cells", static_cast<double>(ep->portals().cells().size()));
        m.set("portals", static_cast<double>(ep->portals().portals().size()));
        m.set("entities", static_cast<double>(ep->world().aliveCount()));
        m.set("portal_crossings", state->crossings);
        m.set("max_view_nodes", state->maxNodes);
        m.set("max_view_depth", state->maxDepth);
        m.set("frame_build_ms_mean", state->ticks ? state->frameMs / state->ticks : 0.0);
        m.set("frame_build_ms_max", state->maxFrameMs);
        return m;
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

bool buildStreaming(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    World& world = engine.world();
    constexpr float kChunkLength = 40.0f;
    constexpr int kAhead = 6;
    constexpr int kBehind = 2;
    constexpr int kObjectsPerChunk = 48;
    constexpr float kSpeed = 14.0f;
    constexpr float kWidth = 30.0f;
    const uint32_t ground = makeMaterial(kit, glm::vec3(0.35f, 0.38f, 0.3f), 0.9f);
    const uint32_t stone = makeMaterial(kit, glm::vec3(0.6f, 0.58f, 0.55f), 0.6f);
    const uint32_t glow = makeMaterial(kit, glm::vec3(0.2f, 0.3f, 0.6f), 0.3f, glm::vec3(0.3f, 0.5f, 1.0f), 3000.0f);
    auto state = std::make_shared<StreamState>();
    auto routeX = [](float z) { return 12.0f * std::sin(z * 0.02f); };
    auto spawnChunk = [&world, kit, state, routeX, ground, stone, glow](int index) {
        const double t0 = nowMs();
        Pcg32 rng(static_cast<uint64_t>(index + 1000) * 7919u, 11u);
        std::vector<EntityHandle> items;
        const float z0 = -static_cast<float>(index) * kChunkLength;
        items.push_back(spawn(world, "chunk ground", kit.cube, ground, glm::vec3(routeX(z0 - kChunkLength * 0.5f) - kWidth, -1.0f, z0 - kChunkLength), glm::vec3(kWidth * 2.0f, 1.0f, kChunkLength + 0.5f), kit.floorShader));
        for (int i = 0; i < kObjectsPerChunk; ++i) {
            const float z = z0 - rng.range(0.0f, kChunkLength);
            const float side = rng.chance(0.5f) ? 1.0f : -1.0f;
            const float x = routeX(z) + side * rng.range(4.0f, kWidth);
            const float h = rng.range(0.5f, 6.0f);
            const bool orb = rng.chance(0.25f);
            items.push_back(spawn(world, orb ? "chunk orb" : "chunk pillar", orb ? kit.sphere : kit.cube, orb ? glow : stone, glm::vec3(x, orb ? h : 0.0f, z), orb ? glm::vec3(1.0f) : glm::vec3(rng.range(0.6f, 2.0f), h, rng.range(0.6f, 2.0f))));
        }
        state->created += 1;
        state->spawnMs += nowMs() - t0;
        return items;
    };
    state->firstChunk = 0;
    for (int i = -kBehind; i <= kAhead; ++i) {
        state->chunks.push_back(spawnChunk(i));
        state->chunkIndex.push_back(i);
    }
    state->firstChunk = -kBehind;
    engine.addSystem(Phase::Update, "testscene.stream", [state, spawnChunk](World& w, float dt) {
        const double t0 = nowMs();
        state->travelled += kSpeed * dt;
        Camera& camera = w.camera();
        const float z = -state->travelled;
        camera.setPos(glm::vec3(12.0f * std::sin(z * 0.02f), 2.0f, z));
        camera.setOrientation(glm::degrees(std::atan2(-1.0f, -0.24f * std::cos(z * 0.02f))), -3.0f);
        const int current = static_cast<int>(state->travelled / kChunkLength);
        while (state->chunkIndex.back() < current + kAhead) {
            const int next = state->chunkIndex.back() + 1;
            state->chunks.push_back(spawnChunk(next));
            state->chunkIndex.push_back(next);
        }
        while (state->chunkIndex.front() < current - kBehind) {
            for (EntityHandle e : state->chunks.front()) w.destroy(e);
            state->destroyed += 1;
            state->chunks.erase(state->chunks.begin());
            state->chunkIndex.erase(state->chunkIndex.begin());
        }
        state->peakEntities = std::max<uint32_t>(state->peakEntities, static_cast<uint32_t>(w.aliveCount()));
        state->peakChunks = std::max<uint32_t>(state->peakChunks, static_cast<uint32_t>(state->chunks.size()));
        state->maxTickMs = std::max(state->maxTickMs, nowMs() - t0);
    });
    info.start = glm::vec3(0.0f, 2.0f, 0.0f);
    info.yaw = -90.0f;
    info.pitch = -3.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 600.0f;
    info.suggestedTicks = 1200;
    Engine* ep = &engine;
    info.metrics = [ep, state]() {
        TestSceneMetrics m;
        m.set("chunks_created", state->created);
        m.set("chunks_destroyed", state->destroyed);
        m.set("chunks_resident", static_cast<double>(state->chunks.size()));
        m.set("peak_chunks", state->peakChunks);
        m.set("peak_entities", state->peakEntities);
        m.set("entities", static_cast<double>(ep->world().aliveCount()));
        m.set("route_travelled_m", state->travelled);
        m.set("spawn_ms_total", state->spawnMs);
        m.set("max_stream_tick_ms", state->maxTickMs);
        return m;
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

bool buildCrowd(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    World& world = engine.world();
    constexpr int kAgents = 500;
    constexpr float kHalf = 40.0f;
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
    for (int i = 0; i < kAgents; ++i) {
        NavAgent agent;
        agent.position = glm::vec3(state->rng.range(-kHalf + 2.0f, kHalf - 2.0f), 0.0f, state->rng.range(-kHalf + 2.0f, kHalf - 2.0f));
        agent.maxSpeed = state->rng.range(1.5f, 3.5f);
        const glm::vec3 hue(state->rng.range(0.2f, 1.0f), state->rng.range(0.2f, 1.0f), state->rng.range(0.2f, 1.0f));
        state->agents.push_back(agent);
        const EntityHandle body = spawn(world, "agent", kit.sphere, makeMaterial(kit, hue, 0.5f), agent.position + glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.5f, 0.9f, 0.5f));
        world.setTag(body, "observed");
        state->entities.push_back(body);
    }
    engine.addSystem(Phase::Update, "testscene.crowd", [state](World& w, float dt) {
        const double t0 = nowMs();
        const float time = static_cast<float>(w.time().elapsed);
        std::vector<std::pair<EntityHandle, glm::vec3>> moves;
        moves.reserve(state->agents.size());
        for (size_t i = 0; i < state->agents.size(); ++i) {
            NavAgent& a = state->agents[i];
            if (!a.hasTarget || a.state == AgentState::Arrived || a.state == AgentState::Failed) {
                if (a.state == AgentState::Arrived) ++state->arrivals;
                navSetTarget(state->mesh, a, glm::vec3(state->rng.range(-kHalf + 2.0f, kHalf - 2.0f), 0.0f, state->rng.range(-kHalf + 2.0f, kHalf - 2.0f)));
                ++state->retargets;
            }
            const glm::vec3 before = a.position;
            navStepAgent(state->mesh, a, dt);
            state->distance += glm::length(a.position - before);
            const float bob = 0.5f + 0.08f * std::sin(time * 8.0f + static_cast<float>(i));
            moves.emplace_back(state->entities[i], a.position + glm::vec3(0.0f, bob, 0.0f));
        }
        w.setPositions(moves);
        const double ms = nowMs() - t0;
        state->stepMs += ms;
        state->maxStepMs = std::max(state->maxStepMs, ms);
        ++state->ticks;
    });
    info.start = glm::vec3(0.0f, 28.0f, 40.0f);
    info.yaw = -90.0f;
    info.pitch = -40.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 400.0f;
    info.suggestedTicks = 600;
    Engine* ep = &engine;
    info.metrics = [ep, state]() {
        TestSceneMetrics m;
        m.set("agents", static_cast<double>(state->agents.size()));
        m.set("entities", static_cast<double>(ep->world().aliveCount()));
        m.set("navmesh_tiles", static_cast<double>(state->mesh.tileCount()));
        m.set("retargets", state->retargets);
        m.set("arrivals", state->arrivals);
        m.set("distance_walked_m", state->distance);
        m.set("agent_step_ms_mean", state->ticks ? state->stepMs / state->ticks : 0.0);
        m.set("agent_step_ms_max", state->maxStepMs);
        return m;
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
    auto track = [&](const glm::vec3& p) {
        ++state->objects;
        state->farthest = std::max(state->farthest, glm::length(glm::vec2(p.x, p.z)));
    };
    const uint32_t ground = makeMaterial(kit, glm::vec3(0.28f, 0.32f, 0.22f), 0.95f);
    const uint32_t slab = makeMaterial(kit, glm::vec3(0.8f, 0.3f, 0.2f), 0.8f);
    const uint32_t slabAlt = makeMaterial(kit, glm::vec3(0.2f, 0.4f, 0.8f), 0.8f);
    const uint32_t stone = makeMaterial(kit, glm::vec3(0.55f, 0.5f, 0.46f), 0.7f);
    const uint32_t rock = makeMaterial(kit, glm::vec3(0.4f, 0.42f, 0.48f), 0.9f);
    const uint32_t beacon = makeMaterial(kit, glm::vec3(0.2f, 0.2f, 0.2f), 0.5f, glm::vec3(1.0f, 0.6f, 0.2f), 20000.0f);
    spawn(world, "vista ground", kit.cube, ground, glm::vec3(-200000.0f, -2.0f, -200000.0f), glm::vec3(400000.0f, 2.0f, 400000.0f), kit.floorShader);
    track(glm::vec3(200000.0f, 0.0f, 200000.0f));
    for (int i = 1; i <= 80; ++i) {
        const float z = -static_cast<float>(i) * 250.0f;
        for (float side : {-1.0f, 1.0f}) {
            const glm::vec3 p(side * 30.0f, 0.0f, z);
            spawn(world, "vista pillar", kit.cube, stone, p - glm::vec3(1.5f, 0.0f, 1.5f), glm::vec3(3.0f, 14.0f + static_cast<float>(i) * 0.6f, 3.0f));
            track(p);
        }
    }
    for (int i = 0; i < 90; ++i) {
        const float angle = rng.range(0.0f, 6.2831853f);
        const float distance = rng.range(3000.0f, 60000.0f);
        const float radius = rng.range(300.0f, 4200.0f) * (0.4f + distance / 60000.0f);
        const glm::vec3 p(std::cos(angle) * distance, 0.0f, std::sin(angle) * distance);
        spawn(world, "vista peak", kit.sphere, rock, p - glm::vec3(0.0f, radius * 0.25f, 0.0f), glm::vec3(radius * 2.0f, radius * rng.range(0.8f, 1.6f), radius * 2.0f));
        track(p);
    }
    for (int i = 0; i < 12; ++i) {
        const float z = -(2000.0f + 3500.0f * static_cast<float>(i));
        const glm::vec3 p(rng.range(-600.0f, 600.0f), 0.0f, z);
        spawn(world, "vista beacon", kit.sphere, beacon, p + glm::vec3(0.0f, 60.0f, 0.0f), glm::vec3(40.0f));
        track(p);
    }
    for (float distance : {400.0f, 1500.0f, 6000.0f, 20000.0f}) {
        const float size = distance * 0.04f;
        const float x = -distance * 0.1f;
        spawn(world, "vista slab a", kit.cube, slab, glm::vec3(x, 0.0f, -distance), glm::vec3(size, 0.0f + size * 0.25f, size));
        spawn(world, "vista slab b", kit.cube, slabAlt, glm::vec3(x + size * 0.5f, 0.0f, -distance - size * 0.5f), glm::vec3(size, size * 0.25f, size));
        track(glm::vec3(x, 0.0f, -distance));
    }
    info.start = glm::vec3(0.0f, 60.0f, 0.0f);
    info.yaw = -90.0f;
    info.pitch = 2.0f;
    info.nearPlane = 0.5f;
    info.farPlane = 400000.0f;
    info.suggestedTicks = 120;
    Engine* ep = &engine;
    info.metrics = [ep, state, info]() {
        TestSceneMetrics m;
        m.set("entities", static_cast<double>(ep->world().aliveCount()));
        m.set("vista_objects", state->objects);
        m.set("farthest_object_m", state->farthest);
        m.set("camera_near_m", info.nearPlane);
        m.set("camera_far_m", info.farPlane);
        for (float d : {100.0f, 1000.0f, 10000.0f, 50000.0f}) {
            char key[48];
            std::snprintf(key, sizeof(key), "depth_step_m_at_%.0f", d);
            m.set(key, forwardDepthStep(info.nearPlane, info.farPlane, d));
        }
        return m;
    };
    return true;
}

}  // namespace

std::vector<std::string> testSceneNames() { return {"portal_torture", "streaming_route", "crowd", "vista"}; }

void clearTestSceneSystems(Engine& engine) {
    for (Phase phase : {Phase::PreUpdate, Phase::FixedUpdate, Phase::Update, Phase::PostUpdate}) {
        for (const std::string& name : engine.systemNames(phase)) {
            if (name.starts_with("testscene.")) engine.removeSystem(name);
        }
    }
}

bool buildTestScene(Engine& engine, const std::string& name, const TestSceneKit& kit, TestSceneInfo& info) {
    clearTestSceneSystems(engine);
    info = TestSceneInfo{};
    info.name = name;
    if (name == "portal_torture") return buildTorture(engine, kit, info);
    if (name == "streaming_route") return buildStreaming(engine, kit, info);
    if (name == "crowd") return buildCrowd(engine, kit, info);
    if (name == "vista") return buildVista(engine, kit, info);
    return false;
}

void applyTestSceneCamera(Engine& engine, const TestSceneInfo& info) {
    Camera& camera = engine.world().camera();
    camera.setPos(info.start);
    camera.setOrientation(info.yaw, info.pitch);
    camera.setNearPlane(info.nearPlane);
    camera.setFarPlane(info.farPlane);
    if (engine.portals().active()) engine.portals().setCameraCell(engine.portals().cellAt(info.start));
}
