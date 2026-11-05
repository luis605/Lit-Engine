module;

#include <algorithm>
#include <cmath>
#include <cstdint>

export module Engine.Render.governor;

import Engine.Render.settings;

export inline constexpr int kGovernorMaxLevel = 8;
export inline constexpr float kGovernorSmoothing = 0.12f;
export inline constexpr float kGovernorOverBudget = 1.06f;
export inline constexpr float kGovernorUnderBudget = 0.72f;
export inline constexpr float kGovernorDownHoldSeconds = 1.5f;
export inline constexpr float kGovernorUpHoldSeconds = 6.0f;
export inline constexpr float kGovernorCooldownSeconds = 2.5f;
export inline constexpr int kGovernorMaxBackoff = 4;
export inline constexpr float kGovernorMaxSampleMs = 250.0f;
export inline constexpr float kGovernorScaleStepPerLevel = 0.05f;
export inline constexpr int kGovernorShadowLevelStart = 4;
export inline constexpr int kGovernorReflectionLevelStart = 3;
export inline constexpr int kGovernorShadowMinResolution = 512;

export struct GovernorLimits {
    float renderScale = 1.0f;
    int shadowResolution = 2048;
    int reflectionQuality = 2;
    bool operator==(const GovernorLimits&) const = default;
};

export struct QualityGovernor {
    int level = 0;
    float smoothedMs = 0.0f;
    float overSeconds = 0.0f;
    float underSeconds = 0.0f;
    float cooldown = 0.0f;
    float sinceStepUp = 1.0e9f;
    int backoff = 1;
    bool primed = false;
    bool hasBaseline = false;
    GovernorLimits baseline;
    GovernorLimits applied;
};

export inline GovernorLimits governorLimitsAt(int level, const GovernorLimits& baseline, float minScale) {
    const int clamped = std::clamp(level, 0, kGovernorMaxLevel);
    GovernorLimits limits = baseline;
    const float floorScale = std::min(std::max(minScale, kRenderScaleMin), baseline.renderScale);
    limits.renderScale = quantizeRenderScale(std::max(baseline.renderScale - kGovernorScaleStepPerLevel * static_cast<float>(clamped), floorScale));
    if (clamped >= kGovernorReflectionLevelStart) limits.reflectionQuality = std::max(0, baseline.reflectionQuality - (clamped - kGovernorReflectionLevelStart) / 2 - 1);
    if (clamped >= kGovernorShadowLevelStart) {
        int resolution = baseline.shadowResolution;
        for (int drop = 0; drop < (clamped - kGovernorShadowLevelStart) / 2 + 1 && resolution / 2 >= kGovernorShadowMinResolution; ++drop) resolution /= 2;
        limits.shadowResolution = resolution;
    }
    return limits;
}

export inline GovernorLimits currentLimits(const RenderSettings& settings) {
    return GovernorLimits{settings.renderScale, settings.shadowResolution, static_cast<int>(settings.reflectionQuality)};
}

export inline void applyLimits(RenderSettings& settings, const GovernorLimits& limits) {
    settings.renderScale = limits.renderScale;
    settings.shadowResolution = limits.shadowResolution;
    settings.reflectionQuality = static_cast<ReflectionQuality>(limits.reflectionQuality);
}

export inline void resetGovernor(QualityGovernor& governor) { governor = QualityGovernor{}; }

inline void stepGovernorLevel(QualityGovernor& governor, int direction) {
    governor.level = std::clamp(governor.level + direction, 0, kGovernorMaxLevel);
    governor.cooldown = kGovernorCooldownSeconds;
    governor.overSeconds = 0.0f;
    governor.underSeconds = 0.0f;
    if (direction > 0) {
        if (governor.sinceStepUp < kGovernorUpHoldSeconds * 2.0f) governor.backoff = std::min(governor.backoff * 2, kGovernorMaxBackoff);
    } else {
        governor.sinceStepUp = 0.0f;
    }
}

inline void trackBaseline(QualityGovernor& governor, const RenderSettings& settings) {
    const GovernorLimits current = currentLimits(settings);
    if (governor.hasBaseline && current == governor.applied) return;
    governor.baseline = current;
    governor.hasBaseline = true;
    governor.applied = current;
    governor.level = 0;
    governor.overSeconds = 0.0f;
    governor.underSeconds = 0.0f;
}

export inline bool updateGovernor(QualityGovernor& governor, RenderSettings& settings, float frameMs, float deltaSeconds) {
    if (!settings.qualityGovernor) {
        if (governor.hasBaseline) {
            applyLimits(settings, governor.baseline);
            resetGovernor(governor);
            return true;
        }
        return false;
    }
    trackBaseline(governor, settings);
    const float sample = std::clamp(frameMs, 0.0f, kGovernorMaxSampleMs);
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.5f);
    governor.smoothedMs = governor.primed ? governor.smoothedMs + (sample - governor.smoothedMs) * kGovernorSmoothing : sample;
    governor.primed = true;
    governor.cooldown = std::max(governor.cooldown - dt, 0.0f);
    governor.sinceStepUp = std::min(governor.sinceStepUp + dt, 1.0e9f);
    const float target = settings.targetFrameMs;
    governor.overSeconds = governor.smoothedMs > target * kGovernorOverBudget ? governor.overSeconds + dt : 0.0f;
    governor.underSeconds = governor.smoothedMs < target * kGovernorUnderBudget ? governor.underSeconds + dt : 0.0f;
    int previous = governor.level;
    if (governor.cooldown <= 0.0f) {
        if (governor.overSeconds >= kGovernorDownHoldSeconds && governor.level < kGovernorMaxLevel) stepGovernorLevel(governor, 1);
        else if (governor.underSeconds >= kGovernorUpHoldSeconds * static_cast<float>(governor.backoff) && governor.level > 0) stepGovernorLevel(governor, -1);
    }
    if (governor.level == 0 && governor.underSeconds >= kGovernorUpHoldSeconds * static_cast<float>(kGovernorMaxBackoff) * 2.0f) governor.backoff = 1;
    const GovernorLimits next = governorLimitsAt(governor.level, governor.baseline, settings.governorMinScale);
    const bool changed = !(next == governor.applied) || previous != governor.level;
    governor.applied = next;
    applyLimits(settings, next);
    return changed;
}
