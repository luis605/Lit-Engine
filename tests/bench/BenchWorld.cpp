#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Harness.hpp"

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Rng;

#include "WorldSetup.hpp"

namespace bench {

namespace {

constexpr size_t kSmallCount = 100000;
constexpr size_t kLargeCount = 1000000;
constexpr size_t kScriptCount = 1000;
constexpr size_t kRootCount = 1000;
constexpr size_t kChildrenPerNode = 10;
constexpr float kFrameSeconds = 1.0f / 60.0f;
constexpr float kSpinRadians = 0.01f;

struct Velocity {
    glm::vec3 value{0.0f};
};

struct Spinner : Script {
    void onUpdate(World& world, EntityHandle entity, float deltaTime) override { world.rotate(entity, glm::vec3(0.0f, 1.0f, 0.0f), kSpinRadians * deltaTime); }
};

Case spawnCase(size_t count, int iterations) {
    auto world = std::make_shared<std::unique_ptr<World>>();
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(count);
    benchCase.iterations = iterations;
    benchCase.warmup = 1;
    benchCase.prepare = [world] { *world = makeEmptyWorld(); };
    benchCase.run = [world, count] {
        Pcg32 rng(kSeed);
        (*world)->createBatch(count, [&](size_t, EntityDesc& desc) {
            desc.mesh = kBenchMesh;
            desc.position = randomPoint(rng, kBenchSceneExtent);
        });
    };
    return benchCase;
}

Case destroyCase(size_t count, int iterations) {
    auto world = std::make_shared<std::unique_ptr<World>>();
    auto handles = std::make_shared<std::vector<EntityHandle>>();
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(count);
    benchCase.iterations = iterations;
    benchCase.warmup = 1;
    benchCase.prepare = [world, handles, count] {
        *world = makeScatteredWorld(count);
        *handles = allHandles(**world);
    };
    benchCase.run = [world, handles] { (*world)->destroyBatch(*handles); };
    return benchCase;
}

Case iterateComponentCase(size_t count) {
    auto world = std::shared_ptr<World>(makeScatteredWorld(count));
    world->forEach([&](EntityHandle handle) { world->add<Velocity>(handle)->value = glm::vec3(1.0f, 0.0f, 0.5f); });
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(count);
    benchCase.iterations = 30;
    benchCase.run = [world] {
        float total = 0.0f;
        world->each<Velocity>([&](EntityHandle, Velocity& velocity) { total += velocity.value.x; });
        sink(total);
    };
    return benchCase;
}

Case iteratePositionCase(size_t count) {
    auto world = std::shared_ptr<World>(makeScatteredWorld(count));
    auto handles = std::make_shared<std::vector<EntityHandle>>(allHandles(*world));
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(count);
    benchCase.iterations = 30;
    benchCase.run = [world, handles] {
        glm::vec3 total(0.0f);
        for (const EntityHandle handle : *handles) total += world->getPosition(handle);
        sink(total.x);
    };
    return benchCase;
}

Case updateCase(size_t count) {
    auto world = std::shared_ptr<World>(makeScatteredWorld(count));
    size_t attached = 0;
    world->forEach([&](EntityHandle handle) {
        if (attached++ < kScriptCount) world->attach<Spinner>(handle);
    });
    world->update(kFrameSeconds);
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(count);
    benchCase.iterations = 60;
    benchCase.run = [world] { world->update(kFrameSeconds); };
    return benchCase;
}

struct Hierarchy {
    std::unique_ptr<World> world;
    std::vector<EntityHandle> roots;
    std::vector<EntityHandle> all;
};

std::shared_ptr<Hierarchy> buildHierarchy() {
    auto hierarchy = std::make_shared<Hierarchy>();
    hierarchy->world = makeEmptyWorld();
    World& world = *hierarchy->world;
    std::vector<EntityHandle> level;
    for (size_t index = 0; index < kRootCount; ++index) {
        EntityDesc desc;
        desc.mesh = kBenchMesh;
        desc.position = glm::vec3(static_cast<float>(index), 0.0f, 0.0f);
        const EntityHandle root = world.create(desc);
        hierarchy->roots.push_back(root);
        level.push_back(root);
    }
    for (int depth = 0; depth < 2; ++depth) {
        std::vector<EntityHandle> next;
        for (const EntityHandle parent : level) {
            for (size_t child = 0; child < kChildrenPerNode; ++child) {
                EntityDesc desc;
                desc.mesh = kBenchMesh;
                desc.parent = parent;
                desc.position = glm::vec3(0.0f, 1.0f, static_cast<float>(child));
                next.push_back(world.create(desc));
            }
        }
        level = std::move(next);
    }
    hierarchy->all = allHandles(world);
    return hierarchy;
}

Case transformCase() {
    auto hierarchy = buildHierarchy();
    auto step = std::make_shared<float>(0.0f);
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(hierarchy->all.size());
    benchCase.iterations = 30;
    benchCase.run = [hierarchy, step] {
        World& world = *hierarchy->world;
        *step += 0.25f;
        for (const EntityHandle root : hierarchy->roots) world.setPosition(root, glm::vec3(*step, 0.0f, 0.0f));
        glm::vec3 total(0.0f);
        for (const EntityHandle handle : hierarchy->all) total += glm::vec3(world.getWorldMatrix(handle)[3]);
        sink(total.x);
    };
    return benchCase;
}

}

void registerWorldBenches() {
    add("world.spawn_100k", [] { return spawnCase(kSmallCount, 10); });
    add("world.spawn_1m", [] { return spawnCase(kLargeCount, 3); });
    add("world.destroy_100k", [] { return destroyCase(kSmallCount, 10); });
    add("world.destroy_1m", [] { return destroyCase(kLargeCount, 3); });
    add("world.iterate_component_1m", [] { return iterateComponentCase(kLargeCount); });
    add("world.iterate_position_1m", [] { return iteratePositionCase(kLargeCount); });
    add("world.update_100k_1k_scripts", [] { return updateCase(kSmallCount); });
    add("world.transform_propagate_111k", [] { return transformCase(); });
}

}
