#pragma once

inline constexpr uint32_t kBenchMesh = 1;
inline constexpr float kBenchMeshRadius = 0.5f;
inline constexpr float kBenchSceneExtent = 400.0f;
inline constexpr float kBenchHalf = 0.5f;

inline glm::vec3 randomPoint(Pcg32& rng, float extent) {
    return glm::vec3((rng.uniform() - kBenchHalf) * extent, (rng.uniform() - kBenchHalf) * extent * 0.25f, (rng.uniform() - kBenchHalf) * extent);
}

inline glm::vec3 randomDirection(Pcg32& rng) {
    const glm::vec3 raw(rng.uniform() - kBenchHalf, rng.uniform() - kBenchHalf, rng.uniform() - kBenchHalf);
    const float length = glm::length(raw);
    return length > 1.0e-4f ? raw / length : glm::vec3(0.0f, 0.0f, -1.0f);
}

inline std::unique_ptr<World> makeEmptyWorld() {
    auto world = std::make_unique<World>();
    world->setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, kBenchMeshRadius); });
    return world;
}

inline std::unique_ptr<World> makeScatteredWorld(size_t count, float extent = kBenchSceneExtent) {
    auto world = makeEmptyWorld();
    Pcg32 rng(bench::kSeed);
    world->createBatch(count, [&](size_t, EntityDesc& desc) {
        desc.mesh = kBenchMesh;
        desc.position = randomPoint(rng, extent);
    });
    return world;
}

inline std::vector<EntityHandle> allHandles(const World& world) {
    std::vector<EntityHandle> handles;
    handles.reserve(world.aliveCount());
    world.forEach([&](EntityHandle handle) { handles.push_back(handle); });
    return handles;
}
