#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.Observation;
import Engine.Render.entity;
import Engine.glm;

namespace {
int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, expr);
    }
}
}  // namespace

#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

int main() {
    if (!glfwInit()) {
        std::printf("GLFW unavailable, skipping GPU observation tests\n");
        return 0;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640, 360, "Lit GPU Observation Tests", nullptr, nullptr);
    if (!window) {
        std::printf("No window available, skipping GPU observation tests\n");
        glfwTerminate();
        return 0;
    }
    {
        Engine engine;
        engine.init(window, 640, 360);
        World& world = engine.world();
        const uint32_t cube = engine.loadMesh("cube");
        auto spawn = [&](const char* name, glm::vec3 position, glm::vec3 scale) {
            EntityDesc d;
            d.name = name;
            d.mesh = cube;
            d.position = position;
            d.scale = scale;
            return world.create(d);
        };
        const EntityHandle center = spawn("center", glm::vec3(0.0f, 0.0f, -8.0f), glm::vec3(1.0f));
        const EntityHandle side = spawn("side", glm::vec3(3.0f, 0.0f, -8.0f), glm::vec3(1.0f));
        const EntityHandle hidden = spawn("hidden", glm::vec3(-3.0f, 0.0f, -8.0f), glm::vec3(1.0f));
        const EntityHandle behind = spawn("behind", glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(1.0f));
        spawn("wall", glm::vec3(-3.0f, 0.0f, -4.0f), glm::vec3(4.0f, 6.0f, 0.5f));
        world.camera().setPos(glm::vec3(0.0f));
        world.camera().setOrientation(-90.0f, 0.0f);

        ObservationConfig config;
        config.logLatency = false;
        config.seenEnter = 0.0f;
        config.attendEnter = 0.0f;
        config.seenExit = 0.0f;
        config.attendExit = 0.0f;
        ObservationSystem& obs = engine.enableObservation(config, true);
        for (EntityHandle e : {center, side, hidden, behind}) obs.track(e);

        for (int frame = 0; frame < 30; ++frame) {
            engine.tick(1.0f / 60.0f);
            engine.update();
            engine.present();
        }
        const ObservationStats& stats = obs.stats();
        std::printf("observation: gpu samples %llu rejected %llu cpu %llu latency last %.2f ms frames %u\n", static_cast<unsigned long long>(stats.gpuSamples), static_cast<unsigned long long>(stats.gpuRejected), static_cast<unsigned long long>(stats.cpuUpdates), stats.lastLatencySeconds * 1000.0, stats.lastLatencyFrames);
        CHECK(stats.gpuSamples > 5);
        CHECK(stats.maxLatencyFrames <= 4);
        CHECK(obs.state(center) == ObservationState::Attended);
        CHECK(obs.state(side) == ObservationState::Seen);
        CHECK(obs.state(hidden) == ObservationState::Unobserved);
        CHECK(obs.state(behind) == ObservationState::Unobserved);
        const uint64_t cpuBefore = stats.cpuUpdates;
        world.camera().setOrientation(90.0f, 0.0f);
        for (int frame = 0; frame < 30; ++frame) {
            engine.tick(1.0f / 60.0f);
            engine.update();
            engine.present();
        }
        CHECK(obs.state(behind) == ObservationState::Attended);
        CHECK(obs.state(center) == ObservationState::Unobserved);
        CHECK(obs.stats().cpuUpdates - cpuBefore < 10);
        engine.cleanup();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
