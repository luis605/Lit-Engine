#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Harness.hpp"

import Engine.AI;
import Engine.Rng;

namespace bench {

namespace {

constexpr size_t kAgentCount = 1000;
constexpr float kTickSeconds = 1.0f / 30.0f;
constexpr double kNeedGrowth = 0.01;
constexpr double kNeedRelief = 0.5;

const char* kTreeGraph = R"({
  "name": "agent",
  "blackboard": {"hunger": 0.2, "fatigue": 0.1, "drive": 0.3, "alarm": 0.0},
  "root": {"type": "selector", "name": "top", "children": [
    {"type": "sequence", "name": "panic", "children": [
      {"type": "condition", "when": {"key": "alarm", "op": ">", "value": 0.5}},
      {"type": "action", "action": "flee"}]},
    {"type": "utility", "name": "needs", "stickiness": 0.05, "select": "best", "options": [
      {"name": "eat", "considerations": [{"input": "hunger", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "eat"}},
      {"name": "rest", "considerations": [{"input": "fatigue", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "rest"}},
      {"name": "work", "considerations": [{"input": "drive"}], "node": {"type": "action", "action": "work"}}]}
  ]}
})";

const char* kSequenceGraph = R"({
  "name": "patrol",
  "blackboard": {"count": 0},
  "root": {"type": "sequence", "name": "loop", "children": [
    {"type": "set", "key": "count", "value": 1, "add": true},
    {"type": "condition", "when": {"key": "count", "op": ">=", "value": 0}},
    {"type": "inverter", "child": {"type": "condition", "when": {"key": "missing"}}},
    {"type": "action", "action": "work"}]}
})";

struct AiFixture {
    BehaviorGraph graph;
    ActionRegistry actions;
    Pcg32 rng{kSeed + 21};
    std::vector<std::unique_ptr<BehaviorRunner>> runners;
};

std::shared_ptr<AiFixture> buildFixture(const char* graphText, bool traceEnabled) {
    auto fixture = std::make_shared<AiFixture>();
    fixture->graph.loadText(graphText);
    fixture->actions.add("flee", [](AiContext&) { return Status::Success; });
    fixture->actions.add("eat", [](AiContext& context) {
        context.blackboard.setNumber("hunger", context.blackboard.number("hunger") - kNeedRelief);
        return Status::Success;
    });
    fixture->actions.add("rest", [](AiContext& context) {
        context.blackboard.setNumber("fatigue", context.blackboard.number("fatigue") - kNeedRelief);
        return Status::Success;
    });
    fixture->actions.add("work", [](AiContext& context) {
        context.blackboard.setNumber("hunger", context.blackboard.number("hunger") + kNeedGrowth);
        context.blackboard.setNumber("fatigue", context.blackboard.number("fatigue") + kNeedGrowth);
        return Status::Success;
    });
    for (size_t index = 0; index < kAgentCount; ++index) {
        fixture->runners.push_back(std::make_unique<BehaviorRunner>(fixture->graph, fixture->actions, fixture->rng, static_cast<uint32_t>(index)));
        fixture->runners.back()->trace().setEnabled(traceEnabled);
    }
    return fixture;
}

Case tickCase(const char* graphText, bool traceEnabled) {
    auto fixture = buildFixture(graphText, traceEnabled);
    Case benchCase;
    benchCase.itemsPerIteration = kAgentCount;
    benchCase.iterations = 40;
    benchCase.warmup = 5;
    benchCase.run = [fixture] {
        for (auto& runner : fixture->runners) runner->tick(kTickSeconds);
        sink(static_cast<double>(fixture->runners.front()->tickCount()));
    };
    return benchCase;
}

Case blackboardCase() {
    auto board = std::make_shared<Blackboard>();
    board->setNumber("hunger", 0.5);
    board->setNumber("fatigue", 0.5);
    board->setBool("alarm", false);
    Case benchCase;
    benchCase.itemsPerIteration = kAgentCount;
    benchCase.iterations = 40;
    benchCase.warmup = 5;
    benchCase.run = [board] {
        double total = 0.0;
        for (size_t index = 0; index < kAgentCount; ++index) {
            board->setNumber("hunger", total);
            total += board->number("hunger") + board->number("fatigue") + (board->boolean("alarm") ? 1.0 : 0.0);
        }
        sink(total);
    };
    return benchCase;
}

}

void registerAiBenches() {
    add("ai.tick_1000_utility_trace_off", [] { return tickCase(kTreeGraph, false); });
    add("ai.tick_1000_utility_trace_on", [] { return tickCase(kTreeGraph, true); });
    add("ai.tick_1000_sequence", [] { return tickCase(kSequenceGraph, false); });
    add("ai.blackboard_1000_rw", blackboardCase);
}

}
