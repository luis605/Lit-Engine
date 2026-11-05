#include "TestSupport.hpp"
#include "NarrativeFixtures.hpp"
#include "NarrativeSuites.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

void testAsyncSave() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lit_narrative_async";
    fs::remove_all(dir);
    Sim a;
    for (int i = 0; i < 20; ++i) a.step(i);
    const std::string path = (dir / "async.litsave").string();
    CHECK(a.save->saveAsync(path, "async"));
    CHECK(a.save->serial() == 1);
    CHECK(a.save->waitPending());
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = Json::parse(text);
    CHECK(parsed.has_value());
    CHECK(parsed->dump() == text);
    Sim b;
    std::string err;
    CHECK(b.save->load(path, &err));
    CHECK(b.world.time().frame == a.world.time().frame);
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

}

void runSaveTests() {
    testSaveContinuation();
    testAutosave();
    testAsyncSave();
    testMigration();
}
