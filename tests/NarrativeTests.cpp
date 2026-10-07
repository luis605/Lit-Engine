#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.camera;
import Engine.Events;
import Engine.Rng;
import Engine.Json;
import Engine.Params;
import Engine.State;
import Engine.Storylet;
import Engine.Observation;
import Engine.Save;
import Engine.CameraRig;

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
#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

const char* kSchema = R"({
  "vars": {
    "bond": {"type": "range", "min": 0, "max": 1, "default": 0.25},
    "drift": {"type": "range", "min": 0, "max": 1},
    "capacity": {"type": "counter", "min": 1, "max": 6, "default": 3},
    "attends": {"type": "counter", "min": 0, "max": 1000000},
    "met": {"type": "flag"},
    "location_tag": {"type": "set"}
  }
})";

void testState() {
    EventBus bus;
    std::vector<StateChanged> events;
    bus.subscribe<StateChanged>([&](const StateChanged& e) { events.push_back(e); });
    StateStore s(&bus);
    std::string err;
    CHECK(s.loadSchema(kSchema, &err));
    CHECK(std::abs(s.get("bond") - 0.25) < 1e-9);
    CHECK(s.get("capacity") == 3.0);
    CHECK(s.set("bond", 5.0));
    CHECK(s.get("bond") == 1.0);
    CHECK(events.size() == 1 && events[0].oldValue == 0.25 && events[0].newValue == 1.0);
    CHECK(s.set("bond", 1.0));
    CHECK(events.size() == 1);
    CHECK(s.add("bond", -2.0));
    CHECK(s.get("bond") == 0.0);
    CHECK(s.add("capacity", 10));
    CHECK(s.get("capacity") == 6.0);
    CHECK(s.add("capacity", -10));
    CHECK(s.get("capacity") == 1.0);
    CHECK(s.set("attends", 2.7));
    CHECK(s.get("attends") == 2.0);
    CHECK(!s.flag("met"));
    CHECK(s.setFlag("met", true));
    CHECK(s.flag("met"));
    CHECK(!s.set("location_tag", 1.0));
    CHECK(!s.set("missing", 1.0));
    events.clear();
    CHECK(s.addMember("location_tag", "home"));
    CHECK(s.addMember("location_tag", "home"));
    CHECK(events.size() == 1 && events[0].added && events[0].member == "home" && events[0].newValue == 1.0);
    CHECK(s.hasMember("location_tag", "home"));
    CHECK(s.removeMember("location_tag", "home"));
    CHECK(events.size() == 2 && !events[1].added);
    CHECK(!s.set("bond", std::nan("")));

    Json saved = s.toJson();
    StateStore t;
    CHECK(t.loadSchema(kSchema));
    CHECK(t.fromJson(saved));
    CHECK(jsonDiff(saved, t.toJson()).empty());

    const char* bad[] = {
        R"({"vars": {"x": {"type": "range", "min": 2, "max": 1}}})",
        R"({"vars": {"x": {"type": "bogus"}}})",
        R"({"vars": {"x": {"type": "range", "extra": 1}}})",
        R"({"vars": {"x": {"type": "range", "default": 5}}})",
        R"({"vars": {"x": {"type": "set", "default": 1}}})",
        R"({"vars": 3})",
        R"({"nope": {}})",
        R"({"vars": {"x": {"type": "range", "min": "a"}}})",
    };
    for (const char* text : bad) {
        StateStore u;
        CHECK(!u.loadSchema(text, &err));
        CHECK(!err.empty());
        CHECK(u.vars().empty());
    }
}

const char* kStorylets = R"({
  "storylets": [
    {"id": "coffee", "when": {"all": ["bond >= 0.2", {"location_tag": "home"}, {"not_played": "coffee"}]},
     "priority": 40, "cooldown_seconds": 1200, "delivery": "walking_talk",
     "lines": [{"speaker": "a", "text_id": "coffee_a", "voice_id": "coffee_a", "seconds": 2}, {"speaker": "b", "text_id": "coffee_b", "seconds": 2}],
     "effects": ["bond += 0.05", {"remember": "habit.coffee"}]},
    {"id": "hello", "when": "bond >= 0.2", "priority": 10, "cooldown_seconds": 5,
     "lines": [{"text_id": "hello_a", "seconds": 1}], "effects": ["attends += 1"]},
    {"id": "once_only", "when": {"any": ["met", {"not": "bond > 0.1"}]}, "priority": 5, "once": true,
     "lines": [{"text_id": "o", "seconds": 0.5}], "effects": ["met = false"]},
    {"id": "tie_a", "when": "attends >= 100", "priority": 7, "lines": [{"text_id": "ta", "seconds": 0.1}]},
    {"id": "tie_b", "when": "attends >= 100", "priority": 7, "lines": [{"text_id": "tb", "seconds": 0.1}]}
  ]
})";

struct StoryFixture {
    World world;
    StateStore state{&world.events()};
    std::unique_ptr<StoryletEngine> story;
    std::vector<std::string> log;
    StoryFixture() {
        state.loadSchema(kSchema);
        story = std::make_unique<StoryletEngine>(state, world.random(), &world.events());
        world.random().seed(11);
        world.events().subscribe<StoryletStarted>([this](const StoryletStarted& e) { log.push_back("start:" + e.id); });
        world.events().subscribe<StoryletCompleted>([this](const StoryletCompleted& e) { log.push_back("done:" + e.id); });
        world.events().subscribe<StoryletInterrupted>([this](const StoryletInterrupted& e) { log.push_back("int:" + e.id + ":" + e.reason); });
        world.events().subscribe<StoryletLineStarted>([this](const StoryletLineStarted& e) { log.push_back("line:" + e.textId); });
    }
    void run(float seconds, float dt = 0.1f) {
        for (float t = 0; t < seconds - 1e-6f; t += dt) story->update(dt);
    }
};

void testStoryletBasics() {
    StoryFixture f;
    std::string err;
    CHECK(f.story->loadText(kStorylets, &err));
    CHECK(f.story->count() == 5);
    CHECK(!f.story->eligible("coffee"));
    CHECK(f.story->eligible("hello"));
    f.state.addMember("location_tag", "home");
    CHECK(f.story->eligible("coffee"));
    CHECK(!f.story->eligible("once_only"));
    CHECK((f.story->eligibleIds() == std::vector<std::string>{"coffee", "hello"}));

    f.run(0.2f);
    CHECK(f.story->current() && f.story->current()->id == "coffee");
    f.run(6.5f);
    CHECK(f.story->played("coffee"));
    CHECK(f.state.hasMember("memory", "habit.coffee"));
    CHECK(std::abs(f.state.get("bond") - 0.30) < 1e-6);
    CHECK(f.log.size() >= 5 && f.log[0] == "start:coffee" && f.log[1] == "line:coffee_a" && f.log[2] == "line:coffee_b" && f.log[3] == "done:coffee");
    CHECK(!f.story->eligible("coffee"));
    CHECK(f.story->playCount("hello") >= 1);

    const uint32_t hellos = f.story->playCount("hello");
    f.run(3.0f);
    CHECK(f.story->playCount("hello") == hellos);
    f.run(4.0f);
    CHECK(f.story->playCount("hello") > hellos);

    f.state.set("bond", 0.0);
    CHECK(f.story->eligible("once_only"));
    f.run(2.0f);
    CHECK(f.story->played("once_only"));
    CHECK(!f.story->eligible("once_only"));
    f.state.set("bond", 0.0);
    f.run(2.0f);
    CHECK(f.story->playCount("once_only") == 1);

    StateStore other;
    other.loadSchema(kSchema);
    RandomService rng;
    StoryletEngine bad(other, rng);
    CHECK(!bad.loadText(R"({"storylets":[{"id":"x","when":"nope >= 1"}]})", &err));
    CHECK(!bad.loadText(R"({"storylets":[{"id":"x","effects":["bond ~= 1"]}]})", &err));
    CHECK(!bad.loadText(R"({"storylets":[{"id":"x","lines":[{"voice_id":"v"}]}]})", &err));
    CHECK(!bad.loadText(R"({"storylets":[{"id":"x","bogus":1}]})", &err));
    CHECK(!bad.loadText(R"({"storylets":[{"id":"x"},{"id":"x"}]})", &err));
    CHECK(bad.count() == 0);
}

void testStoryletInterruptAndPriority() {
    StoryFixture f;
    CHECK(f.story->loadText(R"({"storylets":[
      {"id":"low","when":"bond >= 0.2","priority":1,"lines":[{"text_id":"l0","seconds":1},{"text_id":"l1","seconds":1}]},
      {"id":"high","when":"met","priority":9,"lines":[{"text_id":"h0","seconds":0.5}]}]})"));
    f.run(1.2f);
    CHECK(f.story->current() && f.story->current()->id == "low" && f.story->current()->line == 1);
    f.state.setFlag("met", true);
    f.run(0.1f);
    CHECK(f.story->current() && f.story->current()->id == "high");
    bool interrupted = false;
    for (const auto& l : f.log) interrupted |= l == "int:low:outranked";
    CHECK(interrupted);
    f.state.setFlag("met", false);
    f.run(0.6f);
    CHECK(f.story->played("high"));
    CHECK(!f.story->current());
    f.run(1.0f);
    CHECK(f.story->current() && f.story->current()->id == "low" && f.story->current()->line == 1);
    CHECK(f.story->interrupt("moved_away"));
    CHECK(!f.story->current());
    CHECK(!f.story->played("low"));
    f.run(0.5f);
    CHECK(!f.story->current());
    f.run(1.2f);
    CHECK(f.story->current() && f.story->current()->line == 1);
    f.run(2.0f);
    CHECK(f.story->played("low"));
}

void testStoryletDeterminism() {
    auto run = [](uint64_t seed) {
        StoryFixture f;
        f.world.random().seed(seed);
        f.story->loadText(kStorylets);
        f.state.set("attends", 100);
        f.run(3.0f, 0.05f);
        return f.log;
    };
    CHECK(run(5) == run(5));
    bool differs = false;
    for (uint64_t seed = 1; seed < 20 && !differs; ++seed) differs = run(seed) != run(5);
    CHECK(differs);
}

struct ViewFixture {
    World world;
    EntityHandle a, b, d;
    std::vector<ObservationChanged> events;
    std::unique_ptr<ObservationSystem> obs;
    ViewFixture() {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        world.camera().setPos({0.0f, 0.0f, 0.0f});
        a = world.create("a", 1, {0.0f, 0.0f, -10.0f});
        b = world.create("b", 1, {10.0f, 0.0f, -10.0f});
        d = world.create("d", 1, {0.0f, 0.0f, -20.0f});
        obs = std::make_unique<ObservationSystem>(world);
        obs->track(a);
        obs->track(b);
        obs->track(d);
        world.events().subscribe<ObservationChanged>([this](const ObservationChanged& e) { events.push_back(e); });
        look(-90.0f);
    }
    void look(float yaw) {
        world.camera().setOrientation(yaw, 0.0f);
        obs->setView(observationViewFromCamera(world.camera(), 1.0f));
    }
    void run(float seconds, float dt = 1.0f / 60.0f) {
        for (float t = 0; t < seconds - 1e-6f; t += dt) obs->update(dt);
    }
};

void testObservationCpu() {
    ViewFixture f;
    const ObservationView v = f.obs->view();
    CHECK(std::abs(v.forward.z + 1.0f) < 1e-5f);

    f.obs->update(1.0f / 60.0f);
    CHECK(f.obs->state(f.a) == ObservationState::Unobserved);
    f.run(0.1f);
    CHECK(f.obs->state(f.a) == ObservationState::Seen);
    f.run(0.3f);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);
    CHECK(f.obs->focusEntity() == f.a);
    CHECK(f.obs->state(f.b) == ObservationState::Unobserved);
    CHECK(f.obs->state(f.d) == ObservationState::Unobserved);
    CHECK(f.events.size() == 2);
    CHECK(f.events[0].entity == f.a && f.events[0].to == ObservationState::Seen);
    CHECK(f.events[1].entity == f.a && f.events[1].from == ObservationState::Seen && f.events[1].to == ObservationState::Attended);

    const auto countA = [&] { size_t n = 0; for (const auto& e : f.events) n += e.entity == f.a; return n; };
    const size_t before = countA();
    f.look(-45.0f);
    f.run(0.1f);
    f.look(-90.0f);
    f.run(0.5f);
    CHECK(countA() == before);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);

    f.look(-45.0f);
    f.run(0.2f);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);
    f.run(0.4f);
    CHECK(f.obs->state(f.a) == ObservationState::Unobserved);
    CHECK(f.obs->state(f.b) == ObservationState::Attended);
    CHECK(f.obs->attended().size() == 1 && f.obs->attended()[0] == f.b);

    f.look(90.0f);
    f.run(1.0f);
    CHECK(f.obs->state(f.b) == ObservationState::Unobserved);
    CHECK(f.obs->stats().cpuUpdates > 0);

    f.world.destroy(f.a);
    f.look(-90.0f);
    f.run(0.5f);
    CHECK(!f.obs->tracked(f.a));
    CHECK(f.obs->state(f.d) != ObservationState::Unobserved);
}

void testObservationGpu() {
    ViewFixture f;
    ObservationConfig cfg = f.obs->config();
    cfg.logLatency = false;
    f.obs->setConfig(cfg);
    f.look(90.0f);
    std::vector<uint32_t> ids(64, 0);
    ids[4 * 8 + 4] = f.a.index + 1;
    for (int i = 0; i < 40; ++i) {
        f.obs->submitGpuIds(f.obs->frame(), f.obs->time(), 8, 8, ids);
        f.obs->update(1.0f / 60.0f);
    }
    CHECK(f.obs->stats().usingGpu);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);
    CHECK(f.obs->stats().gpuSamples == 40);
    CHECK(f.obs->stats().lastLatencyFrames == 0);

    ids.assign(64, 0);
    ids[0] = f.a.index + 1;
    for (int i = 0; i < 40; ++i) {
        f.obs->submitGpuIds(f.obs->frame(), f.obs->time(), 8, 8, ids);
        f.obs->update(1.0f / 60.0f);
    }
    CHECK(f.obs->state(f.a) == ObservationState::Seen);

    const uint64_t rejected = f.obs->stats().gpuRejected;
    f.obs->submitGpuIds(f.obs->frame() - 30, f.obs->time() - 0.5, 8, 8, ids);
    CHECK(f.obs->stats().gpuRejected == rejected + 1);
    CHECK(f.obs->stats().maxLatencySeconds >= 0.5);
    f.run(1.0f);
    CHECK(!f.obs->stats().usingGpu);
    CHECK(f.obs->state(f.a) == ObservationState::Unobserved);
}

struct Sim {
    World world;
    StateStore state{&world.events()};
    ParamBus params;
    std::unique_ptr<StoryletEngine> story;
    std::unique_ptr<ObservationSystem> obs;
    std::unique_ptr<SaveSystem> save;
    std::vector<EntityHandle> things;

    Sim() {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        world.random().seed(77);
        state.loadSchema(kSchema);
        params.declare("tension", 0.0f, 0.2f, 0.0f, 1.0f);
        story = std::make_unique<StoryletEngine>(state, world.random(), &world.events());
        story->loadText(kStorylets);
        obs = std::make_unique<ObservationSystem>(world);
        ObservationConfig cfg = obs->config();
        cfg.logLatency = false;
        obs->setConfig(cfg);
        world.camera().setPos({0.0f, 0.0f, 0.0f});
        for (int i = 0; i < 8; ++i) {
            const float ang = glm::radians(45.0f * static_cast<float>(i));
            things.push_back(world.create("t" + std::to_string(i), 1, {10.0f * std::cos(ang), 0.0f, 10.0f * std::sin(ang)}));
            obs->track(things.back());
        }
        world.events().subscribe<ObservationChanged>([this](const ObservationChanged& e) {
            if (e.to == ObservationState::Attended) {
                state.add("attends", 1);
                state.addMember("location_tag", "home");
            }
        });
        save = std::make_unique<SaveSystem>(SaveContext{&world, &state, story.get(), &params, obs.get()});
        save->addSection("extra", [this]() {
            Json j = Json::object();
            j.set("things", things.size());
            return j;
        }, [](const Json& j, std::string*) { return j.get("things") != nullptr; });
    }

    void step(int tick) {
        const float dt = 1.0f / 60.0f;
        world.camera().setOrientation(-180.0f + static_cast<float>(tick) * 0.9f, 0.0f);
        obs->setView(observationViewFromCamera(world.camera(), 1.0f));
        obs->update(dt);
        auto& rng = world.random().stream("drift");
        state.add("drift", rng.range(0.0f, 0.004f));
        if (tick % 90 == 0) state.add("bond", rng.range(0.0f, 0.2f));
        params.set("tension", static_cast<float>(state.get("drift")));
        params.update(dt);
        story->update(dt);
        world.timeState().frame++;
        world.timeState().elapsed += dt;
    }

    std::string fingerprint() const {
        const Json body = save->capture();
        Json out = Json::object();
        for (const auto& [k, v] : body.members()) {
            if (k != "meta") out.set(k, v);
        }
        return out.dump();
    }
};

void testSaveContinuation() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lit_narrative_tests";
    fs::remove_all(dir);
    const std::string path = (dir / "slot1.litsave").string();

    Sim full;
    for (int i = 0; i < 600; ++i) full.step(i);
    const std::string expected = full.fingerprint();
    CHECK(full.state.get("attends") > 0);
    CHECK(full.story->playCount("hello") > 0);

    Sim first;
    for (int i = 0; i < 300; ++i) first.step(i);
    std::string err;
    CHECK(first.save->save(path, "mid", &err));
    CHECK(fs::exists(path));
    CHECK(!fs::exists(path + ".tmp"));
    const std::string midPrint = first.fingerprint();

    Sim second;
    CHECK(second.save->load(path, &err));
    CHECK(second.fingerprint() == midPrint);
    for (int i = 300; i < 600; ++i) second.step(i);
    CHECK(second.fingerprint() == expected);

    for (int i = 300; i < 600; ++i) first.step(i);
    CHECK(first.fingerprint() == expected);

    CHECK(first.save->save(path, "again", &err));
    CHECK(fs::exists(path + ".bak"));

    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), {});
    }
    {
        std::string broken = text;
        const size_t at = broken.find("\"tension\"");
        CHECK(at != std::string::npos);
        broken[at + 2] = 'X';
        std::ofstream out(dir / "broken.litsave", std::ios::binary);
        out << broken;
    }
    Sim third;
    const std::string pristine = third.fingerprint();
    CHECK(!third.save->load((dir / "broken.litsave").string(), &err));
    CHECK(third.fingerprint() == pristine);
    {
        std::ofstream out(dir / "trunc.litsave", std::ios::binary);
        out << text.substr(0, text.size() / 2);
    }
    CHECK(!third.save->load((dir / "trunc.litsave").string(), &err));
    CHECK(!third.save->load((dir / "missing.litsave").string(), &err));

    auto doc = Json::parse(text);
    CHECK(doc.has_value());
    if (doc) {
        doc->set("version", 99);
        std::ofstream out(dir / "future.litsave", std::ios::binary);
        out << doc->dump();
    }
    CHECK(!third.save->load((dir / "future.litsave").string(), &err));
    CHECK(err.find("version") != std::string::npos);

    if (doc) {
        doc->set("version", 0);
        std::ofstream out(dir / "old.litsave", std::ios::binary);
        out << doc->dump();
    }
    CHECK(!third.save->load((dir / "old.litsave").string(), &err));

    fs::remove_all(dir);
}

void testAutosave() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lit_narrative_autosave";
    fs::remove_all(dir);
    Sim a;
    a.save->configureAutosave(dir.string(), 1.0f, 2);
    int saves = 0;
    for (int i = 0; i < 320; ++i) {
        a.step(i);
        if (a.save->autosave(1.0f / 60.0f)) ++saves;
    }
    CHECK(saves == 5);
    CHECK(fs::exists(a.save->autosavePath(0)));
    CHECK(fs::exists(a.save->autosavePath(1)));
    CHECK(!fs::exists(a.save->autosavePath(2)));
    Sim b;
    b.save->configureAutosave(dir.string(), 1.0f, 2);
    std::string err;
    CHECK(b.save->loadLatestAutosave(&err));
    CHECK(b.world.time().frame > 200);
    CHECK(b.save->serial() == 5);
    fs::remove(a.save->autosavePath(1));
    CHECK(b.save->loadLatestAutosave(&err));
    fs::remove_all(dir);
}

void testMigration() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lit_narrative_migrate";
    fs::remove_all(dir);
    Sim a;
    for (int i = 0; i < 30; ++i) a.step(i);
    std::string err;
    const std::string path = (dir / "m.litsave").string();
    CHECK(a.save->save(path, "m", &err));
    Sim b;
    b.save->addMigration(0, [](Json&, std::string*) { return true; });
    CHECK(b.save->load(path, &err));
    fs::remove_all(dir);
}

void testCameraRig() {
    World w;
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    const EntityHandle player = w.create("player", 1, {0.0f, 0.0f, 0.0f}, {3.0f, 3.0f, 3.0f});
    w.camera().setOrientation(-90.0f, 0.0f);
    CameraRig rig(w);
    rig.setTarget(player);
    rig.update();
    const glm::vec3 eye(0.0f, 1.6f, 0.0f);
    CHECK(glm::length(w.camera().getPosition() - eye) < 1e-5f);

    CHECK(rig.toggle() == CameraMode::ThirdPerson);
    CHECK(std::abs(w.camera().getPosition().z - 3.5f) < 1e-4f);
    CHECK(glm::length(rig.pivot() - eye) < 1e-5f);
    const glm::vec3 fpFwd = rig.forward();
    const ObservationView v = observationViewFromCamera(w.camera(), 1.0f);
    CHECK(glm::dot(v.forward, fpFwd) > 0.9999f);

    const EntityHandle wall = w.create("wall", 1, {0.0f, 1.6f, 4.5f});
    CHECK(rig.toggle() == CameraMode::FirstPerson);
    CHECK(glm::length(w.camera().getPosition() - eye) < 1e-5f);
    rig.toggle();
    CHECK(rig.currentDistance() < 3.5f);
    CHECK(std::abs(rig.currentDistance() - (3.5f - 0.25f)) < 0.3f);
    CHECK(rig.currentDistance() >= rig.settings().minDistance);
    w.setPosition(wall, {0.0f, 1.6f, 2.0f});
    rig.update();
    CHECK(rig.currentDistance() <= 1.0f - 0.25f + 1e-3f || rig.currentDistance() == rig.settings().minDistance);
    CHECK(glm::dot(observationViewFromCamera(w.camera(), 1.0f).forward, fpFwd) > 0.9999f);
    w.destroy(wall);
    rig.update();
    CHECK(std::abs(rig.currentDistance() - 3.5f) < 1e-4f);

    rig.look(0.0f, 500.0f);
    CHECK(w.camera().getPitch() <= rig.settings().maxPitch + 1e-4f);

    World free;
    free.camera().setPos({1.0f, 2.0f, 3.0f});
    free.camera().setOrientation(-90.0f, 0.0f);
    CameraRig freeRig(free);
    freeRig.update();
    CHECK(glm::length(free.camera().getPosition() - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-5f);
    freeRig.toggle();
    CHECK(glm::length(free.camera().getPosition() - glm::vec3(1.0f, 2.0f, 6.5f)) < 1e-4f);
    freeRig.update();
    CHECK(glm::length(freeRig.pivot() - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-4f);
    freeRig.toggle();
    CHECK(glm::length(free.camera().getPosition() - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-4f);
}
}

int main() {
    testState();
    testStoryletBasics();
    testStoryletInterruptAndPriority();
    testStoryletDeterminism();
    testObservationCpu();
    testObservationGpu();
    testSaveContinuation();
    testAutosave();
    testMigration();
    testCameraRig();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
