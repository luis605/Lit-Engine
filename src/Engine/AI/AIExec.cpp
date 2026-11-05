module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

module Engine.AI;

#include "AIInternal.hpp"

namespace AiDetail {

namespace {
const char* statusText(Status status) {
    if (status == Status::Running) return "running";
    return status == Status::Success ? "success" : "failure";
}
}

void Exec::resetSubtree(int id) {
    const size_t first = static_cast<size_t>(id);
    const size_t last = static_cast<size_t>(graph.nodes[first].subtreeEnd);
    std::fill(runtime.states.begin() + first, runtime.states.begin() + last, NodeState{});
    std::fill(runtime.currentState.begin() + first, runtime.currentState.begin() + last, -1);
    std::fill(runtime.option.begin() + first, runtime.option.begin() + last, -1);
    std::fill(runtime.running.begin() + first, runtime.running.begin() + last, uint8_t{0});
}

void Exec::emit(const Node& node, const char* kind, std::string_view winner, std::string_view reason, std::vector<TraceOption> options) {
    if (!trace.enabled()) return;
    TraceEvent event;
    event.tick = tick;
    event.agent = agent;
    event.node = node.name;
    event.kind = kind;
    event.winner = winner;
    event.reason = reason;
    event.options = std::move(options);
    trace.record(std::move(event));
}

Status Exec::run(int id) {
    const Status status = exec(id);
    runtime.running[static_cast<size_t>(id)] = status == Status::Running ? 1 : 0;
    if (status != Status::Running) resetSubtree(id);
    return status;
}

Status Exec::execComposite(const Node& node, NodeState& state) {
    const bool isSequence = node.kind == Kind::Sequence;
    if (node.reactive) state.index = 0;
    const int childCount = static_cast<int>(node.children.size());
    for (int index = state.index; index < childCount; ++index) {
        if (node.reactive && state.last >= 0 && state.last != index && runtime.running[static_cast<size_t>(node.children[static_cast<size_t>(state.last)])]) {
            resetSubtree(node.children[static_cast<size_t>(state.last)]);
            state.last = -1;
        }
        const Status status = run(node.children[static_cast<size_t>(index)]);
        if (status == Status::Running) {
            state.index = node.reactive ? 0 : index;
            state.last = index;
            return Status::Running;
        }
        if (isSequence ? status == Status::Failure : status == Status::Success) return status;
    }
    return isSequence ? Status::Success : Status::Failure;
}

Status Exec::execParallel(const Node& node) {
    int successes = 0;
    int failures = 0;
    for (const int child : node.children) {
        const Status status = run(child);
        if (status == Status::Success) ++successes;
        if (status == Status::Failure) ++failures;
    }
    const int total = static_cast<int>(node.children.size());
    if (node.parallelAny) {
        if (successes > 0) return Status::Success;
        return failures == total ? Status::Failure : Status::Running;
    }
    if (failures > 0) return Status::Failure;
    return successes == total ? Status::Success : Status::Running;
}

Status Exec::execDecorator(const Node& node, NodeState& state) {
    const Status status = run(node.children[0]);
    if (status == Status::Running) return status;
    switch (node.kind) {
    case Kind::Inverter: return status == Status::Success ? Status::Failure : Status::Success;
    case Kind::Succeeder: return Status::Success;
    case Kind::Failer: return Status::Failure;
    default: break;
    }
    if (status == Status::Failure) return Status::Failure;
    ++state.counter;
    if (node.count >= 0 && state.counter >= node.count) return Status::Success;
    return Status::Running;
}

void Exec::execSet(const Node& node) {
    if (node.add) {
        blackboard.setNumber(node.key, blackboard.number(node.key) + toNumber(node.value));
        return;
    }
    if (node.valueKey.empty()) {
        blackboard.set(node.key, node.value);
        return;
    }
    if (const Json* source = blackboard.get(node.valueKey)) blackboard.set(node.key, *source);
}

Status Exec::execAction(const Node& node) {
    const ActionFn*& action = runtime.actions[static_cast<size_t>(&node - graph.nodes.data())];
    if (!action) action = actions.find(node.action);
    if (!action) {
        emit(node, "action", node.action, "unknown action");
        return Status::Failure;
    }
    AiContext context{blackboard, rng, dt, tick, agent};
    const Status status = (*action)(context);
    emit(node, "action", node.action, statusText(status));
    return status;
}

Status Exec::exec(int id) {
    const Node& node = graph.nodes[static_cast<size_t>(id)];
    NodeState& state = runtime.states[static_cast<size_t>(id)];
    switch (node.kind) {
    case Kind::Sequence:
    case Kind::Selector: return execComposite(node, state);
    case Kind::Parallel: return execParallel(node);
    case Kind::Inverter:
    case Kind::Succeeder:
    case Kind::Failer:
    case Kind::Repeat: return execDecorator(node, state);
    case Kind::Wait:
        state.timer += dt;
        return state.timer >= node.seconds ? Status::Success : Status::Running;
    case Kind::Condition: return evaluateCondition(node.condition, blackboard) ? Status::Success : Status::Failure;
    case Kind::Set:
        execSet(node);
        return Status::Success;
    case Kind::Action: return execAction(node);
    case Kind::Utility: return utility(id);
    case Kind::Fsm: return fsm(id);
    }
    return Status::Failure;
}

}
