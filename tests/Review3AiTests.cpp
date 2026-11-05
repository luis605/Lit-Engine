#include "Review3Suites.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <limits>
#include <string>

import Engine.AI;
import Engine.Rng;
import Engine.Json;

namespace {

void testRepeatHugeCount() {
    BehaviorGraph graph;
    std::string error;
    CHECK(graph.loadText(R"({"root":{"type":"repeat","count":1e30,"child":{"type":"action","action":"a"}}})", &error));
    ActionRegistry actions;
    int calls = 0;
    actions.add("a", [&](AiContext&) {
        ++calls;
        return Status::Success;
    });
    Pcg32 rng(1);
    BehaviorRunner runner(graph, actions, rng);
    for (int tick = 0; tick < 5; ++tick) CHECK(runner.tick(0.016f) == Status::Running);
    CHECK(calls == 5);
}

void testMalformedConditionLists() {
    BehaviorGraph graph;
    std::string error;
    CHECK(!graph.loadText(R"({"root":{"type":"condition","when":{"all":"yes"}}})", &error));
    BehaviorGraph negative;
    CHECK(!negative.loadText(R"({"root":{"type":"repeat","count":"x","child":{"type":"wait"}}})", &error));
}

void testDeepAndUnknown() {
    std::string text = R"({"type":"wait"})";
    for (int depth = 0; depth < 300; ++depth) text = R"({"type":"inverter","child":)" + text + "}";
    BehaviorGraph graph;
    std::string error;
    graph.loadText("{\"root\":" + text + "}", &error);
    BehaviorGraph unknown;
    CHECK(!unknown.loadText(R"({"root":{"type":"sequence","children":[{"type":"nope"}]}})", &error));
    CHECK(!unknown.valid());
    BehaviorGraph missing;
    CHECK(!missing.loadText(R"({"root":{"type":"fsm","states":{"a":{"node":{"type":"wait"},"transitions":[{"to":"zzz","after":1}]}}}})", &error));
}

void testNanInputDoesNotPoisonUtility() {
    BehaviorGraph graph;
    CHECK(graph.loadText(R"({"root":{"type":"utility","options":[
      {"name":"a","considerations":[{"input":"x"}],"node":{"type":"action","action":"a"}},
      {"name":"b","considerations":[{"input":"y"}],"node":{"type":"action","action":"b"}}]}})"));
    ActionRegistry actions;
    actions.add("a", [](AiContext&) { return Status::Success; });
    actions.add("b", [](AiContext&) { return Status::Success; });
    Pcg32 rng(1);
    BehaviorRunner runner(graph, actions, rng);
    runner.blackboard().setNumber("x", std::numeric_limits<double>::quiet_NaN());
    runner.blackboard().setNumber("y", 0.5);
    runner.tick(0.016f);
    CHECK(runner.trace().lastWinner("utility#0") == "b");
}

void testReloadedGraphResizesRunner() {
    BehaviorGraph graph;
    CHECK(graph.loadText(R"({"root":{"type":"wait","seconds":10}})"));
    ActionRegistry actions;
    Pcg32 rng(1);
    BehaviorRunner runner(graph, actions, rng);
    CHECK(runner.tick(0.1f) == Status::Running);
    CHECK(graph.loadText(R"({"root":{"type":"sequence","children":[{"type":"wait","seconds":10},{"type":"wait","seconds":10},{"type":"wait","seconds":10}]}})"));
    CHECK(runner.tick(0.1f) == Status::Running);
    CHECK(runner.activePath().size() == 2);
}

}

void runReview3AiTests() {
    testRepeatHugeCount();
    testMalformedConditionLists();
    testDeepAndUnknown();
    testNanInputDoesNotPoisonUtility();
    testReloadedGraphResizesRunner();
}
