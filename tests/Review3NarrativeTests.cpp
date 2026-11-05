#include "Review3Suites.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Events;
import Engine.Rng;
import Engine.Json;
import Engine.State;
import Engine.Storylet;
import Engine.Observation;
import Engine.Params;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

void testStateFromJsonAtomic() {
    StateStore state;
    CHECK(state.loadSchema(R"({"vars":{"a":{"type":"range"},"b":{"type":"set"}}})"));
    state.set("a", 0.5);
    state.addMember("b", "x");
    const auto doc = Json::parse(R"({"a":{"value":0.9},"b":{"members":["y",3]}})");
    CHECK(doc.has_value());
    std::string error;
    CHECK(!state.fromJson(*doc, &error));
    CHECK(state.get("a") == 0.5);
    CHECK(state.hasMember("b", "x"));
    const auto unknown = Json::parse(R"({"a":{"value":0.9},"zzz":{"value":1}})");
    CHECK(!state.fromJson(*unknown, &error));
    CHECK(state.get("a") == 0.5);
}

struct StoryFixture {
    World world;
    StateStore state{&world.events()};
    std::unique_ptr<StoryletEngine> story;
    StoryFixture() {
        state.loadSchema(R"({"vars":{"go":{"type":"flag","default":true}}})");
        story = std::make_unique<StoryletEngine>(state, world.random(), &world.events());
    }
};

void testStoryletHostileNumbers() {
    StoryFixture f;
    std::string error;
    CHECK(f.story->loadText(R"({"storylets":[{"id":"p","priority":1e30,"when":"go","lines":[{"text_id":"t","seconds":1}]}]})", &error));
    CHECK(f.story->find("p")->priority > 0);
    CHECK(f.story->loadText(R"({"storylets":[{"id":"n","priority":-1e30,"when":"go"}]})", &error));
    CHECK(f.story->find("n")->priority < 0);
    const auto history = Json::parse(R"({"time":5,"played":{"p":{"count":-3,"last":1e300}},"resume":{"p":1e30},"blocked":{"p":-1e30}})");
    CHECK(f.story->fromJson(*history, &error));
    CHECK(f.story->playCount("p") == 0);
    f.story->update(kNan);
    CHECK(std::isfinite(f.story->time()));
    f.story->update(-1.0f);
    CHECK(f.story->time() >= 5.0);
}

void testStoryletReentrantInterrupt() {
    StoryFixture f;
    CHECK(f.story->loadText(R"({"storylets":[{"id":"s","when":"go","once":true,"lines":[{"text_id":"a","seconds":0.1},{"text_id":"b","seconds":0.1}]}]})"));
    StoryletEngine* engine = f.story.get();
    f.world.events().subscribe<StoryletLineEnded>([engine](const StoryletLineEnded&) { engine->interrupt("test"); });
    for (int step = 0; step < 20; ++step) f.story->update(0.06f);
    CHECK(f.story->current().has_value() || f.story->playCount("s") >= 0);
    StoryFixture started;
    CHECK(started.story->loadText(R"({"storylets":[{"id":"s","when":"go","lines":[{"text_id":"a","seconds":5}]}]})"));
    StoryletEngine* second = started.story.get();
    started.world.events().subscribe<StoryletStarted>([second](const StoryletStarted&) { second->interrupt("early"); });
    for (int step = 0; step < 10; ++step) started.story->update(0.5f);
    CHECK(started.story->time() > 0.0);
}

struct ObserveFixture {
    World world;
    EntityHandle target;
    ObservationSystem system{world};
    ObserveFixture() {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        target = world.create("t", 1, {0.0f, 0.0f, -10.0f});
        system.track(target);
    }
};

void testObservationDegenerateViews() {
    ObserveFixture f;
    ObservationView view;
    view.forward = glm::vec3(0.0f, 1.0f, 0.0f);
    view.up = glm::vec3(0.0f, 1.0f, 0.0f);
    f.system.setView(view);
    for (int step = 0; step < 30; ++step) f.system.update(0.05f);
    CHECK(f.system.state(f.target) == ObservationState::Unobserved);
    view = ObservationView{};
    view.origin = glm::vec3(kNan);
    f.system.setView(view);
    for (int step = 0; step < 30; ++step) f.system.update(0.05f);
    CHECK(f.system.state(f.target) == ObservationState::Unobserved);
}

void testObservationUntrackInHandler() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    std::vector<EntityHandle> entities;
    ObservationSystem system(world);
    for (int index = 0; index < 8; ++index) {
        entities.push_back(world.create("e", 1, {static_cast<float>(index) * 0.1f, 0.0f, -10.0f}));
        system.track(entities.back());
    }
    world.events().subscribe<ObservationChanged>([&](const ObservationChanged&) {
        for (const EntityHandle entity : entities) system.untrack(entity);
    });
    for (int step = 0; step < 20; ++step) system.update(0.05f);
    CHECK(system.trackedEntities().empty());
}

void testObservationFromJsonHostile() {
    ObserveFixture f;
    const auto doc = Json::parse(R"({"time":1e300,"frame":-5,"tracked":[{"index":-1,"state":9},{"index":1e30,"state":-4},{"index":0,"state":7,"candidate":-9,"since":1e300}]})");
    CHECK(doc.has_value());
    if (doc) CHECK(f.system.fromJson(*doc));
    f.system.update(0.1f);
}


void testParamsHostile() {
    ParamBus bus;
    bus.declare("x", 0.5f, 0.0f, 0.0f, 1.0f);
    bus.set("x", kNan);
    bus.snap("x", std::numeric_limits<float>::infinity());
    bus.update(kNan);
    bus.update(0.016f);
    CHECK(std::isfinite(bus.get("x")));
    bus.declare("y", kNan, kNan);
    bus.update(0.016f);
    CHECK(std::isfinite(bus.get("y")));
    std::string error;
    CHECK(!bus.loadText(R"({"params":{"a":{}},"mappings":{"m":{"input":"a","curve":[[0,0],[1,1]],"min":5,"max":1}}})", &error));
    CHECK(!bus.has("m"));
}

}

void runReview3NarrativeTests() {
    testParamsHostile();
    testStateFromJsonAtomic();
    testStoryletHostileNumbers();
    testStoryletReentrantInterrupt();
    testObservationDegenerateViews();
    testObservationUntrackInHandler();
    testObservationFromJsonHostile();
}
