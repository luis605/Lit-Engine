#include <cmath>
#include <optional>
#include <vector>

#include "TestSupport.hpp"

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Rng;

#include "WorldSuites.hpp"

namespace {

constexpr float kSphereRadius = 1.0f;
constexpr float kDistanceTolerance = 0.5f;
constexpr float kUnbounded = 1.0e30f;
constexpr float kBeyondKeyLimit = 3.0e7f;
constexpr int kRaysPerScene = 400;

struct Scene {
    World world;
    std::vector<EntityHandle> handles;
    std::vector<glm::vec3> positions;

    Scene() {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, kSphereRadius); });
    }

    void add(const glm::vec3& position) {
        handles.push_back(world.create("entity", 1, position));
        positions.push_back(position);
    }
};

std::optional<float> referenceDistance(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, size_t& bestIndex) {
    std::optional<float> best;
    const double directionLength = std::sqrt(static_cast<double>(direction.x) * direction.x + static_cast<double>(direction.y) * direction.y + static_cast<double>(direction.z) * direction.z);
    const double unit[3] = {direction.x / directionLength, direction.y / directionLength, direction.z / directionLength};
    for (size_t index = 0; index < scene.positions.size(); ++index) {
        const double toCenter[3] = {static_cast<double>(scene.positions[index].x) - origin.x, static_cast<double>(scene.positions[index].y) - origin.y, static_cast<double>(scene.positions[index].z) - origin.z};
        const double along = toCenter[0] * unit[0] + toCenter[1] * unit[1] + toCenter[2] * unit[2];
        const double perpendicular2 = toCenter[0] * toCenter[0] + toCenter[1] * toCenter[1] + toCenter[2] * toCenter[2] - along * along;
        const double radius2 = static_cast<double>(kSphereRadius) * kSphereRadius;
        if (perpendicular2 > radius2) continue;
        const double half = std::sqrt(radius2 - perpendicular2);
        double distance = along - half;
        if (distance < 0.0) distance = along + half;
        if (distance < 0.0 || distance > maxDistance) continue;
        if (!best || static_cast<float>(distance) < *best) {
            best = static_cast<float>(distance);
            bestIndex = index;
        }
    }
    return best;
}

void compareRay(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    size_t expectedIndex = 0;
    const auto expected = referenceDistance(scene, origin, direction, maxDistance, expectedIndex);
    const auto actual = const_cast<World&>(scene.world).raycast(origin, direction, maxDistance);
    if (!expected) {
        CHECK(!actual.has_value() || actual->distance <= maxDistance);
        if (actual) CHECK(actual->distance <= maxDistance + kDistanceTolerance);
        return;
    }
    CHECK(actual.has_value());
    if (!actual) return;
    CHECK(std::abs(actual->distance - *expected) <= kDistanceTolerance);
}

void compareScene(Scene& scene, float extent, uint32_t seed, float shortRange) {
    Pcg32 rng(seed);
    for (int rayIndex = 0; rayIndex < kRaysPerScene; ++rayIndex) {
        const glm::vec3 origin((rng.uniform() - 0.5f) * extent, (rng.uniform() - 0.5f) * extent * 0.25f, (rng.uniform() - 0.5f) * extent);
        const glm::vec3 target = scene.positions[static_cast<size_t>(rng.uniform() * static_cast<float>(scene.positions.size())) % scene.positions.size()];
        const glm::vec3 aimed = target - origin;
        const glm::vec3 random(rng.uniform() - 0.5f, rng.uniform() - 0.5f, rng.uniform() - 0.5f);
        for (const glm::vec3& direction : {aimed, random, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}) {
            if (glm::length(direction) < 1.0e-3f) continue;
            compareRay(scene, origin, direction, kUnbounded);
            compareRay(scene, origin, direction, shortRange);
        }
    }
}

void testSparseHugeScene() {
    Scene scene;
    Pcg32 rng(11);
    constexpr float kExtent = 1.0e6f;
    for (int index = 0; index < 600; ++index) scene.add(glm::vec3((rng.uniform() - 0.5f) * kExtent, (rng.uniform() - 0.5f) * kExtent * 0.25f, (rng.uniform() - 0.5f) * kExtent));
    compareScene(scene, kExtent, 21, 5000.0f);
}

void testDenseScene() {
    Scene scene;
    Pcg32 rng(12);
    constexpr float kExtent = 1000.0f;
    for (int index = 0; index < 8000; ++index) scene.add(glm::vec3((rng.uniform() - 0.5f) * kExtent, (rng.uniform() - 0.5f) * kExtent * 0.25f, (rng.uniform() - 0.5f) * kExtent));
    compareScene(scene, kExtent, 22, 60.0f);
}

void addCluster(Scene& scene, Pcg32& rng, const glm::vec3& center, int count, float spread) {
    for (int index = 0; index < count; ++index) scene.add(center + glm::vec3(rng.uniform() - 0.5f, rng.uniform() - 0.5f, rng.uniform() - 0.5f) * spread);
}

void testClusteredScenesAfterMutation() {
    Scene scene;
    Pcg32 rng(14);
    constexpr float kExtent = 6000.0f;
    for (int cluster = 0; cluster < 8; ++cluster) {
        const glm::vec3 center((rng.uniform() - 0.5f) * kExtent, (rng.uniform() - 0.5f) * kExtent * 0.25f, (rng.uniform() - 0.5f) * kExtent);
        addCluster(scene, rng, center, 400, 80.0f);
    }
    compareScene(scene, kExtent, 24, 400.0f);
    for (size_t index = 0; index < scene.handles.size(); index += 3) {
        const glm::vec3 moved((rng.uniform() - 0.5f) * kExtent, (rng.uniform() - 0.5f) * kExtent * 0.25f, (rng.uniform() - 0.5f) * kExtent);
        scene.world.setPosition(scene.handles[index], moved);
        scene.positions[index] = moved;
    }
    compareScene(scene, kExtent, 25, 400.0f);
    for (size_t index = 1; index < scene.handles.size(); index += 2) {
        const glm::vec3 parked(1.0e5f, 0.0f, 1.0e5f + static_cast<float>(index) * 10.0f);
        scene.world.setPosition(scene.handles[index], parked);
        scene.positions[index] = parked;
    }
    compareScene(scene, kExtent, 26, 400.0f);
}

void testFarAndUnindexedEntities() {
    Scene scene;
    Pcg32 rng(13);
    for (int index = 0; index < 500; ++index) scene.add(glm::vec3((rng.uniform() - 0.5f) * 2000.0f, 0.0f, (rng.uniform() - 0.5f) * 2000.0f));
    scene.add(glm::vec3(kBeyondKeyLimit, 0.0f, 0.0f));
    scene.add(glm::vec3(-kBeyondKeyLimit, 5.0f, 0.0f));
    scene.add(glm::vec3(0.0f, 0.0f, 9.0e8f));
    compareScene(scene, 2000.0f, 23, 300.0f);
    const auto east = scene.world.raycast(glm::vec3(0.0f, 30000.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(!east.has_value());
    const auto farHit = scene.world.raycast(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(farHit.has_value());
}

void testTiesPreferLowerIndex() {
    Scene scene;
    scene.add(glm::vec3(0.0f, 0.0f, -2.0e5f));
    scene.add(glm::vec3(0.0f, 0.0f, -2.0e5f));
    scene.add(glm::vec3(3.0f, 0.0f, -10.0f));
    scene.add(glm::vec3(5.0e5f, 0.0f, 5.0e5f));
    const auto hit = scene.world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit.has_value());
    if (hit) CHECK(hit->entity.index == scene.handles[0].index);
    CHECK(!scene.world.raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)).has_value());
    CHECK(!scene.world.raycast(glm::vec3(0.0f, 1.0e6f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 5.0e5f).has_value());
}

}

void runSpatialFarTests() {
    testSparseHugeScene();
    testDenseScene();
    testClusteredScenesAfterMutation();
    testFarAndUnindexedEntities();
    testTiesPreferLowerIndex();
}
