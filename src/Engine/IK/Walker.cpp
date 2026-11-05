module;

#include <algorithm>
#include <cmath>
#include <cstddef>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;

namespace {

constexpr float kEpsilon = 1.0e-5f;
constexpr float kDefaultStrideFraction = 0.2f;

bool bindLeg(World& world, WalkerLeg& leg, const glm::mat4& inverseBody) {
    IkChain* chain = world.get<IkChain>(leg.chain);
    if (!chain || !bindIkChain(world, *chain)) return false;
    float reach = 0.0f;
    for (float length : chain->lengths) reach += length;
    if (reach < kEpsilon) return false;
    const glm::vec3 tip = world.getWorldPosition(chain->joints.back());
    leg.localRest = glm::vec3(inverseBody * glm::vec4(tip, 1.0f));
    leg.hipLocal = glm::vec3(inverseBody * glm::vec4(world.getWorldPosition(chain->joints.front()), 1.0f));
    leg.reach = reach;
    leg.placed = false;
    leg.swinging = false;
    chain->target = leg.foot;
    return true;
}

float meanReach(const Walker& walker) {
    float total = 0.0f;
    for (const WalkerLeg& leg : walker.legs) total += leg.reach;
    return walker.legs.empty() ? 0.0f : total / static_cast<float>(walker.legs.size());
}

float meanDrop(const Walker& walker) {
    float total = 0.0f;
    for (const WalkerLeg& leg : walker.legs) total -= leg.localRest.y;
    return walker.legs.empty() ? 0.0f : total / static_cast<float>(walker.legs.size());
}

glm::vec3 flatForward(World& world, EntityHandle body) {
    const glm::vec3 forward = world.forward(body);
    const glm::vec3 flat(forward.x, 0.0f, forward.z);
    return glm::length(flat) < kEpsilon ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::normalize(flat);
}

}

bool bindWalker(World& world, Walker& walker) {
    if (!world.isAlive(walker.body) || walker.legs.empty() || walker.legs.size() > kMaxWalkerLegs) return false;
    const glm::mat4 inverseBody = glm::inverse(world.getWorldMatrix(walker.body));
    for (WalkerLeg& leg : walker.legs) {
        if (!world.isAlive(leg.foot) || !bindLeg(world, leg, inverseBody)) return false;
    }
    if (walker.bodyHeight <= 0.0f) walker.bodyHeight = meanDrop(walker);
    for (WalkerLeg& leg : walker.legs) leg.localRest.y = 0.0f;
    if (walker.stride <= 0.0f) walker.stride = meanReach(walker) * kDefaultStrideFraction;
    walker.heading = flatForward(world, walker.body);
    walker.smoothedUp = world.up(walker.body);
    walker.velocity = glm::vec3(0.0f);
    walker.settled = false;
    assignWalkerGait(walker);
    walker.bound = true;
    return true;
}

void updateWalkers(World& world, float deltaTime) {
    if (!(deltaTime > 0.0f) || !std::isfinite(deltaTime)) return;
    world.view<Walker>([&](EntityHandle, Walker& walker) {
        if (!walker.enabled || !world.isAlive(walker.body)) return;
        if (!walker.bound && !bindWalker(world, walker)) return;
        updateWalkerBody(world, walker, deltaTime);
        updateWalkerLegs(world, walker, deltaTime);
    });
}

void setWalkerTarget(World& world, EntityHandle body, const glm::vec3& target) {
    Walker* walker = world.get<Walker>(body);
    if (!walker) return;
    if (!std::isfinite(target.x + target.y + target.z)) return;
    walker->moveTarget = target;
    walker->hasMoveTarget = true;
}

void setWalkerInput(World& world, EntityHandle body, const glm::vec2& input) {
    Walker* walker = world.get<Walker>(body);
    if (walker && std::isfinite(input.x + input.y)) walker->moveInput = input;
}
