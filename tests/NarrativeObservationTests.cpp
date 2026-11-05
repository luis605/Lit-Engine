#include "TestSupport.hpp"
#include "NarrativeFixtures.hpp"
#include "NarrativeSuites.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.camera;
import Engine.Events;
import Engine.Rng;
import Engine.Json;
import Engine.Params;
import Engine.State;
import Engine.Storylet;
import Engine.Observation;
import Engine.Save;
import Engine.CameraRig;

namespace {

struct ViewFixture {
    World world;
    EntityHandle a, b, d;
    std::vector<ObservationChanged> events;
    std::unique_ptr<ObservationSystem> obs;
    ViewFixture() {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        world.camera().setPos({0.0f, 0.0f, 0.0f});
        a = world.create("a", 1, {0.0f, 0.0f, -10.0f});
        b = world.create("b", 1, {10.0f, 0.0f, -10.0f});
        d = world.create("d", 1, {0.0f, 0.0f, -20.0f});
        obs = std::make_unique<ObservationSystem>(world);
        obs->track(a);
        obs->track(b);
        obs->track(d);
        world.events().subscribe<ObservationChanged>([this](const ObservationChanged& e) { events.push_back(e); });
        look(-90.0f);
    }
    void look(float yaw) {
        world.camera().setOrientation(yaw, 0.0f);
        obs->setView(observationViewFromCamera(world.camera(), 1.0f));
    }
    void run(float seconds, float dt = 1.0f / 60.0f) {
        for (float t = 0; t < seconds - 1e-6f; t += dt) obs->update(dt);
    }
};

void testObservationCpu() {
    ViewFixture f;
    const ObservationView v = f.obs->view();
    CHECK(std::abs(v.forward.z + 1.0f) < 1e-5f);

    f.obs->update(1.0f / 60.0f);
    CHECK(f.obs->state(f.a) == ObservationState::Unobserved);
    f.run(0.1f);
    CHECK(f.obs->state(f.a) == ObservationState::Seen);
    f.run(0.3f);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);
    CHECK(f.obs->focusEntity() == f.a);
    CHECK(f.obs->state(f.b) == ObservationState::Unobserved);
    CHECK(f.obs->state(f.d) == ObservationState::Unobserved);
    CHECK(f.events.size() == 2);
    CHECK(f.events[0].entity == f.a && f.events[0].to == ObservationState::Seen);
    CHECK(f.events[1].entity == f.a && f.events[1].from == ObservationState::Seen && f.events[1].to == ObservationState::Attended);

    const auto countA = [&] { size_t n = 0; for (const auto& e : f.events) n += e.entity == f.a; return n; };
    const size_t before = countA();
    f.look(-45.0f);
    f.run(0.1f);
    f.look(-90.0f);
    f.run(0.5f);
    CHECK(countA() == before);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);

    f.look(-45.0f);
    f.run(0.2f);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);
    f.run(0.4f);
    CHECK(f.obs->state(f.a) == ObservationState::Unobserved);
    CHECK(f.obs->state(f.b) == ObservationState::Attended);
    CHECK(f.obs->attended().size() == 1 && f.obs->attended()[0] == f.b);

    f.look(90.0f);
    f.run(1.0f);
    CHECK(f.obs->state(f.b) == ObservationState::Unobserved);
    CHECK(f.obs->stats().cpuUpdates > 0);

    f.world.destroy(f.a);
    f.look(-90.0f);
    f.run(0.5f);
    CHECK(!f.obs->tracked(f.a));
    CHECK(f.obs->state(f.d) != ObservationState::Unobserved);
}

void testObservationGpu() {
    ViewFixture f;
    ObservationConfig cfg = f.obs->config();
    cfg.logLatency = false;
    f.obs->setConfig(cfg);
    f.look(90.0f);
    std::vector<uint32_t> ids(64, 0);
    ids[4 * 8 + 4] = f.a.index + 1;
    for (int i = 0; i < 40; ++i) {
        f.obs->submitGpuIds(f.obs->frame(), f.obs->time(), 8, 8, ids);
        f.obs->update(1.0f / 60.0f);
    }
    CHECK(f.obs->stats().usingGpu);
    CHECK(f.obs->state(f.a) == ObservationState::Attended);
    CHECK(f.obs->stats().gpuSamples == 40);
    CHECK(f.obs->stats().lastLatencyFrames == 0);

    ids.assign(64, 0);
    ids[0] = f.a.index + 1;
    for (int i = 0; i < 40; ++i) {
        f.obs->submitGpuIds(f.obs->frame(), f.obs->time(), 8, 8, ids);
        f.obs->update(1.0f / 60.0f);
    }
    CHECK(f.obs->state(f.a) == ObservationState::Seen);

    const uint64_t rejected = f.obs->stats().gpuRejected;
    f.obs->submitGpuIds(f.obs->frame() - 30, f.obs->time() - 0.5, 8, 8, ids);
    CHECK(f.obs->stats().gpuRejected == rejected + 1);
    CHECK(f.obs->stats().maxLatencySeconds >= 0.5);
    f.run(1.0f);
    CHECK(!f.obs->stats().usingGpu);
    CHECK(f.obs->state(f.a) == ObservationState::Unobserved);
}

}

void runObservationTests() {
    testObservationCpu();
    testObservationGpu();
}
