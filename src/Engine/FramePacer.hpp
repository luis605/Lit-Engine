#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace Lit {

// Picks a frame interval the machine can hold with headroom to spare. A renderer that runs flat out leaves the compositor
// no GPU time and heats the chip into throttling, which shows up as bursts of long frames.
class FramePacer {
public:
    static constexpr double kHeadroom = 1.15;
    static constexpr double kSlowestFps = 30.0;
    static constexpr size_t kWindow = 120;
    static constexpr size_t kUpdateEvery = 30;
    static constexpr size_t kWarmup = 30;
    static constexpr double kPercentile = 0.9;
    static constexpr double kRecoveryStep = 0.97;
    static constexpr double kRiseThreshold = 1.03;

    void setMaxFps(double fps) {
        m_shortest = fps > 0.0 ? 1000.0 / fps : 0.0;
        m_interval = m_shortest;
        m_count = 0;
        m_warmup = 0;
    }
    [[nodiscard]] bool enabled() const { return m_shortest > 0.0; }
    [[nodiscard]] double intervalMs() const { return m_interval; }

    double addFrame(double workMs) {
        if (!enabled() || !(workMs >= 0.0)) return m_interval;
        if (m_warmup < kWarmup) {
            ++m_warmup;
            return m_interval;
        }
        m_work[m_count % kWindow] = workMs;
        if (++m_count % kUpdateEvery != 0) return m_interval;
        std::array<double, kWindow> ordered = m_work;
        const size_t size = std::min(m_count, kWindow);
        const auto at = ordered.begin() + static_cast<std::ptrdiff_t>(static_cast<double>(size - 1) * kPercentile);
        std::nth_element(ordered.begin(), at, ordered.begin() + static_cast<std::ptrdiff_t>(size));
        const double longest = std::max(1000.0 / kSlowestFps, m_shortest);
        const double wanted = std::clamp(*at * kHeadroom, m_shortest, longest);
        if (wanted > m_interval * kRiseThreshold) m_interval = wanted;
        else if (wanted < m_interval) m_interval = std::max(wanted, m_interval * kRecoveryStep);
        return m_interval;
    }

private:
    std::array<double, kWindow> m_work{};
    size_t m_count = 0;
    size_t m_warmup = 0;
    double m_shortest = 0.0;
    double m_interval = 0.0;
};

}
