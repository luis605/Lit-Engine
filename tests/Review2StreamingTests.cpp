#include "Review2Suites.hpp"
#include "TestSupport.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

import Engine.Portal;
import Engine.Streaming;
import Engine.glm;

namespace {

constexpr int kUpdates = 400;
constexpr std::chrono::milliseconds kPause{2};

PortalSystem makeCells(int count) {
    PortalSystem portals;
    for (int index = 0; index < count; ++index) {
        PortalCell cell;
        cell.name = "S" + std::to_string(index);
        cell.min = glm::vec3(static_cast<float>(index) * 20.0f, 0.0f, 0.0f);
        cell.max = glm::vec3(static_cast<float>(index) * 20.0f + 10.0f, 4.0f, 10.0f);
        portals.addCell(cell);
    }
    return portals;
}

void testThrowingJobs() {
    PortalSystem portals = makeCells(3);
    std::atomic<int> prepared{0};
    {
        StreamingSystem streaming(portals);
        CellLoader loader;
        loader.prepare = [&](int cell) -> std::shared_ptr<void> {
            ++prepared;
            if (cell == 0) throw std::runtime_error("prepare failed");
            return std::make_shared<int>(cell);
        };
        loader.commit = [](int, void*, StreamBudget&) { return true; };
        streaming.setLoader(loader);
        AssetRequest request;
        request.key = "broken";
        request.load = []() -> std::shared_ptr<void> { throw std::runtime_error("asset failed"); };
        streaming.requestAsset(request);
        streaming.jobs().submit(0, [] { throw std::runtime_error("raw job failed"); });
        for (int update = 0; update < kUpdates && !(streaming.resident(0) && streaming.assetReady("broken")); ++update) {
            streaming.update(0);
            std::this_thread::sleep_for(kPause);
        }
        CHECK(streaming.resident(0));
        CHECK(streaming.assetReady("broken"));
        streaming.jobs().waitIdle();
    }
    CHECK(prepared > 0);
}

void testNonFiniteFrameTimes() {
    PortalSystem portals = makeCells(1);
    StreamingSystem streaming(portals);
    streaming.recordFrame(std::numeric_limits<double>::quiet_NaN());
    streaming.recordFrame(std::numeric_limits<double>::infinity());
    for (int frame = 0; frame < 100; ++frame) streaming.recordFrame(1.0 + frame);
    const StreamingStats stats = streaming.stats();
    CHECK(std::isfinite(stats.frameP50) && std::isfinite(stats.frameP99) && std::isfinite(stats.frameMax));
    CHECK(stats.frames == 100);
}

void testOutOfRangeCamera() {
    PortalSystem portals = makeCells(2);
    StreamingSystem streaming(portals);
    CellLoader loader;
    loader.prepare = [](int) -> std::shared_ptr<void> { return std::make_shared<int>(1); };
    loader.commit = [](int, void*, StreamBudget&) { return true; };
    streaming.setLoader(loader);
    streaming.update(-1);
    streaming.update(99);
    streaming.update(0);
    CHECK(streaming.state(-5) == CellState::Unloaded);
    CHECK(streaming.distance(1000) > 0);
    streaming.jobs().waitIdle();
}

void testPrimeTimeout() {
    PortalSystem portals = makeCells(3);
    StreamingConfig config;
    config.primeTimeoutMillis = 50.0;
    StreamingSystem streaming(portals, config);
    CellLoader loader;
    loader.prepare = [](int cell) -> std::shared_ptr<void> { return std::make_shared<int>(cell); };
    loader.commit = [](int, void*, StreamBudget&) { return false; };
    streaming.setLoader(loader);
    const auto start = std::chrono::steady_clock::now();
    CHECK(!streaming.prime(0));
    const double elapsedMillis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CHECK(elapsedMillis >= 40.0);
    CHECK(elapsedMillis < 5000.0);

    PortalSystem healthyPortals = makeCells(3);
    StreamingSystem healthy(healthyPortals);
    CellLoader working;
    working.prepare = loader.prepare;
    working.commit = [](int, void*, StreamBudget&) { return true; };
    healthy.setLoader(working);
    CHECK(healthy.prime(0));
    CHECK(healthy.resident(0));
}

}

void runReview2StreamingTests() {
    testPrimeTimeout();
    testThrowingJobs();
    testNonFiniteFrameTimes();
    testOutOfRangeCamera();
}
