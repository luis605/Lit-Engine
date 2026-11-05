#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "TestSupport.hpp"

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.History;
import Engine.Animation;
import Engine.Jobs;
import Engine.Profiler;
import Engine.LineEditor;
import Engine.GizmoMath;
import Engine.Selection;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

#include "WorldFixtures.hpp"
#include "WorldSuites.hpp"

namespace {

void testEvents() {
    World world;
    int created = 0, destroyed = 0, reparented = 0;
    const uint32_t id = world.events().subscribe<EntityCreated>([&](const EntityCreated&) { ++created; });
    world.events().subscribe<EntityDestroyed>([&](const EntityDestroyed&) { ++destroyed; });
    world.events().subscribe<EntityReparented>([&](const EntityReparented& e) { if (!e.newParent.isNull()) ++reparented; });
    auto a = world.create("a", 1);
    auto b = world.create("b", 1);
    CHECK(created == 0);
    world.events().dispatch();
    CHECK(created == 2);
    world.setParent(b, a);
    world.destroy(a);
    world.events().dispatch();
    CHECK(reparented == 1);
    CHECK(destroyed == 2);
    world.events().unsubscribe(id);
    world.create("c", 1);
    world.events().dispatch();
    CHECK(created == 2);
    CHECK(world.events().pending() == 0);
}

struct FixedCounter : Script {
    int* fixed;
    int* starts;
    FixedCounter(int* f, int* s) : fixed(f), starts(s) {}
    void onStart(World&, EntityHandle) override { ++*starts; }
    void onFixedUpdate(World&, EntityHandle, float) override { ++*fixed; }
};

void testFixedUpdate() {
    World world;
    int fixed = 0, starts = 0;
    auto e = world.create("e", 1);
    world.attach<FixedCounter>(e, &fixed, &starts);
    world.fixedUpdate(0.016f);
    world.fixedUpdate(0.016f);
    CHECK(fixed == 2);
    CHECK(starts == 1);
}

struct TickProbe : Script {
    int* updates;
    float* lastDelta;
    int* fixedCount;
    TickProbe(int* u, float* d, int* f) : updates(u), lastDelta(d), fixedCount(f) {}
    void onUpdate(World&, EntityHandle, float dt) override {
        ++*updates;
        *lastDelta = dt;
    }
    void onFixedUpdate(World&, EntityHandle, float) override { ++*fixedCount; }
};

void testTimeService() {
    Engine engine;
    World& world = engine.world();
    int updates = 0, fixedCount = 0;
    float lastDelta = 0.0f;
    auto e = world.create("e", 1);
    world.attach<TickProbe>(e, &updates, &lastDelta, &fixedCount);

    engine.tick(0.25f);
    CHECK(updates == 1 && std::abs(lastDelta - 0.25f) < 1.0e-6f);
    CHECK(fixedCount >= 14 && fixedCount <= 15);
    CHECK(engine.time().frame == 1);
    CHECK(std::abs(engine.time().elapsed - 0.25) < 1.0e-6);

    engine.setTimeScale(0.5f);
    engine.tick(0.1f);
    CHECK(std::abs(lastDelta - 0.05f) < 1.0e-6f);
    CHECK(std::abs(engine.time().unscaledDeltaTime - 0.1f) < 1.0e-6f);
    CHECK(std::abs(engine.time().elapsed - 0.30) < 1.0e-6);
    CHECK(std::abs(engine.time().unscaledElapsed - 0.35) < 1.0e-6);

    engine.setPaused(true);
    const int updatesBefore = updates;
    const int fixedBefore = fixedCount;
    engine.tick(0.1f);
    CHECK(updates == updatesBefore && fixedCount == fixedBefore);
    CHECK(engine.time().deltaTime == 0.0f);
    CHECK(engine.time().frame == 3);
    CHECK(std::abs(engine.time().elapsed - 0.30) < 1.0e-6);

    engine.setPaused(false);
    engine.tick(1.0f);
    CHECK(std::abs(engine.time().unscaledDeltaTime - 0.25f) < 1.0e-6f);
    CHECK(std::abs(engine.time().deltaTime - 0.125f) < 1.0e-6f);
}

void testSystemScheduler() {
    Engine engine;
    std::vector<std::string> log;
    engine.addSystem(Phase::PostUpdate, "post", [&](World&, float) { log.push_back("post"); });
    engine.addSystem(Phase::Update, "update-a", [&](World&, float) { log.push_back("update-a"); });
    engine.addSystem(Phase::PreUpdate, "pre", [&](World&, float) { log.push_back("pre"); });
    engine.addSystem(Phase::Update, "update-b", [&](World&, float) { log.push_back("update-b"); });
    engine.addSystem(Phase::FixedUpdate, "fixed", [&](World&, float) { log.push_back("fixed"); });

    engine.tick(1.0f / 60.0f + 0.0001f);
    const std::vector<std::string> expected{"pre", "fixed", "update-a", "update-b", "post"};
    CHECK(log == expected);
    CHECK(engine.systemNames(Phase::Update) == (std::vector<std::string>{"animation", "walkers", "ik", "update-a", "update-b"}));

    log.clear();
    CHECK(engine.setSystemEnabled("update-a", false));
    CHECK(!engine.isSystemEnabled("update-a"));
    engine.tick(1.0f / 60.0f + 0.0001f);
    CHECK(std::count(log.begin(), log.end(), "update-a") == 0);
    CHECK(std::count(log.begin(), log.end(), "update-b") == 1);

    log.clear();
    engine.addSystem(Phase::Update, "always", [&](World&, float dt) { log.push_back(dt == 0.0f ? "always-idle" : "always-run"); }, true);
    engine.setPaused(true);
    engine.tick(0.1f);
    CHECK(log == (std::vector<std::string>{"always-idle"}));
    engine.setPaused(false);

    CHECK(engine.removeSystem("update-b"));
    CHECK(!engine.removeSystem("update-b"));
    CHECK(!engine.setSystemEnabled("missing", true));

    engine.addSystem(Phase::Update, "update-a", [&](World&, float) { log.push_back("replaced"); });
    log.clear();
    engine.tick(0.01f);
    CHECK(std::count(log.begin(), log.end(), "replaced") == 1);

    Engine physicsEngine;
    World& world = physicsEngine.world();
    auto ball = world.create("ball", 1, glm::vec3(0.0f, 10.0f, 0.0f));
    world.add<SphereCollider>(ball, 0.5f);
    world.add<RigidBody>(ball);
    for (int i = 0; i < 30; ++i) physicsEngine.tick(1.0f / 60.0f);
    CHECK(world.getPosition(ball).y < 9.0f);
    physicsEngine.setSystemEnabled("physics", false);
    const float frozen = world.getPosition(ball).y;
    for (int i = 0; i < 30; ++i) physicsEngine.tick(1.0f / 60.0f);
    CHECK(world.getPosition(ball).y == frozen);
}

void testTimers() {
    Engine engine;
    World& world = engine.world();
    int oneShot = 0, repeating = 0, cancelledRuns = 0;
    world.after(0.5f, [&](World&, EntityHandle) { ++oneShot; });
    const uint32_t ticker = world.every(0.1f, [&](World&, EntityHandle) { ++repeating; });
    const uint32_t doomed = world.after(0.3f, [&](World&, EntityHandle) { ++cancelledRuns; });
    CHECK(world.timerCount() == 3);
    CHECK(world.cancel(doomed));
    CHECK(!world.cancel(doomed));

    for (int i = 0; i < 6; ++i) engine.tick(0.1f);
    CHECK(oneShot == 1);
    CHECK(repeating >= 5 && repeating <= 6);
    CHECK(cancelledRuns == 0);
    CHECK(world.timerCount() == 1);

    engine.setTimeScale(0.5f);
    const int before = repeating;
    for (int i = 0; i < 4; ++i) engine.tick(0.1f);
    CHECK(repeating - before >= 1 && repeating - before <= 2);

    engine.setPaused(true);
    const int paused = repeating;
    for (int i = 0; i < 10; ++i) engine.tick(0.1f);
    CHECK(repeating == paused);
    engine.setPaused(false);
    engine.setTimeScale(1.0f);

    CHECK(world.cancel(ticker));
    CHECK(world.timerCount() == 0);

    auto owner = world.create("owner");
    int owned = 0;
    world.every(0.1f, [&](World&, EntityHandle e) { if (e == owner) ++owned; }, owner);
    engine.tick(0.25f);
    CHECK(owned == 2);
    world.destroy(owner);
    engine.tick(0.25f);
    CHECK(owned == 2);
    CHECK(world.timerCount() == 0);

    int chained = 0;
    world.after(0.1f, [&](World& callbackWorld, EntityHandle) {
        ++chained;
        callbackWorld.after(0.1f, [&](World&, EntityHandle) { ++chained; });
    });
    engine.tick(0.15f);
    CHECK(chained == 1);
    engine.tick(0.15f);
    CHECK(chained == 2);

    int selfCancel = 0;
    uint32_t selfId = 0;
    selfId = world.every(0.05f, [&](World& callbackWorld, EntityHandle) {
        ++selfCancel;
        if (selfCancel == 3) callbackWorld.cancel(selfId);
    });
    for (int i = 0; i < 10; ++i) engine.tick(0.05f);
    CHECK(selfCancel == 3);
    CHECK(world.timerCount() == 0);
    CHECK(world.after(1.0f, nullptr) == 0);
    CHECK(world.every(0.0f, [](World&, EntityHandle) {}) == 0);
}

void testHistory() {
    World world;
    History history(world);

    auto a = history.create(EntityDesc{.name = "a", .mesh = 1, .position = glm::vec3(1.0f, 0.0f, 0.0f)});
    auto b = history.create(EntityDesc{.name = "b", .mesh = 1, .position = glm::vec3(0.0f, 2.0f, 0.0f)});
    CHECK(world.aliveCount() == 2);

    history.setLocalMatrix(a, glm::translate(glm::mat4(1.0f), glm::vec3(9.0f, 0.0f, 0.0f)));
    CHECK(world.getPosition(a).x == 9.0f);
    CHECK(history.undo());
    CHECK(world.getPosition(a).x == 1.0f);
    CHECK(history.redo());
    CHECK(world.getPosition(a).x == 9.0f);
    CHECK(!history.redo());

    history.setVisible(b, false);
    CHECK(!world.isVisible(b));
    history.undo();
    CHECK(world.isVisible(b));

    history.setParent(b, a, false);
    CHECK(world.getParent(b) == a);
    history.undo();
    CHECK(world.getParent(b).isNull());
    history.redo();
    CHECK(world.getParent(b) == a);

    history.destroy(a);
    CHECK(world.aliveCount() == 0);
    CHECK(history.undo());
    CHECK(world.aliveCount() == 2);
    auto ra = world.find("a");
    auto rb = world.find("b");
    CHECK(!ra.isNull() && !rb.isNull());
    CHECK(world.getParent(rb) == ra);
    CHECK(world.getPosition(ra).x == 9.0f);
    CHECK(history.resolve(a) == ra);
    CHECK(history.resolve(b) == rb);

    history.setLocalMatrix(b, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f)));
    CHECK(world.getPosition(rb).z == 5.0f);
    history.undo();
    CHECK(world.getPosition(rb).z == 0.0f && world.getPosition(rb).y == 2.0f);
    history.undo();
    CHECK(world.getParent(world.find("b")).isNull());
    history.undo();
    CHECK(world.getPosition(world.find("a")).x == 1.0f);
    history.undo();
    CHECK(world.aliveCount() == 1);
    history.undo();
    CHECK(world.aliveCount() == 0);
    CHECK(!history.undo());
    history.redo();
    history.redo();
    CHECK(world.aliveCount() == 2);

    history.create(EntityDesc{.name = "c", .mesh = 1});
    CHECK(!history.canRedo());
}

}

void runRuntimeTests() {
    testEvents();
    testTimeService();
    testHistory();
    testSystemScheduler();
    testTimers();
    testFixedUpdate();
}
