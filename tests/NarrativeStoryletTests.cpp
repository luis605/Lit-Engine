#include "TestSupport.hpp"
#include "NarrativeFixtures.hpp"
#include "NarrativeSuites.hpp"
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

}

void runStoryletTests() {
    testStoryletBasics();
    testStoryletInterruptAndPriority();
    testStoryletDeterminism();
}
