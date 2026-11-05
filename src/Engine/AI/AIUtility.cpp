module;

#include <algorithm>
#include <cmath>
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
constexpr double kMinCombineSeed = 1.0e30;
constexpr const char* kSuccessTarget = "$success";
constexpr const char* kFailureTarget = "$failure";

int stateIndexOf(const Node& node, const std::string& name) {
    for (size_t index = 0; index < node.states.size(); ++index) {
        if (node.states[index].name == name) return static_cast<int>(index);
    }
    return -1;
}

std::string transitionText(const Transition& transition) {
    std::string text;
    if (transition.after >= 0.0f) text = "after " + formatNumber(transition.after) + "s";
    if (transition.when.type != Condition::Type::Always) text += (text.empty() ? "" : " and ") + conditionText(transition.when);
    return text;
}

double combineSeed(CombineMode mode) {
    if (mode == CombineMode::Min) return kMinCombineSeed;
    return mode == CombineMode::Product ? 1.0 : 0.0;
}
}

bool Exec::transitionFires(const Transition& transition, const NodeState& state) const {
    if (transition.after >= 0.0f && state.stateTime < transition.after) return false;
    return evaluateCondition(transition.when, blackboard);
}

const Transition* Exec::firstFiredTransition(const Node& node, const NodeState& state, int current) const {
    const State& currentState = node.states[static_cast<size_t>(current)];
    for (const Transition& transition : node.global) {
        if (currentState.name != transition.to && transitionFires(transition, state)) return &transition;
    }
    for (const Transition& transition : currentState.transitions) {
        if (transitionFires(transition, state)) return &transition;
    }
    return nullptr;
}

Status Exec::fsm(int id) {
    const Node& node = graph.nodes[static_cast<size_t>(id)];
    NodeState& state = runtime.states[static_cast<size_t>(id)];
    int& current = runtime.currentState[static_cast<size_t>(id)];
    if (current < 0) {
        if (node.initial == kSuccessTarget) return Status::Success;
        if (node.initial == kFailureTarget) return Status::Failure;
        current = stateIndexOf(node, node.initial);
        state.stateTime = 0.0f;
        emit(node, "fsm", node.initial, "initial state");
    }
    state.stateTime += dt;
    if (const Transition* fired = firstFiredTransition(node, state, current)) {
        const State& currentState = node.states[static_cast<size_t>(current)];
        if (trace.enabled()) emit(node, "fsm", fired->to, "from " + currentState.name + " when " + transitionText(*fired));
        resetSubtree(currentState.child);
        if (fired->to == kSuccessTarget) return Status::Success;
        if (fired->to == kFailureTarget) return Status::Failure;
        current = stateIndexOf(node, fired->to);
        state.stateTime = 0.0f;
    }
    run(node.states[static_cast<size_t>(current)].child);
    return Status::Running;
}

double Exec::score(const Node& node, const Option& option, bool& available, TraceOption* traceOption) const {
    available = true;
    if (traceOption) {
        traceOption->name = option.name;
        traceOption->considerations.reserve(option.considerations.size());
    }
    if (option.hasGate && !evaluateCondition(option.gate, blackboard)) {
        available = false;
        if (traceOption) traceOption->available = false;
        return -1.0;
    }
    double result = combineSeed(node.combine);
    double sum = 0.0;
    for (const Consideration& consideration : option.considerations) {
        const double rawValue = blackboard.number(consideration.input);
        const double value = std::isfinite(rawValue) ? rawValue : consideration.minimum;
        const float normalized = std::clamp(static_cast<float>((value - consideration.minimum) / (consideration.maximum - consideration.minimum)), 0.0f, 1.0f);
        const double curved = std::clamp(consideration.curve.eval(normalized), 0.0f, 1.0f);
        if (traceOption) traceOption->considerations.push_back({consideration.input, value, curved});
        sum += curved;
        if (node.combine == CombineMode::Product) result *= curved;
        else if (node.combine == CombineMode::Min) result = std::min(result, curved);
        else if (node.combine == CombineMode::Max) result = std::max(result, curved);
    }
    const double count = static_cast<double>(option.considerations.size());
    if (count == 0) result = 1.0;
    else if (node.combine == CombineMode::Product && count > 1) result += (1.0 - result) * (1.0 - 1.0 / count) * result;
    else if (node.combine == CombineMode::Average) result = sum / count;
    else if (node.combine == CombineMode::Sum) result = sum;
    return result * option.weight;
}

void Exec::scoreOptions(const Node& node, int current, std::vector<TraceOption>* records) {
    const size_t optionCount = node.options.size();
    runtime.scores.resize(optionCount);
    runtime.available.resize(optionCount);
    for (size_t index = 0; index < optionCount; ++index) {
        bool available = true;
        double value = score(node, node.options[index], available, records ? &(*records)[index] : nullptr);
        if (static_cast<int>(index) == current && available) value += node.stickiness;
        runtime.scores[index] = value;
        runtime.available[index] = available ? 1 : 0;
        if (records) (*records)[index].score = value;
    }
}

int Exec::pickWeighted(const Node& node, int best, std::string* reason) {
    const std::vector<double>& scores = runtime.scores;
    double total = 0.0;
    runtime.weights.assign(scores.size(), 0.0);
    std::vector<double>& weights = runtime.weights;
    for (size_t index = 0; index < scores.size(); ++index) {
        if (runtime.available[index] && scores[index] >= node.minScore && scores[index] > 0.0) {
            weights[index] = std::pow(scores[index], node.power);
            total += weights[index];
        }
    }
    if (total <= 0.0) {
        if (reason) *reason = "highest score " + formatNumber(scores[static_cast<size_t>(best)]);
        return best;
    }
    const double target = rng.uniform() * total;
    double accumulated = 0.0;
    int pick = best;
    for (size_t index = 0; index < weights.size(); ++index) {
        if (weights[index] <= 0.0) continue;
        accumulated += weights[index];
        pick = static_cast<int>(index);
        if (target < accumulated) break;
    }
    if (reason) *reason = "weighted pick " + formatNumber(weights[static_cast<size_t>(pick)] / total) + " of probability mass";
    return pick;
}

std::string Exec::describeBest(const Node& node, int best, int current) const {
    const std::vector<double>& scores = runtime.scores;
    int second = -1;
    for (size_t index = 0; index < scores.size(); ++index) {
        if (static_cast<int>(index) != best && runtime.available[index] && (second < 0 || scores[index] > scores[static_cast<size_t>(second)])) second = static_cast<int>(index);
    }
    std::string reason = "highest score " + formatNumber(scores[static_cast<size_t>(best)]);
    if (second >= 0) reason += " over '" + node.options[static_cast<size_t>(second)].name + "' " + formatNumber(scores[static_cast<size_t>(second)]);
    if (best == current && node.stickiness > 0.0f) reason += " (stickiness " + formatNumber(node.stickiness) + ")";
    return reason;
}

Status Exec::utility(int id) {
    const Node& node = graph.nodes[static_cast<size_t>(id)];
    int& current = runtime.option[static_cast<size_t>(id)];
    if (current >= 0 && !node.reconsider && runtime.running[static_cast<size_t>(node.options[static_cast<size_t>(current)].child)]) {
        const Option& kept = node.options[static_cast<size_t>(current)];
        emit(node, "utility", kept.name, "kept running (no reconsider)");
        return run(kept.child);
    }
    const bool tracing = trace.enabled();
    std::vector<TraceOption> records;
    if (tracing) records.resize(node.options.size());
    scoreOptions(node, current, tracing ? &records : nullptr);
    const std::vector<double>& scores = runtime.scores;
    int best = -1;
    for (size_t index = 0; index < scores.size(); ++index) {
        if (runtime.available[index] && (best < 0 || scores[index] > scores[static_cast<size_t>(best)])) best = static_cast<int>(index);
    }
    if (best < 0 || scores[static_cast<size_t>(best)] < node.minScore) {
        if (tracing) {
            const std::string reason = best < 0 ? "no available option" : "best score " + formatNumber(scores[static_cast<size_t>(best)]) + " below minScore " + formatNumber(node.minScore);
            emit(node, "utility", "", reason, std::move(records));
        }
        if (current >= 0) resetSubtree(node.options[static_cast<size_t>(current)].child);
        current = -1;
        return Status::Failure;
    }
    int pick = best;
    std::string reason;
    if (node.weighted) pick = pickWeighted(node, best, tracing ? &reason : nullptr);
    else if (tracing) reason = describeBest(node, best, current);
    emit(node, "utility", node.options[static_cast<size_t>(pick)].name, reason, std::move(records));
    if (current >= 0 && current != pick) resetSubtree(node.options[static_cast<size_t>(current)].child);
    current = pick;
    const Status status = run(node.options[static_cast<size_t>(pick)].child);
    if (status != Status::Running) current = -1;
    return status;
}

}
