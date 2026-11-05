#include "TestSupport.hpp"
#include "SystemsSuites.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

import Engine.AI;
import Engine.Nav;
import Engine.Rig;
import Engine.Face;
import Engine.Audio;
import Engine.AudioTrace;
import Engine.Geometry;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Params;
import Engine.glm;
import Engine.Json;
import Engine.Rng;

static std::string tempPath(const std::string& name) { return testsupport::tempFilePath("lit_systems_", name); }

static bool nearlyEqual(float a, float b, float eps = 1.0e-3f) { return std::fabs(a - b) <= eps; }

static const char* kAgentGraph = R"({
  "name": "agent",
  "blackboard": {"hunger": 0.2, "fatigue": 0.1, "drive": 0.3, "alarm": 0.0},
  "root": {"type": "selector", "name": "top", "children": [
    {"type": "sequence", "name": "panic", "children": [
      {"type": "condition", "when": {"key": "alarm", "op": ">", "value": 0.5}},
      {"type": "action", "action": "flee"}]},
    {"type": "utility", "name": "needs", "stickiness": 0.05, "SELECT", "options": [
      {"name": "eat", "considerations": [{"input": "hunger", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "eat"}},
      {"name": "rest", "considerations": [{"input": "fatigue", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "rest"}},
      {"name": "work", "considerations": [{"input": "drive"}], "node": {"type": "action", "action": "work"}}]}
  ]}
})";

static std::string agentGraph(bool weighted) {
    std::string t = kAgentGraph;
    const std::string key = "\"SELECT\"";
    t.replace(t.find(key), key.size(), weighted ? "\"select\": \"weighted\"" : "\"select\": \"best\"");
    return t;
}

static void registerAgentActions(ActionRegistry& actions, int* counts) {
    actions.add("flee", [counts](AiContext&) {
        ++counts[3];
        return Status::Success;
    });
    actions.add("eat", [counts](AiContext& c) {
        ++counts[0];
        c.blackboard.setNumber("hunger", std::max(0.0, c.blackboard.number("hunger") - 0.5));
        return Status::Success;
    });
    actions.add("rest", [counts](AiContext& c) {
        ++counts[1];
        c.blackboard.setNumber("fatigue", std::max(0.0, c.blackboard.number("fatigue") - 0.5));
        return Status::Success;
    });
    actions.add("work", [counts](AiContext& c) {
        ++counts[2];
        c.blackboard.setNumber("hunger", c.blackboard.number("hunger") + 0.07);
        c.blackboard.setNumber("fatigue", c.blackboard.number("fatigue") + 0.05);
        return Status::Success;
    });
}

struct AgentRun {
    uint64_t hash = 0;
    std::string trace;
    int counts[4]{};
};

static AgentRun runAgent(bool weighted, uint64_t seed) {
    BehaviorGraph graph;
    std::string error;
    CHECK(graph.loadText(agentGraph(weighted), &error));
    AgentRun run;
    ActionRegistry actions;
    registerAgentActions(actions, run.counts);
    RandomService random;
    random.seed(seed);
    BehaviorRunner runner(graph, actions, random.stream("ai.agent"));
    for (int i = 0; i < 300; ++i) {
        if (i == 150) runner.blackboard().setNumber("alarm", 1.0);
        if (i == 160) runner.blackboard().setNumber("alarm", 0.0);
        runner.tick(1.0f / 30.0f);
    }
    run.hash = runner.stateHash();
    run.trace = runner.trace().toJson().dump();
    return run;
}

static void testAi() {
    const AgentRun a = runAgent(false, 7);
    const AgentRun b = runAgent(false, 7);
    CHECK(a.hash == b.hash);
    CHECK(a.trace == b.trace);
    CHECK(a.counts[0] > 0 && a.counts[1] > 0 && a.counts[2] > 0);
    CHECK(a.counts[3] == 10);

    const AgentRun w1 = runAgent(true, 1);
    const AgentRun w1b = runAgent(true, 1);
    const AgentRun w2 = runAgent(true, 2);
    CHECK(w1.hash == w1b.hash && w1.trace == w1b.trace);
    CHECK(w1.trace != w2.trace);

    BehaviorGraph graph;
    CHECK(graph.loadText(agentGraph(false)));
    int counts[4]{};
    ActionRegistry actions;
    registerAgentActions(actions, counts);
    Pcg32 rng(1);
    BehaviorRunner runner(graph, actions, rng);
    runner.blackboard().setNumber("hunger", 0.9);
    runner.blackboard().setNumber("fatigue", 0.1);
    runner.tick(0.1f);
    const auto events = runner.trace().forTick(0);
    const TraceEvent* needs = nullptr;
    for (const TraceEvent* e : events) {
        if (e->node == "needs") needs = e;
    }
    CHECK(needs != nullptr);
    if (needs) {
        CHECK(needs->winner == "eat");
        CHECK(needs->options.size() == 3);
        CHECK(needs->reason.find("highest score") != std::string::npos);
        CHECK(needs->reason.find("'work'") != std::string::npos || needs->reason.find("'rest'") != std::string::npos);
        CHECK(needs->options[0].considerations.size() == 1 && nearlyEqual(static_cast<float>(needs->options[0].considerations[0].value), 0.9f));
        CHECK(needs->options[0].score > needs->options[1].score);
    }
    const std::string path = tempPath("trace.json");
    CHECK(runner.trace().writeFile(path));
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    const auto parsed = Json::parse(ss.str());
    CHECK(parsed && parsed->get("events") && parsed->get("events")->size() == runner.trace().events().size());
    CHECK(runner.trace().writeFile(path, true));
    std::filesystem::remove(path);
    CHECK(runner.trace().toText().find("eat") != std::string::npos);

    BehaviorGraph bad;
    std::string error;
    CHECK(!bad.loadText(R"({"root":{"type":"teleport"}})", &error));
    CHECK(error.find("unknown node type") != std::string::npos);
    CHECK(!bad.loadText(R"({"root":{"type":"utility","options":[{"name":"a","node":{"type":"wait"},"considerations":[{"input":"x","curve":[[1,0],[0,1]]}]}]}})", &error));

    BehaviorGraph fsm;
    CHECK(fsm.loadText(R"({"root":{"type":"fsm","name":"mode","initial":"idle","states":{
      "idle":{"node":{"type":"wait","seconds":100},"transitions":[{"to":"work","when":{"key":"job","op":">","value":0}}]},
      "work":{"node":{"type":"action","action":"work"},"transitions":[{"to":"rest","after":0.35},{"to":"idle","when":{"key":"job","op":"==","value":0}}]},
      "rest":{"node":{"type":"wait","seconds":100},"transitions":[{"to":"$success","after":0.2}]}}}})"));
    Pcg32 rng2(1);
    int c2[4]{};
    ActionRegistry a2;
    registerAgentActions(a2, c2);
    BehaviorRunner fr(fsm, a2, rng2);
    fr.tick(0.1f);
    CHECK(fr.stateOf("mode") == "idle");
    fr.blackboard().setNumber("job", 1);
    fr.tick(0.1f);
    CHECK(fr.stateOf("mode") == "work");
    for (int i = 0; i < 4; ++i) fr.tick(0.1f);
    CHECK(fr.stateOf("mode") == "rest");
    Status last = Status::Running;
    for (int i = 0; i < 4 && last == Status::Running; ++i) last = fr.tick(0.1f);
    CHECK(last == Status::Success);
    const auto& evs = fr.trace().events();
    CHECK(std::any_of(evs.begin(), evs.end(), [](const TraceEvent& e) { return e.kind == "fsm" && e.winner == "work" && e.reason.find("job > 0") != std::string::npos; }));

    BehaviorGraph basic;
    CHECK(basic.loadText(R"({"root":{"type":"sequence","children":[{"type":"wait","seconds":0.45},{"type":"set","key":"done","value":true},{"type":"inverter","child":{"type":"condition","when":{"key":"missing"}}}]}})"));
    Pcg32 rng3(1);
    ActionRegistry none;
    BehaviorRunner br(basic, none, rng3);
    int running = 0;
    Status s = Status::Running;
    while (s == Status::Running && running < 20) {
        s = br.tick(0.1f);
        if (s == Status::Running) ++running;
    }
    CHECK(running == 4 && s == Status::Success && br.blackboard().boolean("done"));
}

void runAiTests() {
    testAi();
}
