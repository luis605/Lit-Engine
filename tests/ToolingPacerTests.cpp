#include "TestSupport.hpp"
#include "ToolingSuites.hpp"
#include "Engine/FramePacer.hpp"

#include <cmath>

#define CHECK(expression) testsupport::recordCheck((expression), #expression, __LINE__)

namespace {

double settle(Lit::FramePacer& pacer, double workMs, int frames) {
    double interval = pacer.intervalMs();
    for (int i = 0; i < frames; ++i) interval = pacer.addFrame(workMs);
    return interval;
}

}

void runPacerTests() {
    Lit::FramePacer off;
    CHECK(!off.enabled());
    CHECK(settle(off, 20.0, 300) == 0.0);

    Lit::FramePacer pacer;
    pacer.setMaxFps(60.0);
    CHECK(std::abs(pacer.intervalMs() - 1000.0 / 60.0) < 1e-9);
    CHECK(std::abs(settle(pacer, 5.0, 600) - 1000.0 / 60.0) < 1e-9);
    const double loaded = settle(pacer, 18.0, 600);
    CHECK(std::abs(loaded - 18.0 * Lit::FramePacer::kHeadroom) < 1e-9);
    CHECK(settle(pacer, 18.1, 600) == loaded);
    CHECK(std::abs(settle(pacer, 500.0, 600) - 1000.0 / Lit::FramePacer::kSlowestFps) < 1e-9);
    const double afterOneUpdate = settle(pacer, 5.0, static_cast<int>(Lit::FramePacer::kWindow + Lit::FramePacer::kUpdateEvery));
    CHECK(afterOneUpdate > 1000.0 / 60.0);
    CHECK(std::abs(settle(pacer, 5.0, 3000) - 1000.0 / 60.0) < 1e-9);

    Lit::FramePacer spiky;
    spiky.setMaxFps(60.0);
    for (int i = 0; i < 600; ++i) spiky.addFrame(i % 40 == 0 ? 90.0 : 5.0);
    CHECK(std::abs(spiky.intervalMs() - 1000.0 / 60.0) < 1e-9);
}
