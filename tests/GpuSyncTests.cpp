#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.renderer;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what, size_t frame, size_t entity) {
    ++g_checks;
    if (condition) return;
    if (++g_failures <= 20) std::printf("FAIL frame %zu entity %zu: %s\n", frame, entity, what);
}

bool nearlyEqual(const glm::mat4& a, const glm::mat4& b) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const float tolerance = 1e-3f * std::max(1.0f, std::abs(b[column][row]));
            if (std::abs(a[column][row] - b[column][row]) > tolerance) return false;
        }
    }
    return true;
}

struct Fuzzer {
    Engine& engine;
    World& world;
    uint32_t meshes[2];
    std::mt19937 rng{7};
    std::vector<EntityHandle> alive;

    float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
    size_t index(size_t count) { return std::uniform_int_distribution<size_t>(0, count - 1)(rng); }
    EntityHandle any() { return alive[index(alive.size())]; }

    void prune() { std::erase_if(alive, [&](EntityHandle e) { return !world.isAlive(e); }); }

    EntityHandle spawn(EntityHandle parent) {
        return world.create("fuzz", meshes[index(2)], glm::vec3(uniform(-20.0f, 20.0f), uniform(-20.0f, 20.0f), uniform(-40.0f, -5.0f)), glm::vec3(uniform(0.3f, 1.5f)), parent);
    }

    void seed(size_t count) {
        for (size_t i = 0; i < count; ++i) alive.push_back(spawn(alive.empty() || index(3) == 0 ? NULL_ENTITY : any()));
    }

    void mutate(size_t frame) {
        const size_t edits = 1 + index(24);
        for (size_t i = 0; i < edits && !alive.empty(); ++i) {
            const EntityHandle e = any();
            switch (index(9)) {
                case 0:
                case 1:
                case 2: world.setPosition(e, glm::vec3(uniform(-20.0f, 20.0f), uniform(-20.0f, 20.0f), uniform(-40.0f, -5.0f))); break;
                case 3: world.setRotation(e, glm::angleAxis(uniform(0.0f, 6.28f), glm::normalize(glm::vec3(uniform(-1, 1), uniform(-1, 1), uniform(0.1f, 1))))); break;
                case 4: world.setParent(e, index(4) == 0 ? NULL_ENTITY : any(), index(2) == 0); break;
                case 5: alive.push_back(spawn(index(2) == 0 ? NULL_ENTITY : e)); break;
                case 6: world.destroy(e); break;
                case 7: world.setVisible(e, !world.isVisible(e)); break;
                case 8: world.setMesh(e, meshes[index(2)]); break;
            }
        }
        if (frame % 40 == 20) {
            std::vector<std::pair<EntityHandle, glm::vec3>> moves;
            for (EntityHandle e : alive) moves.push_back({e, glm::vec3(uniform(-20.0f, 20.0f), uniform(-20.0f, 20.0f), uniform(-40.0f, -5.0f))});
            world.setPositions(moves);
        }
        if (frame % 50 == 25) {
            std::vector<EntityHandle> doomed;
            for (size_t i = 0; i < 20 && !alive.empty(); ++i) doomed.push_back(any());
            world.destroyBatch(doomed);
        }
        if (frame % 60 == 30) seed(200);
        prune();
    }

    void verify(size_t frame) {
        const std::vector<GpuEntityState> states = engine.readGpuEntityStates();
        for (EntityHandle e : alive) {
            if (e.index >= states.size()) {
                check(false, "entity beyond GPU capacity", frame, e.index);
                continue;
            }
            const GpuEntityState& gpu = states[e.index];
            check(nearlyEqual(gpu.world, world.getWorldMatrix(e)), "GPU world matrix differs from World", frame, e.index);
            const bool drawn = world.isVisible(e) && world.getMesh(e) != NO_MESH;
            check(drawn == (gpu.cullSphere.w >= 0.0f), "GPU cull sphere visibility differs from World", frame, e.index);
        }
    }
};

}  // namespace

int main() {
    if (!glfwInit()) {
        std::printf("GLFW unavailable, skipping GPU sync tests\n");
        return 0;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640, 360, "Lit GPU Sync Tests", nullptr, nullptr);
    if (!window) {
        std::printf("No window available, skipping GPU sync tests\n");
        glfwTerminate();
        return 0;
    }

    int failures = 0;
    {
        Engine engine;
        engine.init(window, 640, 360);
        Fuzzer fuzzer{engine, engine.world(), {engine.loadMesh("cube"), engine.loadMesh("sphere")}};
        fuzzer.seed(3000);

        for (size_t frame = 0; frame < 300; ++frame) {
            fuzzer.mutate(frame);
            engine.update();
            engine.present();
            fuzzer.verify(frame);
        }
        engine.cleanup();
        failures = g_failures;
    }
    glfwDestroyWindow(window);
    glfwTerminate();

    std::printf("%d checks, %d failures\n", g_checks, failures);
    return failures == 0 ? 0 : 1;
}
