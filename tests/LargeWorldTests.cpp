#include "TestSupport.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.Portal;
import Engine.Streaming;
import Engine.Delta;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;
import Engine.Nav;
import Engine.Geometry;
import Engine.AudioTrace;
import Engine.Audio;
import Engine.Observation;
import Engine.Physics;
import Engine.Character;
import Engine.Save;

constexpr float kSpacing = 24.0f;

static void buildLine(PortalSystem& p, int count) {
    for (int i = 0; i < count; ++i) {
        PortalCell c;
        c.name = "C" + std::to_string(i);
        c.min = glm::vec3(kSpacing * static_cast<float>(i) - 5.4f, -0.4f, -5.4f);
        c.max = glm::vec3(kSpacing * static_cast<float>(i) + 5.4f, 4.4f, 5.4f);
        p.addCell(c);
    }
    auto end = [](const std::string& cell, float x, float yaw) {
        PortalEnd e;
        e.cell = cell;
        e.position = glm::vec3(x, 1.8f, 0.0f);
        e.eulerDegrees = glm::vec3(yaw, 0.0f, 0.0f);
        e.halfSize = glm::vec2(1.4f, 1.8f);
        return e;
    };
    for (int i = 0; i + 1 < count; ++i) {
        const float x = kSpacing * static_cast<float>(i);
        p.addPair("P" + std::to_string(i), end("C" + std::to_string(i), x + 5.0f, -90.0f), end("C" + std::to_string(i + 1), x + kSpacing - 5.0f, 90.0f));
    }
}

static void pump(StreamingSystem& streaming, int cell, const glm::vec3& position, int frames = 3000) {
    for (int i = 0; i < frames; ++i) {
        streaming.update(cell, position);
        if (streaming.settled()) return;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

struct LodLog {
    std::mutex mutex;
    std::map<int, int> lastLod;
};

static void testSpatialRings() {
    Engine engine;
    engine.openHeadless();
    constexpr float kCellSize = 100.0f;
    const int first = addGridCells(engine.portals(), engine.world(), glm::dvec3(0.0), kCellSize, 20, 1);
    CHECK(first == 0);
    StreamingConfig config;
    config.spatial.enabled = true;
    config.spatial.loadRadius = 150.0f;
    config.spatial.keepRadius = 250.0f;
    config.spatial.lodRadii = {100.0f};
    config.spatial.lodHysteresis = 10.0f;
    config.uploadBytesPerFrame = 1u << 30;
    config.commitMillisPerFrame = 50.0;
    StreamingSystem& streaming = engine.enableStreaming(config);
    auto log = std::make_shared<LodLog>();
    streaming.setLoader(makeBatchLoader(engine.world(), engine.portals(), nullptr, [log](int cell, int lod) {
        {
            std::lock_guard lock(log->mutex);
            log->lastLod[cell] = lod;
        }
        CellContent content;
        CellEntity e;
        e.key = "k";
        e.desc.name = "g" + std::to_string(cell);
        e.hasWorldPosition = true;
        e.worldPosition = glm::dvec3(static_cast<double>(cell) * 100.0 + 50.0, 0.0, 50.0);
        content.entities.push_back(e);
        return content;
    }));
    glm::vec3 camera(50.0f, 1.0f, 50.0f);
    streaming.prime(0, camera);
    CHECK(streaming.resident(0) && streaming.resident(1) && streaming.resident(2) && !streaming.resident(3));
    CHECK(streaming.lod(0) == 0 && streaming.lod(1) == 0 && streaming.lod(2) == 1);

    camera.x = 250.0f;
    pump(streaming, 2, camera);
    CHECK(streaming.resident(2) && streaming.lod(2) == 0);
    CHECK(streaming.stats().lodReloads > 0);
    camera.x = 450.0f;
    pump(streaming, 4, camera);
    CHECK(!streaming.resident(0));
    CHECK(streaming.resident(1));
    CHECK(streaming.resident(3) && streaming.resident(4) && streaming.resident(5) && streaming.resident(6));
    CHECK(!streaming.resident(7));
    CHECK(streaming.lod(4) == 0 && streaming.lod(6) == 1);
    CHECK(streaming.lod(1) == 0);
    CHECK(streaming.stats().lodReloads > 0);
    CHECK(engine.world().aliveCount() == streaming.residentCells().size());
}

static void testSpatialUnionWithPortals() {
    Engine engine;
    engine.openHeadless();
    buildLine(engine.portals(), 6);
    StreamingConfig config;
    config.loadHops = 1;
    config.keepHops = 2;
    config.spatial.enabled = true;
    config.spatial.loadRadius = 50.0f;
    config.spatial.keepRadius = 60.0f;
    config.uploadBytesPerFrame = 1u << 30;
    StreamingSystem& streaming = engine.enableStreaming(config);
    streaming.setLoader(makeBatchLoader(engine.world(), engine.portals(), nullptr, [](int cell) {
        CellContent content;
        CellEntity e;
        e.key = "k";
        e.desc.name = "c" + std::to_string(cell);
        content.entities.push_back(e);
        return content;
    }));
    engine.portals().setCameraCell(0);
    streaming.prime(0, glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(streaming.resident(0) && streaming.resident(1));
    CHECK(streaming.resident(2));
    CHECK(!streaming.resident(3));
    CHECK(streaming.graphDistances(0)[2] == 2);
}

struct FakeMesh {
    std::map<std::string, std::vector<int>> uploaded;
    std::vector<std::string> evicted;
};

static void testMeshResidency() {
    PortalSystem portals;
    StreamingConfig config;
    config.uploadBytesPerFrame = 1u << 30;
    StreamingSystem streaming(portals, config);
    MeshResidency residency(streaming, 250);
    FakeMesh fake;
    auto ask = [&](const std::string& key, int lod) {
        MeshStreamRequest r;
        r.key = key;
        r.lod = lod;
        r.bytes = 100;
        r.load = [](int level) { return std::make_shared<int>(level); };
        r.upload = [&fake, key](int level, std::shared_ptr<void> payload) { fake.uploaded[key].push_back(payload ? *static_cast<int*>(payload.get()) : -1); (void)level; };
        r.evict = [&fake, key](int level) { fake.evicted.push_back(key + ":" + std::to_string(level)); };
        return residency.request(r);
    };
    auto settle = [&] {
        for (int i = 0; i < 2000 && !streaming.settled(); ++i) {
            streaming.update(-1);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        streaming.update(-1);
        residency.advance();
    };
    CHECK(ask("a", 2));
    CHECK(!ask("a", 2));
    settle();
    CHECK(residency.residentLod("a") == 2);
    ask("b", 0);
    settle();
    residency.touch("b");
    ask("c", 0);
    settle();
    CHECK(residency.residentBytes() <= 250);
    CHECK(residency.residentLod("a") == -1);
    CHECK(residency.residentLod("b") == 0 && residency.residentLod("c") == 0);
    CHECK(std::find(fake.evicted.begin(), fake.evicted.end(), "a:2") != fake.evicted.end());
    CHECK(ask("b", 0) == false);
    CHECK(ask("a", 3));
    settle();
    CHECK(ask("a", 1));
    settle();
    CHECK(residency.residentLod("a") == 1);
    CHECK(!ask("a", 2));
    CHECK(residency.residentBytes() <= 250);
    CHECK(residency.uploads() >= 5);
    residency.release("a");
    CHECK(residency.residentLod("a") == -1);
}

static void testOriginShift() {
    Engine engine;
    engine.openHeadless();
    buildLine(engine.portals(), 4);
    FloatingOriginConfig originConfig;
    originConfig.threshold = 30.0;
    originConfig.snap = 16.0;
    engine.enableFloatingOrigin(originConfig);
    World& world = engine.world();
    EntityDesc thingDesc;
    thingDesc.name = "thing";
    thingDesc.position = glm::vec3(48.5f, 1.0f, 0.25f);
    thingDesc.mesh = 1;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.5f, 0.5f, 0.5f, 0.87f); });
    const EntityHandle thing = world.create(thingDesc);
    EntityDesc childDesc;
    childDesc.name = "child";
    childDesc.position = glm::vec3(1.0f, 0.0f, 0.0f);
    childDesc.parent = thing;
    const EntityHandle child = world.create(childDesc);
    {
        const auto hit = world.overlapSphere(glm::vec3(48.5f, 1.0f, 0.25f), 0.5f);
        CHECK(std::find(hit.begin(), hit.end(), thing) != hit.end());
    }
    const std::vector<Portal> before = engine.portals().portals();
    const glm::vec3 probe(30.0f, 2.0f, 1.0f);
    const glm::vec3 mapped = glm::vec3(before[0].transform * glm::vec4(probe, 1.0f));

    engine.setCameraWorldPosition(glm::dvec3(48.0, 1.7, 0.0));
    CHECK(engine.worldOrigin().x == 48.0 && engine.originEpoch() == 1);
    CHECK(std::abs(engine.camera().getPosition().x) < 1e-4f);
    CHECK(engine.cameraWorldPosition().x == 48.0);
    CHECK(std::abs(world.toWorld(world.getPosition(thing)).x - 48.5) < 1e-4);
    CHECK(std::abs(world.toWorld(world.getWorldPosition(child)).x - 49.5) < 1e-4);
    CHECK(engine.portals().cellAt(engine.camera().getPosition()) == 2);
    const std::vector<Portal>& after = engine.portals().portals();
    for (size_t i = 0; i < after.size(); ++i) {
        const glm::dvec3 was(before[i].center());
        const glm::dvec3 now = world.toWorld(after[i].center());
        CHECK(glm::length(was - now) < 1e-4);
    }
    const glm::vec3 shiftedProbe = glm::vec3(glm::dvec3(probe) - engine.worldOrigin());
    const glm::vec3 mappedAfter = glm::vec3(after[0].transform * glm::vec4(shiftedProbe, 1.0f));
    CHECK(glm::length(glm::dvec3(mapped) - (glm::dvec3(mappedAfter) + engine.worldOrigin())) < 1e-3);

    const auto near = world.overlapSphere(world.getPosition(thing), 0.5f);
    CHECK(std::find(near.begin(), near.end(), thing) != near.end());
    CHECK(!engine.rebaseOrigin(glm::dvec3(0.0)));
}

static void testOriginWithStreamingAndDeltas() {
    constexpr int kCells = 12;
    Engine engine;
    engine.openHeadless();
    buildLine(engine.portals(), kCells);
    StreamingConfig config;
    config.loadHops = 1;
    config.keepHops = 2;
    config.uploadBytesPerFrame = 1u << 30;
    config.commitMillisPerFrame = 50.0;
    StreamingSystem& streaming = engine.enableStreaming(config);
    DeltaStore& deltas = engine.enableDeltas();
    streaming.setLoader(makeBatchLoader(engine.world(), engine.portals(), &deltas, [](int cell) {
        CellContent content;
        for (int j = 0; j < 3; ++j) {
            CellEntity e;
            e.key = "k" + std::to_string(j);
            e.desc.name = "c" + std::to_string(cell) + "_" + std::to_string(j);
            e.hasWorldPosition = true;
            e.worldPosition = glm::dvec3(kSpacing * static_cast<double>(cell) + j, 0.5, 0.0);
            content.entities.push_back(e);
        }
        return content;
    }));
    World& world = engine.world();
    engine.portals().setCameraCell(0);
    streaming.prime(0);
    const EntityHandle moved = deltas.find("C0", "k1");
    CHECK(!moved.isNull());
    CHECK(std::abs(world.toWorld(world.getPosition(moved)).x - 1.0) < 1e-5);
    world.setPosition(moved, world.getPosition(moved) + glm::vec3(0.0f, 3.0f, 0.0f));

    CHECK(engine.rebaseOrigin(glm::dvec3(1024.0, 0.0, 0.0)));
    CHECK(std::abs(world.toWorld(world.getPosition(moved)).x - 1.0) < 1e-5);
    deltas.captureAll();
    CHECK(deltas.deltaCount() == 1);

    engine.portals().setCameraCell(8);
    pump(streaming, 8, glm::vec3(0.0f));
    CHECK(!streaming.resident(0));
    CHECK(streaming.resident(8));
    engine.rebaseOrigin(glm::dvec3(-512.0, 0.0, 0.0));
    engine.portals().setCameraCell(0);
    pump(streaming, 0, glm::vec3(0.0f));
    CHECK(streaming.resident(0));
    const EntityHandle back = deltas.find("C0", "k1");
    CHECK(!back.isNull());
    const glm::dvec3 restored = world.toWorld(world.getPosition(back));
    CHECK(std::abs(restored.x - 1.0) < 1e-4 && std::abs(restored.y - 3.5) < 1e-4);
    const glm::dvec3 untouched = world.toWorld(world.getPosition(deltas.find("C0", "k2")));
    CHECK(std::abs(untouched.x - 2.0) < 1e-4 && std::abs(untouched.y - 0.5) < 1e-4);
    deltas.captureAll();
    CHECK(deltas.deltaCount() == 1);
}


static NavMesh makeNavArena(float half) {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(half, 0.5f, half));
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(0.5f, 1.0f, 6.0f));
    NavMesh mesh;
    NavConfig config;
    config.originX = -half;
    config.originZ = -half;
    config.cellSize = 0.5f;
    mesh.configure(config);
    mesh.setGeometry(soup);
    mesh.rebuildAll();
    return mesh;
}

static void testNavShift() {
    NavMesh mesh = makeNavArena(16.0f);
    const glm::vec3 from(-8.0f, 0.0f, 0.0f);
    const glm::vec3 to(8.0f, 0.0f, 0.0f);
    const NavPath before = mesh.findPath(from, to);
    CHECK(before.found && before.points.size() >= 3);
    const uint64_t version = mesh.version();
    const uint32_t obstacle = mesh.addObstacle([] {
        Aabb box;
        box.grow(glm::vec3(2.0f, 0.0f, -10.0f));
        box.grow(glm::vec3(3.0f, 2.0f, -9.0f));
        return box;
    }());
    mesh.rebuildDirty();
    NavLink link;
    link.a = glm::vec3(-8.0f, 0.0f, 8.0f);
    link.b = glm::vec3(8.0f, 0.0f, 8.0f);
    const uint32_t linkId = mesh.addLink(link);

    const glm::vec3 shift(1024.0f, 4.0f, -512.0f);
    mesh.shiftOrigin(shift);
    const NavPath after = mesh.findPath(from - shift, to - shift);
    CHECK(after.found && after.points.size() == before.points.size());
    for (size_t i = 0; i < std::min(before.points.size(), after.points.size()); ++i) CHECK(glm::length(before.points[i].position - shift - after.points[i].position) < 1e-3f);
    CHECK(std::abs(after.length - before.length) < 1e-3f);
    CHECK(mesh.walkable(from - shift));
    CHECK(!mesh.walkable(from));
    const auto ground = mesh.groundHeight(glm::vec3(0.0f, 0.0f, 0.0f) - shift + glm::vec3(0.0f, 0.2f, 8.0f));
    CHECK(ground && std::abs(*ground - (0.0f - shift.y)) < 1e-3f);
    CHECK(mesh.link(linkId) && glm::length(mesh.link(linkId)->a - (link.a - shift)) < 1e-4f);
    CHECK(mesh.version() >= version);
    CHECK(mesh.obstacleCount() == 1);
    CHECK(mesh.moveObstacle(obstacle, [&] {
        Aabb box;
        box.grow(glm::vec3(2.0f, 0.0f, -10.0f) - shift);
        box.grow(glm::vec3(3.0f, 2.0f, -9.0f) - shift);
        return box;
    }()));
    CHECK(mesh.rebuildDirty() > 0);
    CHECK(mesh.findPath(from - shift, to - shift).found);

    NavMesh arena = makeNavArena(16.0f);
    NavCrowd crowd;
    std::vector<NavAgent> agents(6);
    for (size_t i = 0; i < agents.size(); ++i) {
        agents[i].position = glm::vec3(-10.0f, 0.0f, -4.0f + 1.5f * static_cast<float>(i));
        navSetTarget(arena, agents[i], glm::vec3(10.0f, 0.0f, -4.0f + 1.5f * static_cast<float>(i)));
    }
    for (int i = 0; i < 30; ++i) crowd.step(arena, agents, 1.0f / 30.0f);
    const std::vector<NavAgent> snapshot = agents;
    arena.shiftOrigin(shift);
    crowd.shiftOrigin(shift);
    navShiftAgents(agents, shift);
    for (size_t i = 0; i < agents.size(); ++i) CHECK(glm::length(agents[i].position - (snapshot[i].position - shift)) < 1e-4f && glm::length(agents[i].target - (snapshot[i].target - shift)) < 1e-4f);
    for (int i = 0; i < 900; ++i) crowd.step(arena, agents, 1.0f / 30.0f);
    int arrived = 0;
    for (size_t i = 0; i < agents.size(); ++i) {
        if (agents[i].state == AgentState::Arrived || glm::length(agents[i].position - agents[i].target) < 1.5f) ++arrived;
        CHECK(std::abs(agents[i].position.y - (-shift.y)) < 0.6f);
    }
    CHECK(arrived == static_cast<int>(agents.size()));
}

static void testAudioShift() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 1.0f, 0.0f)), glm::vec3(0.25f, 3.0f, 6.0f));
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(30.0f, 0.5f, 30.0f));
    AudioPropagation audio;
    audio.setScene(soup);
    const glm::vec3 listener(0.0f, 1.5f, 0.0f);
    const glm::vec3 source(10.0f, 1.5f, 0.0f);
    audio.beginFrame(listener);
    const AudioPath blocked = audio.trace(listener, source);
    CHECK(blocked.occlusion > 0.2f);
    audio.setHopHook([](const glm::vec3& from, std::vector<AudioHop>& out) {
        if (glm::length(from - glm::vec3(0.0f, 1.5f, 0.0f)) < 20.0f) out.push_back({glm::vec3(4.0f, 1.5f, 7.0f), glm::vec3(6.0f, 1.5f, 7.0f), 0.0f, 3});
    });
    audio.beginFrame(listener);
    const AudioPath hopped = audio.trace(listener, source);
    const glm::vec3 shift(2048.0f, 0.0f, -4096.0f);
    audio.shiftOrigin(shift);
    audio.beginFrame(listener - shift);
    const AudioPath shifted = audio.trace(listener - shift, source - shift);
    CHECK(std::abs(shifted.occlusion - hopped.occlusion) < 1e-3f);
    CHECK(shifted.hopId == hopped.hopId && shifted.viaHop == hopped.viaHop);
    CHECK(std::abs(shifted.pathLength - hopped.pathLength) < 1e-2f);
    CHECK(glm::length(shifted.arrivalDirection - hopped.arrivalDirection) < 1e-2f);
    CHECK(audio.bvh().bounds().lo.x < -2000.0f);
    audio.update(listener - shift, {AudioEmitter{7, source - shift}});
    const AudioPath* cached = audio.result(7);
    CHECK(cached && cached->attenuation > 0.0f);

    AudioMixer mixer;
    mixer.setListener(listener, glm::vec3(0, 0, 1));
    mixer.shiftOrigin(shift);
    CHECK(glm::length(mixer.listenerPosition() - (listener - shift)) < 1e-4f);
}

static void testObservationAndEngineHooks() {
    Engine engine;
    engine.openHeadless();
    buildLine(engine.portals(), 2);
    ObservationSystem& observation = engine.enableObservation({}, false);
    ObservationView view;
    view.origin = glm::vec3(3000.0f, 2.0f, 5.0f);
    observation.setView(view);
    NavMesh mesh = makeNavArena(16.0f);
    AudioPropagation audio;
    TriangleSoup soup;
    soup.addBox(glm::mat4(1.0f), glm::vec3(1.0f));
    audio.setScene(soup);
    int calls = 0;
    glm::vec3 seen(0.0f);
    engine.addOriginShiftListener([&](const glm::vec3& shift) {
        ++calls;
        seen = shift;
        mesh.shiftOrigin(shift);
        audio.shiftOrigin(shift);
    });
    CHECK(engine.rebaseOrigin(glm::dvec3(2048.0, 0.0, 0.0)));
    CHECK(calls == 1 && seen.x == 2048.0f);
    CHECK(std::abs(observation.view().origin.x - 952.0f) < 1e-3f);
    CHECK(mesh.walkable(glm::vec3(0.0f - 2048.0f, 0.0f, -8.0f)));
    CHECK(audio.bvh().bounds().hi.x < -2000.0f);
}

static void testCharacterShift() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    PortalSystem portals;
    PhysicsSettings physics;
    EntityDesc floorDesc;
    floorDesc.position = glm::vec3(4096.0f, -0.5f, 0.0f);
    const EntityHandle floor = world.create(floorDesc);
    world.add<BoxCollider>(floor, glm::vec3(50.0f, 0.5f, 50.0f));
    CharacterController character(world, portals, physics);
    character.spawn(glm::vec3(4090.0f, 0.2f, 0.0f), 0.0f);
    character.setInput({glm::vec2(0.0f, 1.0f), false, false});
    for (int i = 0; i < 90; ++i) character.update(1.0f / 60.0f);
    CHECK(character.grounded());
    const glm::vec3 position = character.position();
    const glm::vec3 velocity = character.velocity();
    const glm::vec3 eye = character.eye();
    const glm::vec3 pivot = character.rig().pivot();
    CHECK(glm::length(velocity) > 0.5f);

    const glm::vec3 shift(4096.0f, 0.0f, 0.0f);
    world.shiftOrigin(glm::dvec3(shift));
    character.shiftOrigin(shift);
    CHECK(glm::length(character.position() - (position - shift)) < 1e-4f);
    CHECK(glm::length(character.velocity() - velocity) < 1e-5f);
    CHECK(glm::length(character.eye() - (eye - shift)) < 1e-3f);
    CHECK(glm::length(character.rig().pivot() - (pivot - shift)) < 1e-4f);
    for (int i = 0; i < 60; ++i) character.update(1.0f / 60.0f);
    CHECK(character.grounded());
    CHECK(character.position().x > position.x - shift.x + 1.0f);
    CHECK(std::abs(character.position().y) < 0.05f);
    CHECK(std::abs(world.toWorld(world.getPosition(floor)).x - 4096.0) < 1e-4);
}

static EntityHandle findNamed(World& world, const std::string& name) {
    for (EntityHandle root : world.getRoots()) {
        if (world.getName(root) == name) return root;
    }
    return NULL_ENTITY;
}

static void testSaveWithDifferentOrigin() {
    World a;
    EntityDesc far;
    far.name = "far";
    far.position = glm::vec3(5000.0f, 1.0f, 3.0f);
    a.create(far);
    a.camera().setPos(glm::vec3(4990.0f, 2.0f, 0.0f));
    SaveSystem saveA(SaveContext{&a});
    const std::string path = testsupport::tempFilePath("lit_largeworld_", "origin.litsave");
    CHECK(saveA.save(path));

    World b;
    b.shiftOrigin(glm::dvec3(2048.0, 0.0, 0.0));
    SaveSystem saveB(SaveContext{&b});
    std::string error;
    CHECK(saveB.load(path, &error));
    const EntityHandle farB = findNamed(b, "far");
    CHECK(!farB.isNull());
    CHECK(std::abs(b.getPosition(farB).x - (5000.0f - 2048.0f)) < 1e-3f);
    CHECK(std::abs(b.toWorld(b.getPosition(farB)).x - 5000.0) < 1e-3);
    CHECK(std::abs(b.toWorld(b.camera().getPosition()).x - 4990.0) < 1e-3);

    const std::string pathB = testsupport::tempFilePath("lit_largeworld_", "origin2.litsave");
    CHECK(saveB.save(pathB));
    World c;
    SaveSystem saveC(SaveContext{&c});
    CHECK(saveC.load(pathB, &error));
    const EntityHandle farC = findNamed(c, "far");
    CHECK(!farC.isNull() && std::abs(c.getPosition(farC).x - 5000.0f) < 1e-3f);
    CHECK(std::abs(c.camera().getPosition().x - 4990.0f) < 1e-3f);
    World d;
    d.shiftOrigin(glm::dvec3(-1024.0, 0.0, 512.0));
    SaveSystem saveD(SaveContext{&d});
    CHECK(saveD.load(pathB, &error));
    const EntityHandle farD = findNamed(d, "far");
    CHECK(!farD.isNull() && std::abs(d.toWorld(d.getPosition(farD)).x - 5000.0) < 1e-3 && std::abs(d.toWorld(d.getPosition(farD)).z - 3.0) < 1e-3);
    CHECK(std::abs(d.toWorld(d.camera().getPosition()).x - 4990.0) < 1e-3);
    std::remove(path.c_str());
    std::remove(pathB.c_str());
}

int main() {
    testSpatialRings();
    testSpatialUnionWithPortals();
    testMeshResidency();
    testOriginShift();
    testOriginWithStreamingAndDeltas();
    testNavShift();
    testAudioShift();
    testObservationAndEngineHooks();
    testCharacterShift();
    testSaveWithDifferentOrigin();
    return testsupport::finish();
}
