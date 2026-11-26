module;

#include <string>
#include <vector>

export module Engine.OrbRunRules;

import Engine.glm;

export struct OrbRunConfig {
    int orbTotal = 8;
    int startLives = 3;
    float timeLimit = 180.0f;
    float pickupRadius = 1.1f;
    float spiderRadius = 1.1f;
    float invulnerableSeconds = 2.0f;
    float freezeConeDegrees = 38.0f;
};

export enum class OrbRunPhase { Playing, Won, Lost };
export enum class OrbRunLoss { None, Timeout, NoLives };

export struct OrbRunState {
    OrbRunPhase phase = OrbRunPhase::Playing;
    OrbRunLoss loss = OrbRunLoss::None;
    int lives = 0;
    int collected = 0;
    float timeLeft = 0.0f;
    float elapsed = 0.0f;
    float invulnerable = 0.0f;
    std::vector<bool> taken;
};

export [[nodiscard]] OrbRunState orbRunStart(const OrbRunConfig& config);
export void orbRunAdvance(OrbRunState& state, float deltaTime);
export bool orbRunCollect(OrbRunState& state, const OrbRunConfig& config, int orb);
export bool orbRunHit(OrbRunState& state, const OrbRunConfig& config);
export [[nodiscard]] int orbRunPickupIndex(const OrbRunState& state, const OrbRunConfig& config, const std::vector<glm::vec3>& orbs, const glm::vec3& playerCenter);
export [[nodiscard]] bool orbRunTouches(const OrbRunConfig& config, const glm::vec3& playerCenter, const glm::vec3& spider);
export [[nodiscard]] bool orbRunSeenBy(const OrbRunConfig& config, const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& point);
export [[nodiscard]] std::string orbRunClockText(float seconds);
