#include "TestSupport.hpp"
#include "ToolingSuites.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;
import Engine.Rng;
import Engine.Json;
import Engine.Dump;
import Engine.Replay;
import Engine.Perf;
import Engine.Params;
import Engine.Render.settings;
import Engine.TestScenes;

static std::string tempPath(const std::string& name) { return testsupport::tempFilePath("lit_tooling_", name); }

static void testReplayFormat() {
    ReplayData data;
    data.seed = 77;
    InputState s;
    for (int i = 0; i < 20; ++i) {
        s.mouseDelta = glm::vec2(0.0f);
        if (i == 3) s.keys.set(87);
        if (i == 5) s.keys.set(65);
        if (i == 9) s.keys.reset(87);
        if (i == 12) s.buttons.set(0);
        if (i == 15) s.keys.reset();
        if (i % 4 == 0) s.mouseDelta = glm::vec2(0.1f * static_cast<float>(i), -0.333333f);
        if (i == 7) s.mouse = glm::vec2(100.5f, 200.25f);
        data.ticks.push_back({i < 10 ? 1.0f / 60.0f : 0.0123f, s});
    }
    const std::string text = serializeReplay(data);
    CHECK(text.rfind("litreplay 1\nseed 77\nticks 20\n", 0) == 0);
    CHECK(text.size() > 40 && text.find("keys=65,87") != std::string::npos);
    ReplayData back;
    std::string err;
    CHECK(parseReplay(text, back, &err));
    CHECK(back.seed == 77);
    CHECK(back.ticks.size() == 20);
    bool same = back.ticks.size() == data.ticks.size();
    for (size_t i = 0; same && i < data.ticks.size(); ++i) same = back.ticks[i].dt == data.ticks[i].dt && back.ticks[i].input == data.ticks[i].input;
    CHECK(same);
    CHECK(serializeReplay(back) == text);

    CHECK(!parseReplay("nope\n", back));
    CHECK(!parseReplay("litreplay 1\nseed 1\nticks 3\n", back));
    CHECK(!parseReplay("litreplay 1\nseed 1\nticks 3\n@9 dt=0.1\nend\n", back));
    CHECK(!parseReplay("litreplay 1\nseed 1\nticks 3\n@0 keys=9999\nend\n", back));
    CHECK(!parseReplay("litreplay 1\nseed 1\nticks 3\n@0 bogus=1\nend\n", back));
    CHECK(!parseReplay("litreplay 1\nseed x\nticks 3\nend\n", back));
}

static void buildSim(Engine& engine, uint64_t seed) {
    engine.openHeadless();
    engine.seed(seed);
    World& w = engine.world();
    registerPhysicsComponents(w);
    auto ground = w.create("ground", 0);
    w.add<PlaneCollider>(ground);
    Pcg32& spawn = w.random().stream("spawn");
    for (int i = 0; i < 8; ++i) {
        const auto e = w.create("ball" + std::to_string(i), 1, glm::vec3(spawn.range(-3.0f, 3.0f), spawn.range(2.0f, 8.0f), spawn.range(-3.0f, 3.0f)));
        w.add<SphereCollider>(e, 0.5f);
        w.add<RigidBody>(e);
    }
    w.create("player", 1);
    w.create("drifter", 1);
    engine.addSystem(Phase::FixedUpdate, "player", [&engine](World& world, float dt) {
        const SimInput& in = engine.simInput();
        const auto p = world.find("player");
        glm::vec3 move(0.0f);
        if (in.held('W')) move.z -= 1.0f;
        if (in.held('D')) move.x += 1.0f;
        world.translate(p, move * 3.0f * dt + glm::vec3(in.current.mouseDelta.x * 0.01f, 0.0f, 0.0f));
    });
    engine.addSystem(Phase::FixedUpdate, "drift", [](World& world, float dt) {
        Pcg32& rng = world.random().stream("drift");
        world.translate(world.find("drifter"), glm::vec3(rng.range(-1.0f, 1.0f), 0.0f, rng.range(-1.0f, 1.0f)) * dt);
    });
}

static InputState scriptedInput(uint64_t tick) {
    InputState s;
    if (tick % 50 < 30) s.keys.set('W');
    if (tick % 70 > 20) s.keys.set('D');
    if (tick % 9 == 0) s.mouseDelta = glm::vec2(static_cast<float>(tick % 5), 0.0f);
    return s;
}

static std::string runSim(uint64_t seed, uint64_t ticks) {
    Engine engine;
    buildSim(engine, seed);
    engine.runHeadless(ticks, 1.0f / 60.0f, [&](uint64_t t) { engine.setInput(scriptedInput(t)); });
    return engine.dumpStateText();
}

static void testHeadlessDeterminism() {
    const std::string a = runSim(5, 300);
    const std::string b = runSim(5, 300);
    CHECK(a == b);
    CHECK(hashText(a) == hashText(b));
    const std::string c = runSim(6, 300);
    CHECK(a != c);
    const std::string shorter = runSim(5, 299);
    CHECK(a != shorter);

    const auto doc = Json::parse(a);
    CHECK(doc.has_value());
    CHECK_OK(expectNumber(*doc, "world.time.frame", 300));
    CHECK_OK(expectNumber(*doc, "world.count", 11));
    CHECK_OK(expectNumber(*doc, "world.time.elapsed", 5.0, 1.0e-3));
    CHECK_OK(expectSize(*doc, "world.entities", 11));
    CHECK_OK(expectString(*doc, "world.entities[name=player].name", "player"));
    const Json* ball = jsonFind(*doc, "world.entities[name=ball0].position[1]");
    CHECK(ball && std::abs(ball->asNumber() - 0.5) < 0.05);
    const Json* player = jsonFind(*doc, "world.entities[name=player].position");
    CHECK(player && std::abs(player->items()[2].asNumber()) > 1.0);
    CHECK(jsonFind(*doc, "world.entities[name=ball0].components.RigidBody") != nullptr);
    CHECK(jsonFind(*doc, "random.streams.drift") != nullptr);
    CHECK(jsonFind(*doc, "random.streams.spawn") != nullptr);

    const auto doc2 = Json::parse(c);
    CHECK(!jsonDiff(*doc, *doc2).empty());
    CHECK(jsonDiff(*doc, *Json::parse(runSim(5, 300))).empty());
}

static void testRecordReplay() {
    const std::string path = tempPath("run.replay");
    std::string live;
    {
        Engine engine;
        buildSim(engine, 21);
        engine.startRecording(path);
        CHECK(engine.recording());
        uint64_t t = 0;
        const float dts[] = {1.0f / 60.0f, 0.02f, 0.0137f, 1.0f / 30.0f, 0.005f};
        for (; t < 240; ++t) {
            engine.setInput(scriptedInput(t));
            engine.tick(dts[t % 5]);
        }
        CHECK(engine.stopRecording());
        live = engine.dumpStateText();
    }
    CHECK(std::filesystem::exists(path));
    ReplayData data;
    std::string err;
    CHECK(loadReplay(path, data, &err));
    CHECK(data.seed == 21);
    CHECK(data.ticks.size() == 240);
    CHECK(data.ticks[2].dt == 0.0137f);

    {
        Engine engine;
        buildSim(engine, 999);
        CHECK(engine.startReplay(path));
        CHECK(engine.replaying());
        engine.runHeadless(0);
        CHECK(!engine.replaying());
        CHECK(engine.dumpStateText() != live);
    }
    {
        Engine engine;
        buildSim(engine, data.seed);
        CHECK(engine.startReplay(path));
        CHECK(engine.replaySeed() == 21);
        engine.runHeadless(0);
        const std::string replayed = engine.dumpStateText();
        CHECK(replayed == live);
        CHECK(jsonDiff(*Json::parse(replayed), *Json::parse(live)).empty());
    }
    std::filesystem::remove(path);
}

static void testInputActionsSource() {
    Engine engine;
    engine.openHeadless();
    engine.input().bindKey("jump", 32);
    engine.input().bindAxis("x", 'A', 'D');
    CHECK(!engine.input().pressed("jump"));
    InputState s;
    s.keys.set(32);
    s.keys.set('D');
    engine.setInput(s);
    engine.tick(0.016f);
    CHECK(engine.input().pressed("jump"));
    CHECK(engine.input().held("jump"));
    CHECK(engine.input().axis("x") == 1.0f);
    engine.tick(0.016f);
    CHECK(!engine.input().pressed("jump"));
    CHECK(engine.input().held("jump"));
    engine.setInput(InputState{});
    engine.tick(0.016f);
    CHECK(engine.input().released("jump"));
    CHECK(engine.input().axis("x") == 0.0f);
}

void runReplayTests() {
    testReplayFormat();
    testHeadlessDeterminism();
    testRecordReplay();
    testInputActionsSource();
}
