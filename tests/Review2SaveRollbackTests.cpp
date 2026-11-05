#include "NarrativeFixtures.hpp"
#include "Review2Suites.hpp"
#include "TestSupport.hpp"
#include <memory>
#include <string>

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Events;
import Engine.Rng;
import Engine.Json;
import Engine.Params;
import Engine.State;
import Engine.Storylet;
import Engine.Observation;
import Engine.Save;

namespace {

constexpr int kThingCount = 6;

struct Rig {
    World world;
    StateStore state{&world.events()};
    ParamBus params;
    std::unique_ptr<StoryletEngine> story;
    std::unique_ptr<ObservationSystem> observation;
    std::unique_ptr<SaveSystem> save;

    explicit Rig(int thingCount) {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        world.random().seed(5);
        state.loadSchema(kSchema);
        params.declare("tension", 0.0f, 0.2f, 0.0f, 1.0f);
        story = std::make_unique<StoryletEngine>(state, world.random(), &world.events());
        story->loadText(kStorylets);
        observation = std::make_unique<ObservationSystem>(world);
        for (int index = 0; index < thingCount; ++index) {
            const EntityHandle thing = world.create("t" + std::to_string(index), 1, {static_cast<float>(index) * 3.0f, 0.0f, 4.0f});
            observation->track(thing);
        }
        save = std::make_unique<SaveSystem>(SaveContext{&world, &state, story.get(), &params, observation.get()});
        save->addSection(
            "extra", [] { return Json::object(); }, [this](const Json& data, std::string* error) {
                if (data.get("things") == nullptr) {
                    if (error) *error = "extra: missing things";
                    return false;
                }
                extraLoads++;
                return true;
            });
    }

    int extraLoads = 0;

    std::string fingerprint() const {
        Json body = save->capture();
        body.set("meta", Json::object());
        return body.dump();
    }
};

Json corrupted(const Json& good, const std::string& section) {
    Json body = good;
    if (section == "world") body.set("world", Json("not a world"));
    if (section == "state") body.set("state", Json(5));
    if (section == "storylets") body.set("storylets", Json("junk"));
    if (section == "observation") body.set("observation", Json(7));
    if (section == "params") {
        Json params = Json::object();
        params.set("tension", Json::object());
        body.set("params", params);
    }
    if (section == "random") {
        Json stream = Json::object();
        stream.set("state", Json("zz"));
        stream.set("increment", Json("1"));
        Json streams = Json::object();
        streams.set("drift", stream);
        Json random = Json::object();
        random.set("streams", streams);
        body.set("random", random);
    }
    if (section == "extra") {
        Json sections = Json::object();
        sections.set("extra", Json::object());
        body.set("sections", sections);
    }
    return body;
}

void testCorruptSectionsRollBack() {
    Rig source(kThingCount);
    source.state.add("attends", 4);
    source.params.set("tension", 0.9f);
    Json good = source.save->capture();
    Json sections = Json::object();
    Json extra = Json::object();
    extra.set("things", kThingCount);
    sections.set("extra", extra);
    good.set("sections", sections);

    const char* names[] = {"world", "state", "storylets", "observation", "params", "random", "extra"};
    for (const char* name : names) {
        Rig target(2);
        target.state.add("attends", 9);
        target.state.add("drift", 0.5);
        target.world.random().stream("drift").range(0.0f, 1.0f);
        const std::string before = target.fingerprint();
        const uint64_t serialBefore = target.save->serial();
        std::string error;
        CHECK(!target.save->apply(corrupted(good, name), &error));
        CHECK(!error.empty());
        CHECK(target.fingerprint() == before);
        CHECK(target.save->serial() == serialBefore);
        CHECK(target.state.get("attends") == 9.0);
    }

    Rig target(2);
    CHECK(target.save->apply(good));
    CHECK(target.state.get("attends") == 4.0);
}

}

void runReview2SaveRollbackTests() {
    testCorruptSectionsRollBack();
}
