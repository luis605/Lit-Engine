module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;
import Engine.History;

namespace {

constexpr float kDirtyEpsilon = 1.0e-5f;
constexpr float kDirectionEpsilon = 1.0e-6f;

size_t slot(size_t index) { return index; }

bool jointsAlive(World& world, const IkChain& chain) {
    if (chain.joints.size() < 2 || chain.joints.size() > static_cast<size_t>(kMaxIkJoints)) return false;
    for (EntityHandle joint : chain.joints) {
        if (!world.isAlive(joint)) return false;
    }
    return true;
}

bool finiteVector(const glm::vec3& vector) { return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z); }

std::optional<glm::vec3> resolveTarget(World& world, const IkChain& chain) {
    glm::vec3 point(0.0f);
    if (world.isAlive(chain.target)) point = world.getWorldPosition(chain.target);
    else if (chain.useTargetPoint) point = chain.targetPoint;
    else return std::nullopt;
    if (!finiteVector(point)) return std::nullopt;
    return point;
}

std::optional<glm::vec3> resolvePole(World& world, const IkChain& chain) {
    glm::vec3 point(0.0f);
    if (world.isAlive(chain.pole)) point = world.getWorldPosition(chain.pole);
    else if (chain.usePolePoint) point = chain.polePoint;
    else return std::nullopt;
    if (!finiteVector(point)) return std::nullopt;
    return point;
}

bool nearlySame(const glm::vec3& first, const glm::vec3& second) { return glm::length(first - second) < kDirtyEpsilon; }

bool unchanged(const IkChain& chain, const glm::vec3& target, const glm::vec3& pole, const glm::vec3& root, const glm::vec3& tip) {
    return chain.solved && nearlySame(chain.lastTarget, target) && nearlySame(chain.lastPole, pole) && nearlySame(chain.lastRoot, root) && nearlySame(chain.lastTip, tip) && std::abs(chain.lastWeight - chain.weight) < kDirtyEpsilon;
}

bool hasConstraint(const IkJointLimit& limit) { return limit.hinge || limit.cone < kIkPi; }

glm::vec3 rootBaseDirection(World& world, const IkChain& chain) {
    if (chain.limits.empty() || !hasConstraint(chain.limits.front())) return glm::vec3(0.0f);
    const EntityHandle parent = world.getParent(chain.joints.front());
    if (!world.isAlive(parent)) return glm::vec3(0.0f);
    const glm::vec3 offset = world.getWorldPosition(chain.joints.front()) - world.getWorldPosition(parent);
    return glm::length(offset) < kDirectionEpsilon ? glm::vec3(0.0f) : glm::normalize(offset);
}

void fillState(World& world, const IkChain& chain, IkChainState& state) {
    state.count = static_cast<int>(chain.joints.size());
    for (size_t index = 0; index < chain.joints.size(); ++index) {
        state.points[slot(index)] = world.getWorldPosition(chain.joints[index]);
        state.lengths[slot(index)] = index < chain.lengths.size() ? chain.lengths[index] : 0.0f;
        state.limits[slot(index)] = index < chain.limits.size() ? chain.limits[index] : IkJointLimit{};
    }
}

IkResult runSolver(IkChainState& state, const IkChain& chain, const glm::vec3& target, const IkSettings& settings) {
    switch (chain.algorithm) {
    case IkAlgorithm::TwoBone: return solveTwoBoneAnalytic(state, target, settings);
    case IkAlgorithm::Fabrik: return solveFabrik(state, target, settings);
    case IkAlgorithm::Ccd: return solveCcd(state, target, settings);
    default: return solveIk(state, target, settings);
    }
}

glm::vec3 blendDirection(const glm::vec3& current, const glm::vec3& desired, float weight) {
    if (weight >= 1.0f) return desired;
    const glm::vec3 mixed = current + (desired - current) * weight;
    return glm::length(mixed) < kDirectionEpsilon ? desired : glm::normalize(mixed);
}

void applySolved(World& world, const IkChain& chain, const IkChainState& state) {
    for (size_t bone = 0; bone + 1 < chain.joints.size(); ++bone) {
        const glm::vec3 from = world.getWorldPosition(chain.joints[bone]);
        const glm::vec3 current = world.getWorldPosition(chain.joints[bone + 1]) - from;
        const glm::vec3 desired = state.points[slot(bone + 1)] - state.points[slot(bone)];
        if (glm::length(current) < kDirectionEpsilon || glm::length(desired) < kDirectionEpsilon) continue;
        const glm::vec3 currentUnit = glm::normalize(current);
        const glm::vec3 blended = blendDirection(currentUnit, glm::normalize(desired), chain.weight);
        world.setWorldRotation(chain.joints[bone], ikRotationBetween(currentUnit, blended) * world.getWorldRotation(chain.joints[bone]));
        if (world.getParent(chain.joints[bone + 1]) != chain.joints[bone]) world.setWorldPosition(chain.joints[bone + 1], from + blended * state.lengths[slot(bone)]);
    }
}

bool solveChain(World& world, IkChain& chain, IkChainState& state) {
    if (!chain.enabled || chain.weight <= 0.0f || !jointsAlive(world, chain)) return false;
    if (!chain.bound && !bindIkChain(world, chain)) return false;
    const std::optional<glm::vec3> target = resolveTarget(world, chain);
    if (!target) return false;
    const std::optional<glm::vec3> pole = resolvePole(world, chain);
    const glm::vec3 root = world.getWorldPosition(chain.joints.front());
    const glm::vec3 tip = world.getWorldPosition(chain.joints.back());
    const glm::vec3 poleValue = pole.value_or(glm::vec3(0.0f));
    if (unchanged(chain, *target, poleValue, root, tip)) return false;
    fillState(world, chain, state);
    IkSettings settings;
    settings.iterations = chain.iterations;
    settings.tolerance = chain.tolerance;
    settings.hasPole = pole.has_value();
    settings.pole = poleValue;
    settings.baseDirection = rootBaseDirection(world, chain);
    const IkResult result = runSolver(state, chain, *target, settings);
    applySolved(world, chain, state);
    chain.lastTarget = *target;
    chain.lastPole = poleValue;
    chain.lastRoot = root;
    chain.lastTip = world.getWorldPosition(chain.joints.back());
    chain.lastWeight = chain.weight;
    chain.lastError = result.error;
    chain.lastReached = result.reached;
    chain.solved = true;
    return true;
}

}

bool bindIkChain(World& world, IkChain& chain) {
    if (!jointsAlive(world, chain)) return false;
    chain.lengths.assign(chain.joints.size(), 0.0f);
    for (size_t index = 0; index + 1 < chain.joints.size(); ++index) {
        chain.lengths[index] = glm::length(world.getWorldPosition(chain.joints[index + 1]) - world.getWorldPosition(chain.joints[index]));
    }
    if (chain.limits.size() < chain.joints.size()) chain.limits.resize(chain.joints.size());
    chain.bound = true;
    chain.solved = false;
    return true;
}

IkFrameStats updateIk(World& world, float) {
    IkFrameStats stats;
    IkChainState state;
    world.view<IkChain>([&](EntityHandle, IkChain& chain) {
        if (solveChain(world, chain, state)) ++stats.solved;
        else ++stats.skipped;
    });
    return stats;
}

EntityHandle createIkChain(World& world, std::span<const EntityHandle> joints, EntityHandle target) {
    if (joints.size() < 2 || joints.size() > static_cast<size_t>(kMaxIkJoints)) return NULL_ENTITY;
    IkChain* chain = world.add<IkChain>(joints.front());
    if (!chain) return NULL_ENTITY;
    chain->joints.assign(joints.begin(), joints.end());
    chain->target = target;
    bindIkChain(world, *chain);
    return joints.front();
}

EntityHandle spawnIkChain(History& history, std::span<const EntityHandle> joints, const std::string& targetName, const glm::vec3& targetPosition) {
    if (joints.size() < 2 || joints.size() > static_cast<size_t>(kMaxIkJoints)) return NULL_ENTITY;
    World& world = history.world();
    const std::vector<EntityHandle> chainJoints(joints.begin(), joints.end());
    history.beginGroup("Create IK Chain");
    const std::vector<EntityHandle> created = history.spawnRebuildable("Create IK Target", [&world, targetName, targetPosition] {
        EntityDesc marker;
        marker.name = targetName;
        marker.position = targetPosition;
        return std::vector<EntityHandle>{world.create(marker)};
    });
    const EntityHandle target = created.front();
    History* owner = &history;
    auto attach = [owner, chainJoints, target] {
        std::vector<EntityHandle> resolved;
        resolved.reserve(chainJoints.size());
        for (const EntityHandle joint : chainJoints) resolved.push_back(owner->resolve(joint));
        createIkChain(owner->world(), resolved, owner->resolve(target));
    };
    auto detach = [owner, root = chainJoints.front()] { owner->world().remove<IkChain>(owner->resolve(root)); };
    attach();
    history.record("Attach IK Chain", attach, detach);
    history.endGroup();
    return history.resolve(target);
}
