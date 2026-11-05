module;

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

module Engine.AI;

#include "AIInternal.hpp"

namespace {
constexpr uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;
constexpr uint64_t kCurrentStateMultiplier = 31u;
constexpr uint64_t kOptionMultiplier = 131u;
constexpr uint64_t kCounterMultiplier = 7u;
constexpr int kHashRotateLeft = 13;
constexpr int kHashRotateRight = 51;
}

BehaviorRunner::BehaviorRunner(const BehaviorGraph& graph, const ActionRegistry& actions, Pcg32& rng, uint32_t agent)
    : m_impl(std::make_unique<Impl>()), m_graph(graph), m_actions(actions), m_rng(rng), m_agent(agent) {
    reset();
}

BehaviorRunner::~BehaviorRunner() = default;

void BehaviorRunner::resizeToGraph() {
    const size_t nodeCount = m_graph.impl().nodes.size();
    m_impl->states.assign(nodeCount, AiDetail::NodeState{});
    m_impl->currentState.assign(nodeCount, -1);
    m_impl->option.assign(nodeCount, -1);
    m_impl->running.assign(nodeCount, 0);
    m_impl->actions.assign(nodeCount, nullptr);
}

void BehaviorRunner::reset() {
    resizeToGraph();
    m_tick = 0;
    m_trace.clear();
    m_blackboard.clear();
    for (const auto& [key, value] : m_graph.impl().initial.members()) m_blackboard.set(key, value);
}

Status BehaviorRunner::tick(float dt) {
    if (!m_graph.valid()) return Status::Failure;
    if (m_impl->states.size() != m_graph.impl().nodes.size()) resizeToGraph();
    AiDetail::Exec executor{m_graph.impl(), *m_impl, m_actions, m_blackboard, m_trace, m_rng, dt, m_tick, m_agent};
    const Status status = executor.run(m_graph.impl().root);
    ++m_tick;
    return status;
}

std::vector<std::string> BehaviorRunner::activePath() const {
    std::vector<std::string> out;
    const auto& nodes = m_graph.impl().nodes;
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (m_impl->running[index]) out.push_back(nodes[index].name);
    }
    return out;
}

std::string BehaviorRunner::stateOf(const std::string& fsmName) const {
    const auto& nodes = m_graph.impl().nodes;
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].kind != AiDetail::Kind::Fsm || nodes[index].name != fsmName) continue;
        const int stateIndex = m_impl->currentState[index];
        return stateIndex < 0 ? std::string() : nodes[index].states[static_cast<size_t>(stateIndex)].name;
    }
    return {};
}

uint64_t BehaviorRunner::stateHash() const {
    uint64_t hash = hashName(m_blackboard.toJson().dump());
    for (size_t index = 0; index < m_impl->states.size(); ++index) {
        const AiDetail::NodeState& state = m_impl->states[index];
        hash ^= static_cast<uint64_t>(state.index + 1) * kGoldenGamma + static_cast<uint64_t>(m_impl->currentState[index] + 2) * kCurrentStateMultiplier +
                static_cast<uint64_t>(m_impl->option[index] + 2) * kOptionMultiplier + static_cast<uint64_t>(state.counter) * kCounterMultiplier;
        hash = (hash << kHashRotateLeft) | (hash >> kHashRotateRight);
    }
    return hash;
}
