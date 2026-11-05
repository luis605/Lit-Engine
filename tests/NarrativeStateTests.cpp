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

}

void runStateTests() {
    testState();
}
