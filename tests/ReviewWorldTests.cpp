#include "ReviewSuites.hpp"
#include "TestSupport.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Rng;

namespace {

constexpr float kQuiet = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

struct Scene {
    World world;
    std::vector<EntityHandle> handles;
};

void fill(Scene& scene, Pcg32& rng, int count, float extent, float maxScale) {
    scene.world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    for (int index = 0; index < count; ++index) {
        const glm::vec3 position(rng.range(-extent, extent), rng.range(-extent, extent), rng.range(-extent, extent));
        const glm::vec3 scale(rng.range(0.05f, maxScale), rng.range(0.05f, maxScale), rng.range(0.05f, maxScale));
        const EntityHandle handle = scene.world.create("e", 1, position, scale);
        scene.world.setRotation(handle, glm::normalize(glm::quat(rng.range(0.1f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f))));
        scene.handles.push_back(handle);
    }
}

std::optional<float> bruteRay(World& world, const std::vector<EntityHandle>& handles, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    std::optional<float> best;
    for (EntityHandle handle : handles) {
        const glm::vec4 bounds = world.getWorldBounds(handle);
        const glm::vec3 toCenter = glm::vec3(bounds) - origin;
        const float along = glm::dot(toCenter, direction);
        const float perpendicular2 = glm::dot(toCenter, toCenter) - along * along;
        if (perpendicular2 > bounds.w * bounds.w) continue;
        const float half = std::sqrt(bounds.w * bounds.w - perpendicular2);
        float distance = along - half;
        if (distance < 0.0f) distance = along + half;
        if (distance < 0.0f || distance > maxDistance) continue;
        if (!best || distance < *best) best = distance;
    }
    return best;
}

int compareRays(Scene& scene, Pcg32& rng, int rays, float extent, float maxDistance, bool insideStarts) {
    int mismatches = 0;
    for (int ray = 0; ray < rays; ++ray) {
        glm::vec3 origin(rng.range(-extent, extent), rng.range(-extent, extent), rng.range(-extent, extent));
        if (insideStarts) origin = glm::vec3(scene.world.getWorldBounds(scene.handles[rng.next() % scene.handles.size()]));
        const glm::vec3 direction = glm::normalize(glm::vec3(rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)) + glm::vec3(1.0e-3f));
        const std::optional<float> expected = bruteRay(scene.world, scene.handles, origin, direction, maxDistance);
        const auto hit = scene.world.raycast(origin, direction * 3.0f, maxDistance);
        if (expected.has_value() != hit.has_value()) {
            ++mismatches;
            continue;
        }
        if (hit && std::fabs(hit->distance - *expected) > 1.0e-3f * std::max(1.0f, *expected)) ++mismatches;
    }
    return mismatches;
}

void testRaycastBruteForce() {
    Pcg32 rng(31337);
    {
        Scene scene;
        fill(scene, rng, 1500, 400.0f, 12.0f);
        CHECK(compareRays(scene, rng, 300, 400.0f, 1.0e30f, false) == 0);
        CHECK(compareRays(scene, rng, 300, 400.0f, 250.0f, false) == 0);
        CHECK(compareRays(scene, rng, 200, 400.0f, 1.0e30f, true) == 0);
    }
    {
        Scene scene;
        fill(scene, rng, 400, 5.0e4f, 30.0f);
        CHECK(compareRays(scene, rng, 300, 5.0e4f, 1.0e30f, false) == 0);
    }
}

void testRaycastHugeExtent() {
    Pcg32 rng(99);
    Scene scene;
    fill(scene, rng, 300, 2.0e7f, 50.0f);
    CHECK(compareRays(scene, rng, 200, 2.0e7f, 1.0e30f, true) == 0);
    CHECK(compareRays(scene, rng, 200, 2.0e7f, 1.0e30f, false) == 0);
}

void testRaycastSparseLongRange() {
    Scene scene;
    scene.world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    const EntityHandle near = scene.world.create("near", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    const EntityHandle far = scene.world.create("far", 1, glm::vec3(0.0f, 0.0f, -2.0e5f));
    scene.handles = {near, far};
    scene.world.destroy(near);
    scene.handles = {far};
    const auto hit = scene.world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit.has_value());
    if (hit) CHECK(std::fabs(hit->distance - 1.99999e5f) < 10.0f);
}

void testRaycastDegenerateInputs() {
    Pcg32 rng(5);
    Scene scene;
    fill(scene, rng, 200, 100.0f, 5.0f);
    CHECK(!scene.world.raycast(glm::vec3(0.0f), glm::vec3(0.0f)).has_value());
    CHECK(!scene.world.raycast(glm::vec3(0.0f), glm::vec3(kQuiet, 0.0f, 1.0f)).has_value());
    CHECK(!scene.world.raycast(glm::vec3(kQuiet, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)).has_value());
    CHECK(!scene.world.raycast(glm::vec3(0.0f), glm::vec3(kInf, 0.0f, 0.0f)).has_value());
    (void)scene.world.raycast(glm::vec3(1.0e30f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    (void)scene.world.raycast(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), kQuiet);
    CHECK(!scene.world.raycast(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), -5.0f).has_value());
    const auto tiny = scene.world.raycast(glm::vec3(0.0f), glm::vec3(1.0e-20f, 0.0f, 0.0f));
    CHECK(!tiny.has_value());
}

void testOverlapBruteForce() {
    Pcg32 rng(2024);
    Scene scene;
    fill(scene, rng, 2000, 300.0f, 10.0f);
    std::vector<EntityHandle> found;
    int mismatches = 0;
    for (int query = 0; query < 200; ++query) {
        const glm::vec3 center(rng.range(-300.0f, 300.0f), rng.range(-300.0f, 300.0f), rng.range(-300.0f, 300.0f));
        const float radius = rng.range(0.0f, 120.0f);
        scene.world.overlapSphere(center, radius, found);
        std::vector<uint32_t> expected;
        for (EntityHandle handle : scene.handles) {
            const glm::vec4 bounds = scene.world.getWorldBounds(handle);
            const glm::vec3 offset = glm::vec3(bounds) - center;
            const float reach = radius + bounds.w;
            if (glm::dot(offset, offset) <= reach * reach * 0.999f) expected.push_back(handle.index);
        }
        std::vector<uint32_t> got;
        for (EntityHandle handle : found) got.push_back(handle.index);
        std::sort(got.begin(), got.end());
        std::sort(expected.begin(), expected.end());
        if (!std::includes(got.begin(), got.end(), expected.begin(), expected.end())) ++mismatches;
        for (EntityHandle handle : found) {
            const glm::vec4 bounds = scene.world.getWorldBounds(handle);
            const glm::vec3 offset = glm::vec3(bounds) - center;
            const float reach = radius + bounds.w;
            if (glm::dot(offset, offset) > reach * reach * 1.001f + 1.0e-3f) ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

void testOverlapDegenerate() {
    Pcg32 rng(8);
    Scene scene;
    fill(scene, rng, 300, 100.0f, 5.0f);
    std::vector<EntityHandle> found;
    scene.world.overlapSphere(glm::vec3(kQuiet), 5.0f, found);
    CHECK(found.empty());
    scene.world.overlapSphere(glm::vec3(0.0f), kQuiet, found);
    CHECK(found.empty());
    scene.world.overlapSphere(glm::vec3(0.0f), -3.0f, found);
    CHECK(found.empty());
    scene.world.overlapSphere(glm::vec3(0.0f), kInf, found);
    CHECK(found.size() == scene.handles.size());
    scene.world.overlapSphere(glm::vec3(1.0e30f), 1.0f, found);
    CHECK(found.empty());
}

void testFarEntitiesDoNotAlias() {
    Scene scene;
    scene.world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    const float cell = 16.0f;
    for (int step = 0; step < 4; ++step) scene.world.create("filler", 1, glm::vec3(static_cast<float>(step) * 2.0f, 0.0f, 0.0f));
    const EntityHandle far = scene.world.create("far", 1, glm::vec3(cell * 2097152.0f, 0.0f, 0.0f));
    std::vector<EntityHandle> found;
    scene.world.overlapSphere(glm::vec3(0.0f), 20.0f, found);
    CHECK(std::find(found.begin(), found.end(), far) == found.end());
    const auto hit = scene.world.raycast(glm::vec3(-100.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 200.0f);
    CHECK(hit.has_value() && hit->entity != far);
}

void testNanEntityDoesNotBreakQueries() {
    Scene scene;
    scene.world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    const EntityHandle good = scene.world.create("good", 1, glm::vec3(0.0f, 0.0f, -5.0f));
    const EntityHandle bad = scene.world.create("bad", 1, glm::vec3(kQuiet, 0.0f, 0.0f));
    const EntityHandle huge = scene.world.create("huge", 1, glm::vec3(0.0f), glm::vec3(kInf));
    scene.world.setPosition(huge, glm::vec3(1.0e10f));
    std::vector<EntityHandle> found;
    scene.world.overlapSphere(glm::vec3(0.0f, 0.0f, -5.0f), 2.0f, found);
    CHECK(std::find(found.begin(), found.end(), good) != found.end());
    CHECK(std::find(found.begin(), found.end(), bad) == found.end());
    const auto hit = scene.world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit.has_value() && hit->entity == good);
    scene.world.destroy(bad);
    scene.world.destroy(good);
    CHECK(scene.world.validate().empty());
}

}

void runReviewWorldTests() {
    testRaycastBruteForce();
    testRaycastDegenerateInputs();
    testOverlapBruteForce();
    testOverlapDegenerate();
    testNanEntityDoesNotBreakQueries();
    testRaycastSparseLongRange();
    testFarEntitiesDoNotAlias();
    testRaycastHugeExtent();
}
