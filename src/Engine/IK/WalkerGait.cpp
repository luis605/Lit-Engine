module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;

namespace {

constexpr float kEpsilon = 1.0e-5f;
constexpr float kLandingLead = 0.8f;
constexpr float kReachLimit = 0.92f;
constexpr float kStepThreshold = 1.0f;
constexpr float kOffGroupThreshold = 1.25f;
constexpr float kStretchFull = 0.9f;
constexpr float kStretchStop = 0.98f;
constexpr float kPi = 3.14159265358979f;
constexpr float kPoleHeight = 1.0f;
constexpr float kPoleOutward = 0.35f;

struct BodyFrame {
    glm::vec3 position{0.0f};
    glm::mat3 axes{1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

glm::vec3 unitOr(const glm::vec3& vector, const glm::vec3& fallback) {
    const float length = glm::length(vector);
    return length < kEpsilon || !std::isfinite(length) ? fallback : vector / length;
}

BodyFrame frameOf(World& world, const Walker& walker) {
    BodyFrame frame;
    const glm::mat4 matrix = world.getWorldMatrix(walker.body);
    frame.position = glm::vec3(matrix[3]);
    frame.axes = glm::mat3(unitOr(glm::vec3(matrix[0]), glm::vec3(1.0f, 0.0f, 0.0f)), unitOr(glm::vec3(matrix[1]), glm::vec3(0.0f, 1.0f, 0.0f)), unitOr(glm::vec3(matrix[2]), glm::vec3(0.0f, 0.0f, 1.0f)));
    frame.up = frame.axes[1];
    return frame;
}

glm::vec3 horizontal(const glm::vec3& vector) { return glm::vec3(vector.x, 0.0f, vector.z); }

glm::vec3 homeOf(const BodyFrame& frame, const WalkerLeg& leg) { return frame.position + frame.axes * glm::vec3(leg.localRest.x, 0.0f, leg.localRest.z); }

glm::vec3 hipOf(World& world, const IkChain& chain) { return world.getWorldPosition(chain.joints.front()); }

bool adjacent(const Walker& walker, size_t first, size_t second) {
    const WalkerLeg& a = walker.legs[first];
    const WalkerLeg& b = walker.legs[second];
    const int other = static_cast<int>(second);
    const int self = static_cast<int>(first);
    return a.sameSideNext == other || a.sameSidePrevious == other || a.mirror == other || b.sameSideNext == self || b.sameSidePrevious == self || b.mirror == self;
}

size_t swingingCount(const Walker& walker) {
    size_t count = 0;
    for (const WalkerLeg& leg : walker.legs) count += leg.swinging ? 1 : 0;
    return count;
}

bool neighbourSwinging(const Walker& walker, size_t index) {
    for (size_t other = 0; other < walker.legs.size(); ++other) {
        if (other != index && walker.legs[other].swinging && adjacent(walker, index, other)) return true;
    }
    return false;
}

glm::vec3 landingPoint(World& world, const Walker& walker, const BodyFrame& frame, const WalkerLeg& leg, const glm::vec3& hip) {
    glm::vec3 target = homeOf(frame, leg);
    if (walker.speed > kEpsilon) target += glm::normalize(horizontal(walker.velocity)) * walker.stride * kLandingLead;
    GroundHit ground = sampleWalkerGround(world, walker, target.x, target.z, frame.position.y);
    if (!ground.found) return glm::vec3(target.x, leg.planted.y, target.z);
    const float reach = leg.reach * kReachLimit;
    const float rise = ground.point.y - hip.y;
    const float allowed = std::sqrt(std::max(0.0f, reach * reach - rise * rise));
    const glm::vec3 offset = horizontal(ground.point - hip);
    if (glm::length(offset) <= allowed) return ground.point;
    const glm::vec3 pulled = hip + offset * (allowed / glm::length(offset));
    ground = sampleWalkerGround(world, walker, pulled.x, pulled.z, frame.position.y);
    return ground.found ? ground.point : glm::vec3(pulled.x, leg.planted.y, pulled.z);
}

void beginSwing(World& world, Walker& walker, const BodyFrame& frame, WalkerLeg& leg, const glm::vec3& hip) {
    leg.swingFrom = leg.planted;
    leg.swingTo = landingPoint(world, walker, frame, leg, hip);
    const float distance = glm::length(leg.swingTo - leg.swingFrom);
    const float footSpeed = std::max(walker.speed, walker.maxSpeed * 0.5f) * walker.swingSpeed;
    leg.swingDuration = std::clamp(distance / std::max(footSpeed, kEpsilon), walker.minSwingTime, walker.maxSwingTime);
    leg.swingTime = 0.0f;
    leg.swinging = true;
}

bool advanceSwing(Walker& walker, WalkerLeg& leg, float deltaTime) {
    leg.swingTime += deltaTime;
    const float t = std::min(1.0f, leg.swingTime / leg.swingDuration);
    const float eased = t * t * (3.0f - 2.0f * t);
    leg.position = leg.swingFrom + (leg.swingTo - leg.swingFrom) * eased + glm::vec3(0.0f, walker.stepHeight * std::sin(kPi * t), 0.0f);
    if (t < 1.0f) return false;
    leg.planted = leg.swingTo;
    leg.position = leg.swingTo;
    leg.swinging = false;
    return true;
}

void placeFeet(World& world, Walker& walker, const BodyFrame& frame) {
    for (WalkerLeg& leg : walker.legs) {
        if (leg.placed) continue;
        const glm::vec3 home = homeOf(frame, leg);
        const GroundHit ground = sampleWalkerGround(world, walker, home.x, home.z, frame.position.y);
        leg.planted = ground.found ? ground.point : glm::vec3(home.x, frame.position.y - walker.bodyHeight, home.z);
        leg.position = leg.planted;
        leg.placed = true;
    }
}

float urgency(const Walker& walker, const BodyFrame& frame, const WalkerLeg& leg) {
    const glm::vec3 home = homeOf(frame, leg);
    return glm::length(horizontal(leg.planted - home)) / std::max(walker.stride, kEpsilon);
}

void beginSteps(World& world, Walker& walker, const BodyFrame& frame, bool landed) {
    std::array<size_t, kMaxWalkerLegs> order{};
    std::array<float, kMaxWalkerLegs> score{};
    size_t candidates = 0;
    for (size_t index = 0; index < walker.legs.size(); ++index) {
        const WalkerLeg& leg = walker.legs[index];
        if (leg.swinging) continue;
        const float need = urgency(walker, frame, leg);
        const float threshold = leg.group == walker.activeGroup ? kStepThreshold : kOffGroupThreshold;
        if (need < threshold) continue;
        size_t place = candidates++;
        while (place > 0 && score[place - 1] < need) {
            order[place] = order[place - 1];
            score[place] = score[place - 1];
            --place;
        }
        order[place] = index;
        score[place] = need;
    }
    const size_t limit = walker.maxSimultaneous > 0 ? static_cast<size_t>(walker.maxSimultaneous) : std::max<size_t>(1, walker.legs.size() / 2);
    for (size_t rank = 0; rank < candidates; ++rank) {
        const size_t index = order[rank];
        if (swingingCount(walker) >= limit || neighbourSwinging(walker, index)) continue;
        WalkerLeg& leg = walker.legs[index];
        const IkChain* chain = world.get<IkChain>(leg.chain);
        if (chain) beginSwing(world, walker, frame, leg, hipOf(world, *chain));
    }
    if (landed) {
        bool activeBusy = false;
        for (const WalkerLeg& leg : walker.legs) activeBusy = activeBusy || (leg.swinging && leg.group == walker.activeGroup);
        if (!activeBusy) walker.activeGroup = 1 - walker.activeGroup;
    }
}

void publishLeg(World& world, Walker& walker, const BodyFrame& frame, WalkerLeg& leg, float& stretch) {
    world.setWorldPosition(leg.foot, leg.position);
    IkChain* chain = world.get<IkChain>(leg.chain);
    if (!chain) return;
    const glm::vec3 hip = hipOf(world, *chain);
    const glm::vec3 outward = horizontal(hip - frame.position);
    const glm::vec3 outwardUnit = glm::length(outward) < kEpsilon ? glm::vec3(0.0f) : glm::normalize(outward);
    chain->polePoint = hip + frame.up * (leg.reach * kPoleHeight) + outwardUnit * (leg.reach * kPoleOutward);
    chain->usePolePoint = true;
    if (!leg.swinging) stretch = std::max(stretch, glm::length(leg.planted - hip) / leg.reach);
}

}

void updateWalkerLegs(World& world, Walker& walker, float deltaTime) {
    const BodyFrame frame = frameOf(world, walker);
    placeFeet(world, walker, frame);
    bool landed = false;
    for (WalkerLeg& leg : walker.legs) {
        if (leg.swinging) landed = advanceSwing(walker, leg, deltaTime) || landed;
    }
    beginSteps(world, walker, frame, landed);
    float stretch = 0.0f;
    for (WalkerLeg& leg : walker.legs) publishLeg(world, walker, frame, leg, stretch);
    walker.speedScale = std::clamp((kStretchStop - stretch) / (kStretchStop - kStretchFull), 0.0f, 1.0f);
}

void assignWalkerGait(Walker& walker) {
    std::array<std::array<int, kMaxWalkerLegs>, 2> sides{};
    std::array<size_t, 2> counts{0, 0};
    for (size_t index = 0; index < walker.legs.size(); ++index) {
        const size_t side = walker.legs[index].localRest.x >= 0.0f ? 0 : 1;
        size_t place = counts[side]++;
        const auto forwardOf = [&](int leg) { return -walker.legs[static_cast<size_t>(leg)].localRest.z; };
        while (place > 0 && forwardOf(sides[side][place - 1]) < forwardOf(static_cast<int>(index))) {
            sides[side][place] = sides[side][place - 1];
            --place;
        }
        sides[side][place] = static_cast<int>(index);
    }
    for (size_t side = 0; side < 2; ++side) {
        for (size_t rank = 0; rank < counts[side]; ++rank) {
            WalkerLeg& leg = walker.legs[static_cast<size_t>(sides[side][rank])];
            leg.group = static_cast<int>((rank + side) % 2);
            leg.sameSidePrevious = rank > 0 ? sides[side][rank - 1] : -1;
            leg.sameSideNext = rank + 1 < counts[side] ? sides[side][rank + 1] : -1;
            leg.mirror = -1;
            float best = 1.0e30f;
            for (size_t other = 0; other < counts[1 - side]; ++other) {
                const WalkerLeg& candidate = walker.legs[static_cast<size_t>(sides[1 - side][other])];
                const float gap = std::abs(candidate.localRest.z - leg.localRest.z);
                if (gap < best) {
                    best = gap;
                    leg.mirror = sides[1 - side][other];
                }
            }
        }
    }
}
