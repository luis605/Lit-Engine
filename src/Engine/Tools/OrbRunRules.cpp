module;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

module Engine.OrbRunRules;

import Engine.glm;

namespace {

constexpr float kSecondsPerMinute = 60.0f;
constexpr size_t kClockBufferSize = 16;
constexpr float kMinDirectionLength = 1.0e-4f;

bool isPlaying(const OrbRunState& state) { return state.phase == OrbRunPhase::Playing; }

}

OrbRunState orbRunStart(const OrbRunConfig& config) {
    OrbRunState state;
    state.lives = config.startLives;
    state.timeLeft = config.timeLimit;
    state.taken.assign(static_cast<size_t>(config.orbTotal), false);
    return state;
}

void orbRunAdvance(OrbRunState& state, float deltaTime) {
    if (!isPlaying(state)) return;
    state.elapsed += deltaTime;
    state.invulnerable = std::max(0.0f, state.invulnerable - deltaTime);
    state.timeLeft = std::max(0.0f, state.timeLeft - deltaTime);
    if (state.timeLeft > 0.0f) return;
    state.phase = OrbRunPhase::Lost;
    state.loss = OrbRunLoss::Timeout;
}

bool orbRunCollect(OrbRunState& state, const OrbRunConfig& config, int orb) {
    if (!isPlaying(state) || orb < 0 || orb >= static_cast<int>(state.taken.size()) || state.taken[static_cast<size_t>(orb)]) return false;
    state.taken[static_cast<size_t>(orb)] = true;
    ++state.collected;
    if (state.collected >= config.orbTotal) state.phase = OrbRunPhase::Won;
    return true;
}

bool orbRunHit(OrbRunState& state, const OrbRunConfig& config) {
    if (!isPlaying(state) || state.invulnerable > 0.0f) return false;
    --state.lives;
    state.invulnerable = config.invulnerableSeconds;
    if (state.lives > 0) return true;
    state.phase = OrbRunPhase::Lost;
    state.loss = OrbRunLoss::NoLives;
    return true;
}

int orbRunPickupIndex(const OrbRunState& state, const OrbRunConfig& config, const std::vector<glm::vec3>& orbs, const glm::vec3& playerCenter) {
    for (size_t index = 0; index < orbs.size() && index < state.taken.size(); ++index) {
        if (!state.taken[index] && glm::distance(orbs[index], playerCenter) <= config.pickupRadius) return static_cast<int>(index);
    }
    return -1;
}

bool orbRunTouches(const OrbRunConfig& config, const glm::vec3& playerCenter, const glm::vec3& spider) {
    return glm::distance(playerCenter, spider) <= config.spiderRadius;
}

bool orbRunSeenBy(const OrbRunConfig& config, const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& point) {
    const glm::vec3 offset = point - eye;
    const float distance = glm::length(offset);
    if (distance < kMinDirectionLength) return true;
    return glm::dot(offset / distance, glm::normalize(forward)) >= std::cos(glm::radians(config.freezeConeDegrees));
}

std::string orbRunClockText(float seconds) {
    const int whole = static_cast<int>(std::ceil(std::max(0.0f, seconds)));
    char buffer[kClockBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", whole / static_cast<int>(kSecondsPerMinute), whole % static_cast<int>(kSecondsPerMinute));
    return buffer;
}
