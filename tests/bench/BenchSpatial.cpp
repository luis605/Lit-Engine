#include <cstdint>
#include <memory>
#include <vector>

#include "Harness.hpp"

import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;
import Engine.Rng;

#include "WorldSetup.hpp"

namespace bench {

namespace {

constexpr size_t kEntityCount = 100000;
constexpr size_t kRayCount = 1000;
constexpr size_t kOverlapCount = 1000;
constexpr size_t kFrustumCount = 100;
constexpr size_t kMovedCount = 1000;
constexpr float kRayRange = 200.0f;
constexpr float kOverlapRadius = 8.0f;
constexpr float kMoveStep = 0.5f;
constexpr float kYawRangeDegrees = 360.0f;
constexpr float kPitchRangeDegrees = 60.0f;
constexpr float kAspectWidth = 16.0f;
constexpr float kAspectHeight = 9.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 200.0f;
constexpr size_t kSparseCount = 1000;
constexpr float kSparseExtent = 1.0e6f;
constexpr float kDenseExtent = 1000.0f;
constexpr float kUnbounded = 1.0e30f;
constexpr float kShortRange = 100.0f;
constexpr size_t kClusterCount = 20;
constexpr size_t kClusterMembers = 1000;
constexpr float kClusterSpread = 200.0f;
constexpr float kClusteredExtent = 4000.0f;
constexpr float kLongRange = 1500.0f;
constexpr size_t kFarCount = 8;
constexpr float kFarDistance = 2.0e7f;

struct SpatialFixture {
    std::unique_ptr<World> world;
    std::vector<glm::vec3> origins;
    std::vector<glm::vec3> directions;
    std::vector<EntityHandle> handles;
    std::vector<EntityHandle> scratch;
};

std::shared_ptr<SpatialFixture> buildFixture() {
    auto fixture = std::make_shared<SpatialFixture>();
    fixture->world = makeScatteredWorld(kEntityCount);
    fixture->handles = allHandles(*fixture->world);
    Pcg32 rng(kSeed + 1);
    for (size_t index = 0; index < kRayCount; ++index) {
        fixture->origins.push_back(randomPoint(rng, kBenchSceneExtent));
        fixture->directions.push_back(randomDirection(rng));
    }
    fixture->world->overlapSphere(glm::vec3(0.0f), 1.0f);
    return fixture;
}

Case raycastCase() {
    auto fixture = buildFixture();
    Case benchCase;
    benchCase.itemsPerIteration = kRayCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture] {
        size_t hits = 0;
        for (size_t index = 0; index < kRayCount; ++index) {
            hits += fixture->world->raycast(fixture->origins[index], fixture->directions[index], kRayRange).has_value() ? 1 : 0;
        }
        sink(static_cast<double>(hits));
    };
    return benchCase;
}

Case overlapCase() {
    auto fixture = buildFixture();
    Case benchCase;
    benchCase.itemsPerIteration = kOverlapCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture] {
        size_t found = 0;
        for (size_t index = 0; index < kOverlapCount; ++index) {
            fixture->world->overlapSphere(fixture->origins[index], kOverlapRadius, fixture->scratch);
            found += fixture->scratch.size();
        }
        sink(static_cast<double>(found));
    };
    return benchCase;
}

Case frustumCase() {
    auto fixture = buildFixture();
    Camera camera;
    camera.updateAspectRatio(kAspectWidth, kAspectHeight);
    camera.setNearPlane(kNearPlane);
    camera.setFarPlane(kFarPlane);
    auto sharedCamera = std::make_shared<Camera>(camera);
    Case benchCase;
    benchCase.itemsPerIteration = kFrustumCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture, sharedCamera] {
        Pcg32 rng(kSeed + 2);
        size_t visible = 0;
        for (size_t index = 0; index < kFrustumCount; ++index) {
            sharedCamera->setPos(fixture->origins[index]);
            sharedCamera->setOrientation(rng.uniform() * kYawRangeDegrees, (rng.uniform() - kBenchHalf) * kPitchRangeDegrees);
            fixture->world->queryFrustum(*sharedCamera, fixture->scratch);
            visible += fixture->scratch.size();
        }
        sink(static_cast<double>(visible));
    };
    return benchCase;
}

Case refreshCase() {
    auto fixture = buildFixture();
    auto phase = std::make_shared<float>(0.0f);
    Case benchCase;
    benchCase.itemsPerIteration = kMovedCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture, phase] {
        *phase += kMoveStep;
        for (size_t index = 0; index < kMovedCount; ++index) fixture->world->translate(fixture->handles[index], glm::vec3(kMoveStep, 0.0f, 0.0f));
        sink(static_cast<double>(fixture->world->overlapSphere(glm::vec3(0.0f), 1.0f).size()));
    };
    return benchCase;
}

Case missCase(size_t entityCount, float extent, float maxDistance, bool inside = false) {
    auto fixture = std::make_shared<SpatialFixture>();
    fixture->world = makeScatteredWorld(entityCount, extent);
    Pcg32 rng(kSeed + 3);
    for (size_t index = 0; index < kRayCount; ++index) {
        glm::vec3 origin = randomPoint(rng, extent);
        if (!inside) origin.y = extent;
        fixture->origins.push_back(origin);
        fixture->directions.push_back(inside ? randomDirection(rng) : glm::vec3(rng.uniform() - kBenchHalf, 1.0f, rng.uniform() - kBenchHalf));
    }
    fixture->world->overlapSphere(glm::vec3(0.0f), 1.0f);
    Case benchCase;
    benchCase.itemsPerIteration = kRayCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture, maxDistance] {
        size_t hits = 0;
        for (size_t index = 0; index < kRayCount; ++index) hits += fixture->world->raycast(fixture->origins[index], fixture->directions[index], maxDistance).has_value() ? 1 : 0;
        sink(static_cast<double>(hits));
    };
    return benchCase;
}

Case clusteredCase() {
    auto fixture = std::make_shared<SpatialFixture>();
    fixture->world = makeEmptyWorld();
    Pcg32 rng(kSeed + 4);
    for (size_t cluster = 0; cluster < kClusterCount; ++cluster) {
        const glm::vec3 center = randomPoint(rng, kClusteredExtent);
        fixture->world->createBatch(kClusterMembers, [&](size_t, EntityDesc& desc) {
            desc.mesh = kBenchMesh;
            desc.position = center + (randomPoint(rng, kClusterSpread));
        });
    }
    for (size_t index = 0; index < kRayCount; ++index) {
        fixture->origins.push_back(randomPoint(rng, kClusteredExtent));
        fixture->directions.push_back(randomDirection(rng));
    }
    fixture->world->overlapSphere(glm::vec3(0.0f), 1.0f);
    Case benchCase;
    benchCase.itemsPerIteration = kRayCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture] {
        size_t hits = 0;
        for (size_t index = 0; index < kRayCount; ++index) hits += fixture->world->raycast(fixture->origins[index], fixture->directions[index], kLongRange).has_value() ? 1 : 0;
        sink(static_cast<double>(hits));
    };
    return benchCase;
}

std::shared_ptr<SpatialFixture> buildFarFixture() {
    auto fixture = std::make_shared<SpatialFixture>();
    fixture->world = makeScatteredWorld(kEntityCount);
    for (size_t index = 0; index < kFarCount; ++index) {
        const EntityHandle far = fixture->world->create();
        fixture->world->setMesh(far, kBenchMesh);
        fixture->world->setPosition(far, glm::vec3(kFarDistance * static_cast<float>(index + 1), 0.0f, -kFarDistance));
    }
    Pcg32 rng(kSeed + 1);
    for (size_t index = 0; index < kRayCount; ++index) fixture->origins.push_back(randomPoint(rng, kBenchSceneExtent));
    fixture->world->overlapSphere(glm::vec3(0.0f), 1.0f);
    return fixture;
}

Case farOverlapCase() {
    auto fixture = buildFarFixture();
    Case benchCase;
    benchCase.itemsPerIteration = kOverlapCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture] {
        size_t found = 0;
        for (size_t index = 0; index < kOverlapCount; ++index) {
            fixture->world->overlapSphere(fixture->origins[index], kOverlapRadius, fixture->scratch);
            found += fixture->scratch.size();
        }
        sink(static_cast<double>(found));
    };
    return benchCase;
}

Case farFrustumCase() {
    auto fixture = buildFarFixture();
    auto sharedCamera = std::make_shared<Camera>();
    sharedCamera->updateAspectRatio(kAspectWidth, kAspectHeight);
    sharedCamera->setNearPlane(kNearPlane);
    sharedCamera->setFarPlane(kFarPlane);
    Case benchCase;
    benchCase.itemsPerIteration = kFrustumCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture, sharedCamera] {
        Pcg32 rng(kSeed + 2);
        size_t visible = 0;
        for (size_t index = 0; index < kFrustumCount; ++index) {
            sharedCamera->setPos(fixture->origins[index]);
            sharedCamera->setOrientation(rng.uniform() * kYawRangeDegrees, (rng.uniform() - kBenchHalf) * kPitchRangeDegrees);
            fixture->world->queryFrustum(*sharedCamera, fixture->scratch);
            visible += fixture->scratch.size();
        }
        sink(static_cast<double>(visible));
    };
    return benchCase;
}

}

void registerSpatialBenches() {
    add("spatial.raycast_miss_sparse_1k_1e6_unbounded", [] { return missCase(kSparseCount, kSparseExtent, kUnbounded); });
    add("spatial.raycast_miss_sparse_1k_1e6_max100", [] { return missCase(kSparseCount, kSparseExtent, kShortRange); });
    add("spatial.raycast_miss_dense_100k_1km_unbounded", [] { return missCase(kEntityCount, kDenseExtent, kUnbounded); });
    add("spatial.raycast_miss_dense_100k_1km_max100", [] { return missCase(kEntityCount, kDenseExtent, kShortRange); });
    add("spatial.raycast_inside_sparse_1k_1e6_unbounded", [] { return missCase(kSparseCount, kSparseExtent, kUnbounded, true); });
    add("spatial.raycast_clustered_20k_4km_max1500", clusteredCase);
    add("spatial.raycast_inside_sparse_1k_1e6_max100", [] { return missCase(kSparseCount, kSparseExtent, kShortRange, true); });
    add("spatial.overlap_sphere_100k_far8_x1000", farOverlapCase);
    add("spatial.frustum_100k_far8_x100", farFrustumCase);
    add("spatial.raycast_100k_x1000", raycastCase);
    add("spatial.overlap_sphere_100k_x1000", overlapCase);
    add("spatial.frustum_100k_x100", frustumCase);
    add("spatial.refresh_move1000_100k", refreshCase);
}

}
