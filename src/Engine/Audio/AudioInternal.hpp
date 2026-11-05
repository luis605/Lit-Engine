#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace AudioInternal {
inline constexpr float kPi = 3.14159265f;
inline constexpr uint32_t kRing = 4096;
inline constexpr uint32_t kRingMask = kRing - 1;
inline constexpr float kFullBandwidthHz = 20000.0f;
inline constexpr float kBackLowpassHz = 6000.0f;
inline constexpr float kShadowLowpassHz = 3000.0f;
inline constexpr float kMaxInterauralDelaySeconds = 0.00066f;
inline constexpr size_t kMaxReflectionTaps = 4;

inline float onePole(float cutoffHz, uint32_t rate) {
    const float cutoff = std::clamp(cutoffHz, 20.0f, static_cast<float>(rate) * 0.45f);
    return 1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(rate));
}
}
