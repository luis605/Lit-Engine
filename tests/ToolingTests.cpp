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

namespace {
int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    }
}

void checkOk(const std::string& error, const char* expr, int line) {
    ++g_checks;
    if (!error.empty()) {
        ++g_failures;
        std::fprintf(stderr, "FAIL line %d: %s -> %s\n", line, expr, error.c_str());
    }
}
}

#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)
#define CHECK_OK(x) checkOk((x), #x, __LINE__)

static std::string tempPath(const std::string& name) { return (std::filesystem::temp_directory_path() / ("lit_tooling_" + name)).string(); }

static std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void testRng() {
    Pcg32 ref(42, 54);
    const uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expected) CHECK(ref.next() == e);

    Pcg32 a(7), b(7), c(8);
    bool differs = false;
    for (int i = 0; i < 64; ++i) {
        const uint32_t x = a.next();
        CHECK(x == b.next());
        differs = differs || x != c.next();
    }
    CHECK(differs);

    Pcg32 r(3);
    for (int i = 0; i < 2000; ++i) {
        const float u = r.uniform();
        CHECK(u >= 0.0f && u < 1.0f);
        CHECK(r.below(10) < 10);
        const int n = r.rangeInt(-3, 3);
        CHECK(n >= -3 && n <= 3);
    }

    Pcg32 saved(11);
    saved.next();
    Pcg32 copy;
    copy.restore(saved.state(), saved.increment());
    CHECK(copy == saved);
    CHECK(copy.next() == saved.next());

    RandomService svc;
    svc.seed(99);
    const uint32_t first = svc.stream("alpha").next();
    const uint32_t other = svc.stream("beta").next();
    CHECK(first != other);
    svc.seed(99);
    CHECK(svc.stream("alpha").next() == first);
    RandomService svc2;
    svc2.seed(99);
    CHECK(svc2.stream("alpha").next() == first);
    svc2.seed(100);
    CHECK(svc2.stream("alpha").next() != first);
}

static void testJson() {
    std::string err;
    auto doc = Json::parse(R"({"a":[1,2.5,{"n":"x\ny"}],"b":true,"c":null,"d":"é","e":-1e3})", &err);
    CHECK(doc.has_value());
    CHECK(doc->get("b")->asBool());
    CHECK(doc->get("c")->isNull());
    CHECK(doc->get("e")->asNumber() == -1000.0);
    CHECK(doc->get("a")->size() == 3);
    CHECK(jsonFind(*doc, "a[2].n")->asString() == "x\ny");
    CHECK(jsonFind(*doc, "a[1]")->asNumber() == 2.5);
    CHECK(!jsonFind(*doc, "a[9]"));
    CHECK(!jsonFind(*doc, "zzz.y"));
    const auto round = Json::parse(doc->dump(), &err);
    CHECK(round.has_value() && *round == *doc);
    const auto pretty = Json::parse(doc->dump(2), &err);
    CHECK(pretty.has_value() && *pretty == *doc);

    CHECK(!Json::parse("{", &err).has_value() && !err.empty());
    CHECK(!Json::parse("[1,]").has_value());
    CHECK(!Json::parse("{\"a\":1} x").has_value());
    CHECK(!Json::parse("").has_value());
    CHECK(!Json::parse(std::string(500, '[')).has_value());

    Json built = Json::object();
    built.set("list", Json::array());
    built.set("v", 3);
    built.set("v", 4);
    CHECK(built.get("v")->asNumber() == 4.0);
    CHECK(built.members().size() == 2);

    auto items = Json::parse(R"({"items":[{"name":"a","v":1},{"name":"b","v":2,"pos":[1,2,3]}]})");
    CHECK_OK(expectNumber(*items, "items[name=b].v", 2.0));
    CHECK_OK(expectVec(*items, "items[name=b].pos", {1.0, 2.0, 3.0}));
    CHECK_OK(expectSize(*items, "items", 2));
    CHECK_OK(expectString(*items, "items[0].name", "a"));
    CHECK(!expectNumber(*items, "items[name=b].v", 3.0).empty());
    CHECK(!expectNumber(*items, "items[name=zz].v", 3.0).empty());
    CHECK(!expectVec(*items, "items[name=b].pos", {1.0, 2.0, 4.0}).empty());
    CHECK(!expectBool(*items, "items[0].name", true).empty());

    auto other = Json::parse(R"({"items":[{"name":"a","v":1},{"name":"b","v":2.5,"pos":[1,2,3]}]})");
    CHECK(jsonDiff(*items, *items).empty());
    CHECK(jsonDiff(*items, *other).find("items[1].v") != std::string::npos);
    CHECK(jsonDiff(*items, *other, 1.0).empty());
}

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

static void testStateDump() {
    Engine engine;
    engine.openHeadless();
    World& w = engine.world();
    auto parent = w.create("parent", NO_MESH, glm::vec3(1.0f, 2.0f, 3.0f));
    auto child = w.create("child", NO_MESH, glm::vec3(0.5f, 0.0f, 0.0f));
    w.setParent(child, parent, false);
    w.setTag(child, "tagged");
    w.setLayer(child, 4);
    const Json doc = dumpWorld(w);
    CHECK_OK(expectNumber(doc, "count", 2));
    CHECK_OK(expectVec(doc, "entities[name=parent].position", {1.0, 2.0, 3.0}));
    CHECK_OK(expectVec(doc, "entities[name=child].worldPosition", {1.5, 2.0, 3.0}));
    CHECK_OK(expectNumber(doc, "entities[name=child].parent", parent.index));
    CHECK_OK(expectNumber(doc, "entities[name=parent].parent", -1));
    CHECK_OK(expectString(doc, "entities[name=child].tag", "tagged"));
    CHECK_OK(expectNumber(doc, "entities[name=child].layer", 4));
    CHECK_OK(expectNumber(doc, "entities[name=parent].children", 1));
    CHECK_OK(expectBool(doc, "entities[name=child].visible", true));
    const std::string path = tempPath("state.json");
    engine.params().set("drive", 0.5f);
    engine.params().update(0.016f);
    CHECK(engine.writeStateDump(path));
    const auto file = Json::parse(slurp(path));
    CHECK(file.has_value());
    CHECK_OK(expectNumber(*file, "params.drive", 0.5));
    CHECK_OK(expectNumber(*file, "world.count", 2));
    std::filesystem::remove(path);

    World other;
    other.create("parent", NO_MESH, glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(!jsonDiff(doc, dumpWorld(other)).empty());
    DumpOptions lean;
    lean.components = false;
    lean.scripts = false;
    lean.worldTransforms = false;
    CHECK(!jsonFind(dumpWorld(w, lean), "entities[0].components"));
}

static std::vector<std::vector<std::string>> readCsv(const std::string& path) {
    std::vector<std::vector<std::string>> rows;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) cells.push_back(cell);
        rows.push_back(std::move(cells));
    }
    return rows;
}

static void testPerfLog() {
    const std::string path = tempPath("perf.csv");
    {
        PerfLog log;
        CHECK(!log.active());
        log.open(path);
        CHECK(log.active());
        log.record(0, 16.0, 1.0, 4.0, {{"Shadow", 1.5}, {"GBuffer", 2.5}});
        log.record(1, 17.0, 1.1, 4.5, {{"GBuffer", 3.0}, {"Post,Fx", 0.5}});
        CHECK(log.rows() == 2);
    }
    const auto rows = readCsv(path);
    CHECK(rows.size() == 3);
    CHECK(rows[0].size() == 7);
    CHECK(rows[0][0] == "frame" && rows[0][1] == "cpu_ms" && rows[0][3] == "gpu_ms");
    CHECK(rows[0][4] == "gpu:Shadow" && rows[0][5] == "gpu:GBuffer" && rows[0][6] == "gpu:Post_Fx");
    CHECK(rows[1].size() == 7 && std::abs(std::stod(rows[1][5]) - 2.5) < 1.0e-6);
    CHECK(std::abs(std::stod(rows[2][5]) - 3.0) < 1.0e-6 && std::stod(rows[2][4]) == 0.0);
    std::filesystem::remove(path);

    Engine engine;
    engine.openHeadless();
    engine.enablePerfLog(path);
    CHECK(engine.perfLog().active());
    engine.runHeadless(30, 1.0f / 60.0f);
    engine.disablePerfLog();
    const auto engineRows = readCsv(path);
    CHECK(engineRows.size() == 31);
    CHECK(engineRows[0].size() == 4);
    CHECK(std::stod(engineRows[10][1]) >= 0.0);
    CHECK(std::stoull(engineRows[30][0]) == 29);
    std::filesystem::remove(path);
}

static void testParams() {
    ParamBus bus;
    bus.declare("drive", 0.0f, 0.5f, 0.0f, 1.0f);
    bus.set("drive", 1.0f);
    CHECK(bus.get("drive") == 0.0f);
    bus.update(0.5f);
    CHECK(std::abs(bus.get("drive") - (1.0f - std::exp(-1.0f))) < 1.0e-5f);
    for (int i = 0; i < 200; ++i) bus.update(0.1f);
    CHECK(bus.get("drive") > 0.999f);
    bus.set("drive", 5.0f);
    CHECK(bus.target("drive") == 1.0f);
    CHECK(bus.slot("drive") == 0);
    CHECK(bus.slot("missing") == -1);
    bus.snap("drive", 0.25f);
    CHECK(bus.get("drive") == 0.25f);
    CHECK(bus.values()[0] == 0.25f);
    bus.set("auto", 2.0f);
    bus.update(0.016f);
    CHECK(bus.get("auto") == 2.0f && bus.slot("auto") == 1);

    ParamCurve curve;
    curve.points = {{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}};
    CHECK(curve.eval(-1.0f) == 0.0f);
    CHECK(std::abs(curve.eval(0.25f) - 0.5f) < 1.0e-6f);
    CHECK(curve.eval(2.0f) == 0.0f);
    curve.mode = CurveMode::Step;
    CHECK(curve.eval(0.4f) == 0.0f && curve.eval(0.6f) == 1.0f);
    curve.mode = CurveMode::Smooth;
    CHECK(std::abs(curve.eval(0.25f) - 0.5f) < 1.0e-6f);
    CHECK(curve.eval(0.1f) < 0.1f * 2.0f);

    const std::string path = tempPath("params.json");
    const char* v1 = R"({
        "params": {"drive": {"default": 0, "min": 0, "max": 1, "smooth": 0, "slot": 4}, "bias": {"default": 0.5}},
        "mappings": {
            "vignette": {"input": "drive", "curve": [[0, 0.1], [1, 0.9]], "slot": 9},
            "grade": {"input": "vignette", "curve": [[0, 0], [1, 10]], "mode": "step"}
        }
    })";
    {
        std::ofstream(path) << v1;
    }
    ParamBus file;
    std::string err;
    CHECK(file.loadFile(path, &err));
    CHECK(file.slot("drive") == 4 && file.slot("vignette") == 9 && file.slot("bias") == 0);
    file.set("drive", 0.5f);
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.5f) < 1.0e-6f);
    CHECK(file.get("grade") == 0.0f);
    file.set("drive", 1.0f);
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.9f) < 1.0e-6f);
    CHECK(file.get("grade") == 0.0f);
    CHECK(file.valueAt(9) == file.get("vignette"));
    CHECK(file.curve("vignette") != nullptr && file.curve("drive") == nullptr);
    CHECK(!file.pollReload());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream(path) << R"({"params": {"drive": {"default": 0, "min": 0, "max": 1, "slot": 4}, "bias": {"default": 0.5}},
            "mappings": {"vignette": {"input": "drive", "curve": [[0, 0.0], [1, 0.2]], "slot": 9}}})";
    }
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
    const uint32_t before = file.reloadCount();
    CHECK(file.pollReload(&err));
    CHECK(file.reloadCount() == before + 1);
    CHECK(file.get("drive") == 1.0f);
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.2f) < 1.0e-6f);
    CHECK(!file.has("grade"));
    CHECK(!file.pollReload());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream(path) << R"({"mappings": {"broken": {"input": "nothing", "curve": [[0, 0]]}}})";
    }
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(4));
    CHECK(!file.pollReload(&err));
    CHECK(!err.empty());
    CHECK(!file.has("broken"));
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.2f) < 1.0e-6f);
    CHECK(!file.pollReload());
    std::filesystem::remove(path);

    ParamBus bad;
    CHECK(!bad.loadText("{", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"min": 2, "max": 1}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"wat": 1}}})", &err) && err.find("unknown key") != std::string::npos);
    CHECK(!bad.loadText(R"({"params": {"a": {}}, "mappings": {"m": {"input": "a", "curve": [[1, 0], [0, 1]]}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {}}, "mappings": {"m": {"input": "a", "curve": []}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"smooth": -1}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"slot": 9999}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {}}, "mappings": {"m": {"input": "a", "curve": [[0, 0]], "mode": "wavy"}}})", &err));
    CHECK(bad.params().empty());
}

static void testEngineParamsSystem() {
    const std::string path = tempPath("engine_params.json");
    {
        std::ofstream(path) << R"({"params": {"drive": {"default": 0, "smooth": 0.1}}, "mappings": {"tight": {"input": "drive", "curve": [[0, 0], [1, 2]]}}})";
    }
    Engine engine;
    engine.openHeadless();
    std::string err;
    CHECK(engine.params().loadFile(path, &err));
    engine.params().set("drive", 1.0f);
    engine.runHeadless(120, 1.0f / 60.0f);
    CHECK(engine.params().get("drive") > 0.99f);
    CHECK(engine.params().get("tight") > 1.98f);
    const std::string a = engine.dumpStateText();
    Engine again;
    again.openHeadless();
    CHECK(again.params().loadFile(path, &err));
    again.params().set("drive", 1.0f);
    again.runHeadless(120, 1.0f / 60.0f);
    CHECK(again.dumpStateText() == a);
    std::filesystem::remove(path);
}

static void testPostFxAndDataReload() {
    const std::string paramsPath = tempPath("fx_params.json");
    const std::string fxPath = tempPath("fx_post.json");
    std::ofstream(paramsPath) << R"({"params": {"focus": {"default": 1}}, "mappings": {"width": {"input": "focus", "curve": [[0, 0.3], [1, 0.9]]}}})";
    std::ofstream(fxPath) << R"({"vignetteWidth": {"base": 0.5, "param": "width"}, "maskContrast": 1.5, "maskSheenColor": [0.1, 0.2, 0.3]})";
    Engine engine;
    engine.openHeadless();
    std::string err;
    CHECK(engine.params().loadFile(paramsPath, &err));
    CHECK(engine.loadPostFx(fxPath, &err));
    CHECK(std::fabs(engine.renderSettings().maskContrast - 1.5f) < 1e-5f);
    CHECK(std::fabs(engine.renderSettings().maskSheenColor.z - 0.3f) < 1e-5f);
    engine.params().snap("focus", 0.0f);
    engine.runHeadless(4, 1.0f / 60.0f);
    CHECK(std::fabs(engine.renderSettings().vignetteWidth - 0.3f) < 1e-3f);
    engine.params().snap("focus", 1.0f);
    engine.runHeadless(4, 1.0f / 60.0f);
    CHECK(std::fabs(engine.renderSettings().vignetteWidth - 0.9f) < 1e-3f);
    std::ofstream(fxPath) << R"({"maskContrast": 2.25})";
    std::filesystem::last_write_time(fxPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
    engine.runHeadless(32, 1.0f / 60.0f);
    CHECK(engine.dataReloads() >= 1);
    CHECK(std::fabs(engine.renderSettings().maskContrast - 2.25f) < 1e-5f);
    std::ofstream(fxPath) << R"({"nonsense": 1})";
    std::filesystem::last_write_time(fxPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10));
    engine.runHeadless(32, 1.0f / 60.0f);
    CHECK(std::fabs(engine.renderSettings().maskContrast - 2.25f) < 1e-5f);
    CHECK(!engine.loadPostFx(fxPath, &err));
    std::filesystem::remove(paramsPath);
    std::filesystem::remove(fxPath);
}

static void testRenderMaskFlag() {
    Engine engine;
    engine.openHeadless();
    EntityDesc desc;
    desc.name = "masked";
    desc.mesh = 0;
    const EntityHandle e = engine.world().create(desc);
    CHECK((engine.world().getRenderFlags(e) & RENDER_MASKED) == 0);
    engine.world().setRenderFlags(e, RENDER_MASKED, true);
    CHECK((engine.world().getRenderFlags(e) & RENDER_MASKED) != 0);
    engine.world().setRenderFlags(e, RENDER_MASKED, false);
    CHECK((engine.world().getRenderFlags(e) & RENDER_MASKED) == 0);
}

static void testTestScenes() {
    CHECK(testSceneNames().size() == 4);
    for (const std::string& name : testSceneNames()) {
        Engine engine;
        engine.openHeadless();
        engine.seed(7);
        TestSceneInfo info;
        CHECK(buildTestScene(engine, name, TestSceneKit{}, info));
        applyTestSceneCamera(engine, info);
        engine.runHeadless(name == "streaming_route" ? 900 : 120, 1.0f / 60.0f);
        const TestSceneMetrics m = info.metrics();
        CHECK(m.get("entities") > 10.0);
        if (name == "portal_torture") {
            CHECK(m.get("portals") >= 4.0);
            CHECK(m.get("max_view_nodes") >= 2.0);
        } else if (name == "streaming_route") {
            CHECK(m.get("chunks_created") > 9.0);
            CHECK(m.get("chunks_destroyed") > 0.0);
            CHECK(m.get("chunks_resident") == 9.0);
        } else if (name == "crowd") {
            CHECK(m.get("agents") >= 500.0);
            CHECK(m.get("distance_walked_m") > 100.0);
        } else {
            CHECK(m.get("farthest_object_m") > 50000.0);
            CHECK(m.get("depth_step_m_at_10000") > 0.0);
        }
        Engine again;
        again.openHeadless();
        again.seed(7);
        TestSceneInfo second;
        buildTestScene(again, name, TestSceneKit{}, second);
        applyTestSceneCamera(again, second);
        again.runHeadless(name == "streaming_route" ? 900 : 120, 1.0f / 60.0f);
        CHECK(again.dumpStateText() == engine.dumpStateText());
    }
    TestSceneInfo none;
    Engine engine;
    engine.openHeadless();
    CHECK(!buildTestScene(engine, "missing", TestSceneKit{}, none));
    CHECK(forwardDepthStep(0.1f, 100000.0f, 10000.0f) > forwardDepthStep(0.5f, 100000.0f, 10000.0f));
}

int main() {
    testRng();
    testJson();
    testReplayFormat();
    testHeadlessDeterminism();
    testRecordReplay();
    testInputActionsSource();
    testStateDump();
    testPerfLog();
    testParams();
    testEngineParamsSystem();
    testPostFxAndDataReload();
    testRenderMaskFlag();
    testTestScenes();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
